#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>

#define FIFO_PATH "/tmp/kernel_fifo"
#define MAX 10

int pc = 0;
int acessos_D1 = 0;
int acessos_D2 = 0;
int dispositivo_atual = 0;
char operacao_atual = '-';

void sys_call(int dispositivo) {
    int fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("[App] Erro ao abrir FIFO");
        return;
    }

    int msg = 10 + dispositivo;
    write(fd, &msg, sizeof(int));
    close(fd);

    dispositivo_atual = dispositivo;
    int op = rand() % 3;
    if (op == 0) operacao_atual = 'R';
    else if (op == 1) operacao_atual = 'W';
    else operacao_atual = 'X';

    if (dispositivo == 1) acessos_D1++;
    else acessos_D2++;

    printf("[App %d] -> Bloqueando por D%d (Operação %c, PC=%d)\n",
           getpid(), dispositivo, operacao_atual, pc);

    kill(getpid(), SIGSTOP);
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
