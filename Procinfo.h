#pragma once
#include <unistd.h>
#include "State.h"

typedef struct {
    pid_t pid;
    State estado;
    int dispositivo;
    char operacao;
    int acessos_D1;
    int acessos_D2;
    int pc;
} ProcInfo;