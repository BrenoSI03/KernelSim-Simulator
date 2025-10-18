#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "MsgSyscall.h"

#define FIFO_PATH "/tmp/kernel_fifo"
#define MAX 10

int pc = 0;
int acessos_D1 = 0, acessos_D2 = 0;

void sys_call(int dispositivo) {
    int fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    if (fd < 0) return;

    MsgSyscall msg;
    msg.pid = getpid();
    msg.tipo = 10 + dispositivo; // 11 ou 12
    msg.pc = pc;
    msg.acessos_D1 = acessos_D1;
    msg.acessos_D2 = acessos_D2;
    msg.dispositivo = dispositivo;

    int op = rand() % 3;
    msg.operacao = (op == 0) ? 'R' : (op == 1) ? 'W' : 'X';

    if (dispositivo == 1) acessos_D1++;
    else acessos_D2++;

    write(fd, &msg, sizeof(msg));
    close(fd);

    printf("[App %d] -> solicitou por D%d (Operação %c, PC=%d)\n",
           msg.pid, msg.dispositivo, msg.operacao, msg.pc);
}

int main() {
    srand(time(NULL) ^ getpid());
    printf("[App %d] iniciado.\n", getpid());

    while (pc < MAX) {
        sleep(1);
        if (rand() % 100 < 15) {
            int dispositivo = (rand() % 2) + 1;
            sys_call(dispositivo);
        }
        pc++;
    }

    printf("[App %d] terminou. PC final=%d | D1=%d | D2=%d\n",
           getpid(), pc, acessos_D1, acessos_D2);
    return 0;
}
