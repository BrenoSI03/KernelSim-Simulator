#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>

#define FIFO_PATH "/tmp/kernel_fifo"
#define MAX 10

void sys_call(int dispositivo) {
    int fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("[App] Erro ao abrir FIFO");
        return; // não sai do processo, apenas pula o envio
    }

    int msg = 10 + dispositivo; // 11 -> D1, 12 -> D2
    write(fd, &msg, sizeof(int));

    printf("[App %d] -> Bloqueando por D%d (enviando msg=%d)\n",
           getpid(), dispositivo, msg);

    close(fd);

    kill(getpid(), SIGSTOP);  // pausa o próprio processo
}

int main() {
    srand(time(NULL) ^ getpid());
    int pc = 0;
    printf("[App %d] iniciado.\n", getpid());

    while (pc < MAX) {
        sleep(1);
        if (rand() % 100 < 15) {
            int dispositivo = (rand() % 2) + 1;
            sys_call(dispositivo);
        }
        pc++;
    }

    printf("[App %d] terminou.\n", getpid());
    return 0;
}
