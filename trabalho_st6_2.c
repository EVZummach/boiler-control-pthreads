#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <ncurses.h>

static pthread_t t1;
static pthread_t task_console;		// Thread para interface via console

static int valor_1 = 0;
static int valor_2 = 0;
static pthread_mutex_t emValorLido = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t tela = PTHREAD_MUTEX_INITIALIZER;

struct param_t {
	int ns;
	char *nome;
};

void codigo_tarefa(struct param_t *pparam){
	for( int ns=1; ns <= pparam->ns; ++ns) {
		sleep(1);
		pthread_mutex_lock( &emValorLido);
		valor_1 = ns;
		pthread_mutex_unlock( &emValorLido);
		
	}
}

void console_init(){#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <ncurses.h>

static pthread_t t1;
static pthread_t task_console;		// Thread para interface via console

static int valor_1 = 0;
static pthread_mutex_t emValorLido = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t tela = PTHREAD_MUTEX_INITIALIZER;

struct param_t {
	int ns;
	char *nome;
};

void codigo_tarefa(struct param_t *pparam){
	for( int ns=0; ns < pparam->ns; ++ns) {
		sleep(1);
		pthread_mutex_lock( &emValorLido);
		valor_1 = ns;
		pthread_mutex_unlock( &emValorLido);
		
	}
}

void console_init(){
	initscr();			// Inicializa o terminal em modo curses
	start_color();		// Inicializa as cores
	raw();				// Desativa o buffer de linha, CTRL-Z e CTRL-C não disparam sinais
	noecho();			// Teclado nao ecoa no display
	keypad(stdscr, TRUE);		// Permite leitura de teclas de função
}

void console_thread(){
	while(1){
		pthread_mutex_lock(&tela);
		pthread_mutex_lock( &emValorLido);
		mvprintw(0, 0, "Passaram %d segundos\n", valor_1);
		pthread_mutex_unlock( &emValorLido);
		pthread_mutex_unlock(&tela);
		refresh();
	}
	
}

void console_end(){
	keypad(stdscr, FALSE);	// Retorna ao default
	noraw();				// Retorna ao default
	echo();					// Retorna ao default
	endwin();				// Encerra o modo curses
}

int main(){

	struct param_t p1;
	
	p1.ns = 10;
	p1.nome = "TAREFA 1";

	pthread_create(&t1, NULL, (void *) codigo_tarefa, (void *) &p1);

	console_init();
	
	pthread_create( &task_console, NULL, (void *) console_thread, NULL);

	pthread_join(t1, NULL);
	console_end();
	printf("Fim\n");
}

	initscr();			// Inicializa o terminal em modo curses
	start_color();		// Inicializa as cores
	raw();				// Desativa o buffer de linha, CTRL-Z e CTRL-C não disparam sinais
	noecho();			// Teclado nao ecoa no display
	keypad(stdscr, TRUE);		// Permite leitura de teclas de função
}

void console_thread(){
	while(1){
		pthread_mutex_lock(&tela);
		pthread_mutex_lock( &emValorLido);
		mvprintw(0, 0, "Passaram %d segundos na tarefa 1\n", valor_1);
		pthread_mutex_unlock( &emValorLido);
		pthread_mutex_unlock(&tela);
		refresh();
	}
	
}

void console_end(){
	keypad(stdscr, FALSE);	// Retorna ao default
	noraw();				// Retorna ao default
	echo();					// Retorna ao default
	endwin();				// Encerra o modo curses
}

int main(){

	struct param_t p1;
	
	p1.ns = 10;
	p1.nome = "TAREFA 1";

	pthread_create(&t1, NULL, (void *) codigo_tarefa, (void *) &p1);

	console_init();
	
	pthread_create( &task_console, NULL, (void *) console_thread, NULL);

	pthread_join(t1, NULL);
	console_end();
	printf("Fim\n");
}
