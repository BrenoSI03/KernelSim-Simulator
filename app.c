#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "MsgSyscall.h"

#define FIFO_SYSCALL "/tmp/fifo_syscall"
#define MAX 50

int pc = 0;
int acessos_D1 = 0, acessos_D2 = 0;

/* Restaura PC salvo quando volta a rodar */
void trata_sigcont(int sig){
    FILE *f;
    char fname[50];
    sprintf(fname, "/tmp/context_%d", getpid());
    f = fopen(fname, "r");
    if (f) {
        fscanf(f, "%d", &pc);
        fclose(f);
    }
}

/* Salva PC atual em arquivo simples */
void salvar_contexto() {
    FILE *f;
    char fname[50];
    sprintf(fname, "/tmp/context_%d", getpid());
    f = fopen(fname, "w");
    if (!f) return;
    fprintf(f, "%d\n", pc);
    fclose(f);
}

/* Envia solicitação de E/S (D1/D2) ao kernel e se bloqueia */
void sys_call(int dispositivo) {
    int fd = open(FIFO_SYSCALL, O_WRONLY | O_NONBLOCK);
    if (fd < 0) return;

    /* incrementa apenas 1x por syscall */
    if (dispositivo == 1) acessos_D1++;
    else acessos_D2++;

    MsgSyscall msg;
    msg.pid = getpid();
    msg.tipo = 10 + dispositivo; // 11 (D1) ou 12 (D2)
    msg.pc = pc;
    msg.acessos_D1 = acessos_D1;
    msg.acessos_D2 = acessos_D2;
    msg.dispositivo = dispositivo;
    int op = rand() % 3;
    msg.operacao = (op == 0) ? 'R' : (op == 1) ? 'W' : 'X';

    write(fd, &msg, sizeof(msg));
    close(fd);

    printf("[App %d] -> solicitou por D%d (Operação %c, PC=%d)\n",
           msg.pid, msg.dispositivo, msg.operacao, msg.pc);

    /* Bloqueia a si mesmo até o kernel retomar */
    kill(getpid(), SIGSTOP);
}

int main() {
    srand(time(NULL) ^ getpid());
    printf("[App %d] iniciado.\n", getpid());

    signal(SIGCONT, trata_sigcont);

    while (pc < MAX) {
        salvar_contexto();
        sleep(500000);  // 0.5 s

        // ~15% de chance de syscall (baixa probabilidade)
        if (rand() % 100 < 15) {
            int dispositivo = (rand() % 2) + 1; // 1 ou 2
            sys_call(dispositivo);
        }
        else{
            printf("\nFora\n");
        }

        usleep(500000);  // 0.5 s
        pc++;
        salvar_contexto();
    }

    printf("[App %d] terminou. PC final=%d | D1=%d | D2=%d\n",
           getpid(), pc, acessos_D1, acessos_D2);
    salvar_contexto();
    return 0;
}
