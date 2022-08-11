#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <ncurses.h>
#include <pthread.h>

#define FALHA -1

#define MAX_NIVEL 0.6
#define PERC_CONTROLE 0.75 
#define MAX_PERC_CONTROLE 0.9
#define MAX_DIFERENCA_CANOS 5
#define MAX_DIFERENCA_BOILER_AQ 2
#define MAX_DIFERENCA_BOILER_CO 2

#define COL_VALORES 25

#define PERIODO 30000000		
#define PERIODO_PRINT 300000000
#define PERIODO_ARMAZENAGEM 1000000000

#define NSEC_POR_SEC	(1000000000)	// Numero de nanosegundos em um segundo (1 bilhao)
#define USEC_POR_SEC	(1000000)	// Numero de microssegundos em um segundo (1 milhao)
#define NSEC_POR_USEC	(1000)		// Numero de nanosegundos em um microsegundo (1 mil)

#define COLETOR 0
#define CIRCULA 1
#define AQUECE 2
#define ENTRADA 3
#define ESGOTO 4

char comandos_sensores[4][100] = {{"nivelboiler"}, {"tempboiler"}, {"tempcoletor"}, {"tempcanos"}};
char comandos_atuadores[5][100] = {{"bombacoletor "}, {"bombacirculacao "}, {"aquecedor "}, {"valvulaentrada "}, {"valvulaesgoto "}};

FILE* armazena;

int roda_simul = 1;

static pthread_t controlador;
static pthread_t cliente;
static pthread_t task_console;
static pthread_t registra_dados;
static pthread_t teclado;

struct sensores{
	float nivel_boiler;
	float temp_boiler;
	float temp_coletor;
	float temp_canos;
};

struct atuadores{
	char bomba_coletor;
	char bomba_circ;
	char aquecedor;
	char valvula_entrada;
	char valvula_esgoto;
};

struct sensores entradas;
struct atuadores atuador;
struct atuadores retencao;


static pthread_mutex_t valores = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t saidas = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t tela = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_time = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_temp = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_simul = PTHREAD_MUTEX_INITIALIZER;

struct timespec t;				// Hora atual
struct timespec tp;				// Hora de inicio para o periodo de interesse
struct timespec tc;				// Hora de inicio para o periodo de interesse
struct timespec tf;				// Hora de inicio para o periodo de interesse

static long diferenca_us;				// Diferenca em microsegundos
static pthread_mutex_t diff = PTHREAD_MUTEX_INITIALIZER;

static int porta_destino;
static int porta_server;
static int socket_local_c;
static int socket_local_s;
static struct sockaddr_in endereco_destino;


struct param_t {
	int ns;
	char nome;
};


int temperatura = 45;


int cria_socket_local(void)
{
	int socket_local;		/* Socket usado na comunicacão */

	socket_local = socket( PF_INET, SOCK_DGRAM, 0);
	if (socket_local < 0) {
		perror("socket");
		return -1;
	}
	return socket_local;
}

struct sockaddr_in cria_endereco_destino(char *destino, int porta_destino)
{
	struct sockaddr_in servidor; 	/* Endereço do servidor incluindo ip e porta */
	struct hostent *dest_internet;	/* Endereço destino em formato próprio */
	struct in_addr dest_ip;		/* Endereço destino em formato ip numérico */

	if (inet_aton ( destino, &dest_ip ))
		dest_internet = gethostbyaddr((char *)&dest_ip, sizeof(dest_ip), AF_INET);
	else
		dest_internet = gethostbyname(destino);

	if (dest_internet == NULL) {
		fprintf(stderr,"Endereco de rede invalido\n");
		exit(FALHA);
	}

	memset((char *) &servidor, 0, sizeof(servidor));
	memcpy(&servidor.sin_addr, dest_internet->h_addr_list[0], sizeof(servidor.sin_addr));
	servidor.sin_family = AF_INET;
	servidor.sin_port = htons(porta_destino);

	return servidor;
}

static void define_porta_escutada(int socket_local, int porta_escutada)
{
	struct sockaddr_in servidor;	// Endereço completo do servidor e do cliente
	int tam_s;						// Tamanho da estrutura servidor
	
	memset((char *) &servidor, 0, sizeof(servidor));
	servidor.sin_family = AF_INET;
	servidor.sin_addr.s_addr = htonl(INADDR_ANY);
	servidor.sin_port = htons(porta_escutada);

	tam_s = sizeof(servidor);
	if (bind(socket_local, (struct sockaddr *) &servidor, tam_s) < 0) {
		perror("bind");
		exit(-1);
	}
}

void envia_mensagem_c(int socket_local, struct sockaddr_in endereco_destino, char *mensagem)
{
	/* Envia msg ao servidor */

	if (sendto(socket_local, mensagem, strlen(mensagem)+1, 0, (struct sockaddr *) &endereco_destino, sizeof(endereco_destino)) < 0 )
	{
		perror("sendto");
		return;
	}
}

int recebe_mensagem_c(int socket_local, char *buffer, int TAM_BUFFER)
{
	int bytes_recebidos;		/* Número de bytes recebidos */

	/* Espera pela msg de resposta do servidor */
	bytes_recebidos = recvfrom(socket_local, buffer, TAM_BUFFER, 0, NULL, 0);
	if (bytes_recebidos < 0)
	{
		perror("recvfrom");
	}

	return bytes_recebidos;
}

int recebe_mensagem_s( char *buffer, int tam_buffer, int socket_local, struct sockaddr_in *endereco_cliente, int *tam_c)
{
	int bytes_recebidos;			/* Número de bytes recebidos */

	memset( (char *)endereco_cliente, 0, sizeof(struct sockaddr_in));
	endereco_cliente->sin_family = AF_INET;

	*tam_c = sizeof(struct sockaddr_in);
	bytes_recebidos = recvfrom(socket_local, buffer, tam_buffer, 0, (struct sockaddr *) endereco_cliente, tam_c);
	if (bytes_recebidos < 0) {
		perror("recvfrom");
		exit(FALHA);
	}
	return bytes_recebidos;
}



void controlador_init(){
	pthread_mutex_lock( &mutex_time);
	clock_gettime(CLOCK_MONOTONIC, &t);
	pthread_mutex_unlock( &mutex_time);

	tp.tv_sec = t.tv_sec + 1;
	tp.tv_nsec = t.tv_nsec;
}

float controlador_valorSensores(char recebida[1000], char enviada[100]){
	float valor_sensor;
	int recebida_len = strlen(recebida);
	int enviada_len = strlen(enviada);
	
	char valor[100] = {};
	
	if (recebida_len > enviada_len)	strncpy(valor, recebida+enviada_len+1, recebida_len-enviada_len);
	
	valor_sensor = strtof(valor, NULL);
	
	return valor_sensor;
}

void controlador_lerDados(){
	char msg_recebida[1000];
	pthread_mutex_lock( &valores);
	
	for (int i = 0; i < 4; i++){
		envia_mensagem_c(socket_local_c, endereco_destino, comandos_sensores[i]);
		recebe_mensagem_c(socket_local_c, msg_recebida, 1000);
		float sensor = controlador_valorSensores(msg_recebida, comandos_sensores[i]);		
		
		switch(i){
			case 0:
				entradas.nivel_boiler = sensor;
				break;
			case 1:
				entradas.temp_boiler = sensor;
				break;
			case 2:
				entradas.temp_coletor = sensor;
				break;
			case 3:
				entradas.temp_canos = sensor;
				break;
			default:
				break;
		}		
	}
	
	pthread_mutex_unlock( &valores);
}

void controlador_enviaAtuador(char *comando, char estado){
	char comando_aux[20] = "";
	char msg_recebida_atuador[1000];
	
	strcpy(comando_aux, comando);
	
	strcat(comando_aux, &estado);
				
	envia_mensagem_c(socket_local_c, endereco_destino, comando_aux);
	recebe_mensagem_c(socket_local_c, msg_recebida_atuador, 1000);
}

void controlador_processaDados(){
	
	pthread_mutex_lock( &saidas);
	
	//######################################
	//			   CIRCULAÇÃO
	//######################################
	if(entradas.temp_canos <= (entradas.temp_boiler-MAX_DIFERENCA_CANOS) || retencao.bomba_circ == '1'){
		atuador.bomba_circ = '1';
		retencao.bomba_circ = '1';
	}
	if(entradas.temp_canos >= (entradas.temp_boiler-2)){
		atuador.bomba_circ = '0';
		retencao.bomba_circ = '0';
	}
	controlador_enviaAtuador(comandos_atuadores[CIRCULA], atuador.bomba_circ);
		
	
	//######################################
	//		     NÍVEL DO BOILER
	//######################################
	if(entradas.nivel_boiler <= (MAX_NIVEL*PERC_CONTROLE) || retencao.valvula_entrada == '1'){
		atuador.valvula_entrada = '1';
		retencao.valvula_entrada = '1';
	}
	if(entradas.nivel_boiler >=(MAX_NIVEL*MAX_PERC_CONTROLE)){
		atuador.valvula_entrada = '0';
		retencao.valvula_entrada = '0';
	}
	controlador_enviaAtuador(comandos_atuadores[ENTRADA], atuador.valvula_entrada);
	
	
	
	//######################################
	//			    COLETOR
	//######################################
	pthread_mutex_lock( &mutex_temp);
	if(entradas.temp_boiler <= (temperatura-MAX_DIFERENCA_BOILER_CO) || retencao.bomba_coletor == '1'){
		atuador.bomba_coletor = '1';
		retencao.bomba_coletor = '1';
	}
	if(entradas.temp_boiler >= (temperatura)){
		atuador.bomba_coletor = '0';
		retencao.bomba_coletor = '0';
	}
	pthread_mutex_unlock( &mutex_temp);
	
	controlador_enviaAtuador(comandos_atuadores[COLETOR], atuador.bomba_coletor);
	
	
	
	//######################################
	//			    AQUECEDOR
	//######################################
	pthread_mutex_lock( &mutex_temp);
	if(entradas.temp_boiler <= (temperatura-MAX_DIFERENCA_BOILER_AQ) || retencao.aquecedor == '1'){
		atuador.aquecedor = '1';
		retencao.aquecedor = '1';
	}
	if(entradas.temp_boiler >= (temperatura+MAX_DIFERENCA_BOILER_AQ)){
		atuador.aquecedor = '0';
		retencao.aquecedor = '0';
	}
	pthread_mutex_unlock( &mutex_temp);
	
	controlador_enviaAtuador(comandos_atuadores[AQUECE], atuador.aquecedor);
	
	pthread_mutex_unlock( &saidas);
}

void controlador_run(){
	int periodo_ns = PERIODO;			// 300 ms = 300 000 000 ns
	while(1){
		clock_nanosleep( CLOCK_MONOTONIC, TIMER_ABSTIME, &tp, NULL);	
		
		pthread_mutex_lock( &mutex_time);
		clock_gettime(CLOCK_MONOTONIC, &t);		
		
		pthread_mutex_lock( &diff);
		diferenca_us = ( t.tv_sec - tp.tv_sec ) * USEC_POR_SEC + ( t.tv_nsec - tp.tv_nsec) / NSEC_POR_USEC;
		pthread_mutex_unlock( &diff);
		
		pthread_mutex_unlock( &mutex_time);
		
		controlador_lerDados();
		controlador_processaDados();
				
		tp.tv_nsec += periodo_ns;
		
		while (tp.tv_nsec >= NSEC_POR_SEC) {
			tp.tv_nsec -= NSEC_POR_SEC;
			tp.tv_sec++;
		}
	}
}




void console_init(){
	initscr();			// Inicializa o terminal em modo curses
	start_color();		// Inicializa as cores
	//raw();				// Desativa o buffer de linha, CTRL-Z e CTRL-C não disparam sinais
	noecho();			// Teclado nao ecoa no display
	curs_set(0);
	keypad(stdscr, TRUE);		// Permite leitura de teclas de função
	
	pthread_mutex_lock( &mutex_time);
	clock_gettime(CLOCK_MONOTONIC, &t);
	pthread_mutex_unlock( &mutex_time);
	
	tc.tv_sec = t.tv_sec + 1;
	tc.tv_nsec = t.tv_nsec;
}

void console_thread(){
	int periodo_ns = PERIODO_PRINT;			// 300 ms = 300 000 000 ns
	long diferenca_us_print;				// Diferenca em microsegundos
	while(1){
		clock_nanosleep( CLOCK_MONOTONIC, TIMER_ABSTIME, &tc, NULL);
		
		pthread_mutex_lock( &mutex_time);
		clock_gettime(CLOCK_MONOTONIC, &t);
		diferenca_us_print = ( t.tv_sec - tc.tv_sec ) * USEC_POR_SEC + ( t.tv_nsec - tc.tv_nsec) / NSEC_POR_USEC;
		pthread_mutex_unlock( &mutex_time);
		
		pthread_mutex_lock(&tela);
		
		mvprintw(0, 0, "TRABALHO ST6 - TECNICAS DE IMPLEMENTACAO DE SISTEMAS AUTOMATIZADOS - EDUARDO VARGAS ZUMMACH\n");
		
		
		mvprintw(2, 0, "ENTRADAS");
		mvprintw(4, 0, "Nível do boiler:");
		mvprintw(5, 0, "Temperatura do boiler:");
		mvprintw(6, 0, "Temperatura dos canos:");
		mvprintw(7, 0, "Temperatura do coletor:");
		
		pthread_mutex_lock( &valores);
		mvprintw(4, COL_VALORES, "%0.3f\n", entradas.nivel_boiler);
		mvprintw(5, COL_VALORES, "%0.3f\n", entradas.temp_boiler);
		mvprintw(6, COL_VALORES, "%0.3f\n", entradas.temp_canos);
		mvprintw(7, COL_VALORES, "%0.3f\n", entradas.temp_coletor);
		pthread_mutex_unlock( &valores);		
		
		mvprintw(9, 0, "Set temperatura:\n");
		pthread_mutex_lock( &mutex_temp);
		mvprintw(9, COL_VALORES, "%ld\n", temperatura);
		pthread_mutex_unlock( &mutex_temp);
		mvprintw(11, 0, "Pressione 't' para digitar nova temperatura, ou pressione +/-\n");
		move(11, 0);
		
		
		mvprintw(14, 0, "SAIDAS\n");
		mvprintw(16, 0, "Bomba Coletor:\n");
		mvprintw(17, 0, "Bomba Recirculacao:\n");
		mvprintw(18, 0, "Aquecedor Boiler:\n");
		mvprintw(19, 0, "Valvula Entrada:\n");
		mvprintw(20, 0, "Valvula Esgoto:\n");
		
		pthread_mutex_lock( &saidas);
		mvprintw(16, COL_VALORES, "%s\n", atuador.bomba_coletor == '1' ? "LIGADA" : "DESLIGADA");
		mvprintw(17, COL_VALORES, "%s\n", atuador.bomba_circ == '1' ? "LIGADA" : "DESLIGADA");
		mvprintw(18, COL_VALORES, "%s\n", atuador.aquecedor == '1' ? "LIGADA" : "DESLIGADA");
		mvprintw(19, COL_VALORES, "%s\n", atuador.valvula_entrada == '1' ? "LIGADA" : "DESLIGADA");
		mvprintw(20, COL_VALORES, "%s\n", atuador.valvula_esgoto == '1' ? "LIGADA" : "DESLIGADA");
		pthread_mutex_unlock( &saidas);
		
		mvprintw(22, 0, "Latencia Controlador:");
		pthread_mutex_lock( &diff);
		mvprintw(22, COL_VALORES, "%ld us\n", diferenca_us);
		pthread_mutex_unlock( &diff);
		
		mvprintw(23, 0, "Latencia Print:");
		mvprintw(23, COL_VALORES, "%ld us\n", diferenca_us_print);
		
		time_t clk = time(NULL);
		char s[1000];
		struct tm * p = localtime(&clk);
		strftime(s, 1000, "%d/%m/%Y %H:%M:%S", p);
		
		mvprintw(24, 0, "Timestamp: %s\n", s);
		
		pthread_mutex_unlock(&tela);
		
		tc.tv_nsec += periodo_ns;

		while (tc.tv_nsec >= NSEC_POR_SEC) {
			tc.tv_nsec -= NSEC_POR_SEC;
			tc.tv_sec++;
		}
		
		refresh();
		
		pthread_mutex_lock( &mutex_simul);
		if (!roda_simul) break;
		pthread_mutex_unlock( &mutex_simul);
	}
	
	
}

void console_end(){
	pthread_mutex_lock(&tela);
	erase();
	mvprintw(0, 0, "ENCERRANDO!\n");
	refresh();
	sleep(3);
	pthread_mutex_unlock(&tela);
	
	keypad(stdscr, FALSE);	// Retorna ao default
	noraw();				// Retorna ao default
	echo();					// Retorna ao default
	endwin();				// Encerra o modo curses
}




int armazenagem_init(){
	pthread_mutex_lock( &mutex_time);
	clock_gettime(CLOCK_MONOTONIC, &t);
	pthread_mutex_unlock( &mutex_time);
	
	tf.tv_sec = t.tv_sec + 1;
	tf.tv_nsec = t.tv_nsec;		
	
	if (access("test.log", F_OK) != 0) {
		char cmd[200];
		strcpy(cmd, "timestamp");
		strcat(cmd, ",nivelboiler,tempboiler,tempcoletor,tempcanos\n");
		armazena = fopen("test.log", "a+");
		fprintf(armazena, "%s", cmd);		
	}
	else armazena = fopen("test.log", "a+");
	
	if (NULL == armazena)	return 0;
    
    return 1;
}

void armazenagem_thread(){
	int periodo_ns = PERIODO_ARMAZENAGEM;			// 300 ms = 300 000 000 ns
	char comando_atual[200];
	char val_buffer[10];

	while(1){
		clock_nanosleep( CLOCK_MONOTONIC, TIMER_ABSTIME, &tf, NULL);
		
		pthread_mutex_lock( &mutex_time);
		clock_gettime(CLOCK_MONOTONIC, &t);
		pthread_mutex_unlock( &mutex_time);
		
		time_t clk = time(NULL);
		char s[1000];
		struct tm * p = localtime(&clk);
		strftime(s, 1000, "%d/%m/%Y %H:%M:%S", p);
		
		pthread_mutex_lock( &valores);
		strcpy(comando_atual, s);
		strcat(comando_atual, ",");
		
		gcvt(entradas.nivel_boiler, 4, val_buffer);
		strcat(comando_atual, val_buffer);
		strcat(comando_atual, ",");
		
		gcvt(entradas.temp_boiler, 4, val_buffer);
		strcat(comando_atual, val_buffer);
		strcat(comando_atual, ",");
		
		float temp_aux = entradas.temp_coletor;
		gcvt(entradas.temp_coletor, 4, val_buffer);
		strcat(comando_atual, val_buffer);
		strcat(comando_atual, ",");
		
		gcvt(entradas.temp_canos, 4, val_buffer);
		strcat(comando_atual, val_buffer);
		strcat(comando_atual, "\n");
		pthread_mutex_unlock( &valores);
		
		if(temp_aux > 0) fprintf(armazena, "%s", comando_atual);
		
		
		tf.tv_nsec += periodo_ns;

		while (tf.tv_nsec >= NSEC_POR_SEC) {
			tf.tv_nsec -= NSEC_POR_SEC;
			tf.tv_sec++;
		}
	}
}

void armazenagem_end(){
	fclose(armazena);
}



int teclado_thread_fgets(){
	char linha[100];
	
	pthread_mutex_lock(&tela);
	mvprintw(12, 0, "Novo valor manual:\n");
	refresh();
	pthread_mutex_unlock(&tela);
	
	getstr(linha);
	pthread_mutex_lock( &mutex_temp);
	temperatura = atoi(linha);
	pthread_mutex_unlock( &mutex_temp);
	
	pthread_mutex_lock(&tela);
	mvprintw(12, 0, "                  ");
	refresh();
	pthread_mutex_unlock(&tela);
	
	return 1;
}

void teclado_thread(){
	int c;
	do{
		c = getch();
			
		switch(c){
			case '+':
				pthread_mutex_lock( &mutex_temp);
				temperatura += 1;
				pthread_mutex_unlock( &mutex_temp);
				break;
			case '-':
				pthread_mutex_lock( &mutex_temp);
				temperatura -= 1;
				pthread_mutex_unlock( &mutex_temp);
				break;
			case 'x':
				pthread_mutex_lock( &mutex_simul);
				roda_simul = 0;
				pthread_mutex_unlock( &mutex_simul);
				break;
			case 't':
				
				teclado_thread_fgets();
				break;
			default:
				break;
		}
	} while(c != 'x');
}



void cliente_thread(){
	int bytes_recebidos;
	char buffer[1000];

	struct sockaddr_in endereco_cliente;
	int tam_c;

	do{
		bytes_recebidos = recebe_mensagem_s( buffer, 1000, socket_local_s, &endereco_cliente, &tam_c);
		buffer[bytes_recebidos] = '\0';
		pthread_mutex_lock( &mutex_temp);
		temperatura = atoi(buffer);
		pthread_mutex_unlock( &mutex_temp);
		
	}while( 1 );
}




int main(int argc, char *argv[]) {

	if (argc < 3) {
		fprintf(stderr,"controlador <endereco> <porta> \n");
		fprintf(stderr,"<endereco> eh o DNS ou IP do servidor \n");
		fprintf(stderr,"<porta> eh o numero da porta do servidor \n");
		exit(FALHA);
	}
	
	// ###############################################
	// 		DEFINIÇÃO DE PARÂMETROS DO CLIENTE
	// ###############################################	
	porta_destino = atoi(argv[2]);
	socket_local_c = cria_socket_local();	
	endereco_destino = cria_endereco_destino(argv[1], porta_destino);
	
	
	// ###############################################
	// 		DEFINIÇÃO DE PARÂMETROS DO SERVIDOR
	// ###############################################	
	porta_server = porta_destino+1;
	socket_local_s = cria_socket_local();	
	define_porta_escutada(socket_local_s, porta_server);
	
	
	// ###############################################
	// 			INICIALIZAÇÃO DO CONTROLADOR
	// ###############################################
	controlador_init();
	console_init();
	
	if (!armazenagem_init()){
		fprintf(stderr, "Não foi possível abrir o arquivo de logs!\n");
		exit(FALHA);
	 }
	
	retencao.bomba_coletor = 0;
	retencao.bomba_circ = 0;
	retencao.aquecedor = 0;
	retencao.valvula_entrada = 0;
	retencao.valvula_esgoto = 0;

	// ###############################################
	// 		DEFINIÇÃO DE PARÂMETROS DO CLIENTE
	// ###############################################
	pthread_create(&cliente, NULL, (void *) cliente_thread, NULL);
	pthread_create(&controlador, NULL, (void *) controlador_run, NULL);	
	pthread_create(&registra_dados, NULL, (void *) armazenagem_thread, NULL);
	pthread_create(&teclado, NULL, (void *) teclado_thread, NULL);
		
	pthread_create( &task_console, NULL, (void *) console_thread, NULL);
	
	pthread_join(teclado, NULL);
	
	//while(1){
	//	if (!roda_simul) break;
	//	sleep(1);
	//}
	
	armazenagem_end();
	console_end();
}
