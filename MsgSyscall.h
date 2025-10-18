#pragma once
#include <unistd.h>

typedef struct {
    pid_t pid;
    int tipo;         // 11 ou 12
    int pc;
    int acessos_D1;
    int acessos_D2;
    int dispositivo;
    char operacao;
} MsgSyscall;