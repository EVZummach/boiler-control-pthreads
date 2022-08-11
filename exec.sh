#!/bin/bash
echo "Compilando arquivo"
gcc -o trabalho_st6 trabalho_st6.c -lpthread -lncurses
./trabalho_st6 localhost 40000