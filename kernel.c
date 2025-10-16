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

int estado[NPROC]; // 0 = pronto, 1 = bloqueado, 2 = terminado

void escalona_proximo() {
    // Para o atual (se não estiver bloqueado ou terminado)
    if (estado[current] == 0)
        kill(apps[current], SIGSTOP);

    int tentativas = 0;
    do {
        current = (current + 1) % NPROC;
        tentativas++;
        // Se todos estiverem bloqueados ou terminados, não há quem rodar
        if (tentativas > NPROC) return;
    } while (estado[current] != 0);

    kill(apps[current], SIGCONT);
}

void bloqueia_processo(pid_t pid, int dispositivo) {
    if (dispositivo == 1) {
        fila_D1[fim_D1++ % NPROC] = pid;
    } else if (dispositivo == 2) {
        fila_D2[fim_D2++ % NPROC] = pid;
    }

    // marca como bloqueado
    for (int i = 0; i < NPROC; i++) {
        if (apps[i] == pid) estado[i] = 1;
    }
    kill(pid, SIGSTOP);
}

void desbloqueia_processo(int dispositivo) {
    pid_t pid;
    if (dispositivo == 1 && inicio_D1 < fim_D1) {
        pid = fila_D1[inicio_D1++ % NPROC];
    } else if (dispositivo == 2 && inicio_D2 < fim_D2) {
        pid = fila_D2[inicio_D2++ % NPROC];
    } else return;

    // marca como pronto
    for (int i = 0; i < NPROC; i++) {
        if (apps[i] == pid) estado[i] = 0;
    }
    printf("[KernelSim] Desbloqueando processo %d\n", pid);
}

void verifica_terminos() {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < NPROC; i++) {
            if (apps[i] == pid) {
                estado[i] = 2;
                printf("[KernelSim] Processo %d terminou.\n", pid);
                break;
            }
        }
    }
}


int main() {
    mkfifo(FIFO_PATH, 0666);

    // Criação dos processos de aplicação
    for (int i = 0; i < NPROC; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            char prog[10];
            sprintf(prog, "./app%d", i + 1);
            execl(prog, prog, NULL);
            exit(0);
        } else {
            apps[i] = pid;
            kill(apps[i], SIGSTOP);
        }
    }


    current = 0;
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
                case 11:
                    printf("[KernelSim] Processo %d bloqueado em D1.\n", apps[current]);
                    bloqueia_processo(apps[current], 1);
                    escalona_proximo();
                    break;
                case 12:
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
