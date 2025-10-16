#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define NPROC 5
#define TIMESLICE 1
#define FIFO_PATH "/tmp/kernel_fifo"

pid_t fila_D1[NPROC];
pid_t fila_D2[NPROC];
int inicio_D1 = 0, fim_D1 = 0;
int inicio_D2 = 0, fim_D2 = 0;

pid_t apps[NPROC];
int current = 0;

void escalona_proximo() {
    kill(apps[current], SIGSTOP);
    current = (current + 1) % NPROC;
    kill(apps[current], SIGCONT);
}

void bloqueia_processo(pid_t pid, int dispositivo) {
    if (dispositivo == 1) {
        fila_D1[fim_D1++ % NPROC] = pid;
        kill(pid, SIGSTOP);
    } else if (dispositivo == 2) {
        fila_D2[fim_D2++ % NPROC] = pid;
        kill(pid, SIGSTOP);
    }
}

void desbloqueia_processo(int dispositivo) {
    pid_t pid;
    if (dispositivo == 1 && inicio_D1 < fim_D1) {
        pid = fila_D1[inicio_D1++ % NPROC];
        kill(pid, SIGCONT);
    } else if (dispositivo == 2 && inicio_D2 < fim_D2) {
        pid = fila_D2[inicio_D2++ % NPROC];
        kill(pid, SIGCONT);
    }
}

int main() {
    mkfifo(FIFO_PATH, 0666);

    for (int i = 0; i < NPROC; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            char prog[10];
            sprintf(prog, "./app%d", i + 1);
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
                    desbloqueia_processo(1);
                    break;
                case 2:
                    printf("[KernelSim] IRQ2 recebido: operação em D2 terminou.\n");
                    desbloqueia_processo(2);
                    break;
                case 11: // app bloqueado em D1
                    printf("[KernelSim] Processo %d bloqueado em D1.\n", apps[current]);
                    bloqueia_processo(apps[current], 1);
                    escalona_proximo();
                    break;
                case 12: // app bloqueado em D2
                    printf("[KernelSim] Processo %d bloqueado em D2.\n", apps[current]);
                    bloqueia_processo(apps[current], 2);
                    escalona_proximo();
                    break;
                default:
                    break;
            }
        }
    }


    close(fd);
    unlink(FIFO_PATH);
    return 0;
}
