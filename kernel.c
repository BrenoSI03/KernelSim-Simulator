#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define NPROC 5
#define TIMESLICE 1
#define FIFO_PATH "/tmp/kernel_fifo"

pid_t apps[NPROC];
int current = 0;

void escalona_proximo() {
    kill(apps[current], SIGSTOP);
    current = (current + 1) % NPROC;
    kill(apps[current], SIGCONT);
}

int main() {
    mkfifo(FIFO_PATH, 0666); 

    for (int i = 0; i < NPROC; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            char prog[10];
            sprintf(prog, "./app%d", i+1);
            execl(prog, prog, NULL);
            exit(0);
        } else {
            apps[i] = pid;
        }
    }

    sleep(1);
    kill(apps[current], SIGCONT);

    int fd = open(FIFO_PATH, O_RDONLY);
    int irq;

    while (1) {
        if (read(fd, &irq, sizeof(int)) > 0) {
            switch (irq) {
                case 0:
                    printf("[KernelSim] IRQ0 recebido: Troca de processo.\n");
                    escalona_proximo();
                    break;
                case 1:
                    printf("[KernelSim] IRQ1 recebido: operação em D1 terminou.\n");
                    break;
                case 2:
                    printf("[KernelSim] IRQ2 recebido: operação em D2 terminou.\n");
                    break;
            }
        }
    }

    close(fd);
    unlink(FIFO_PATH);
    return 0;
}
