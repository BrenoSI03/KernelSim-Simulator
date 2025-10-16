#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#define NPROC 5
#define FIFO_PATH "/tmp/kernel_fifo"

typedef struct {
    pid_t pid;
    int estado;
    int dispositivo;
    char operacao;
    int acessos_D1;
    int acessos_D2;
    int pc;
} ProcInfo;

typedef struct {
    pid_t pid;
    int tipo;
    int pc;
    int acessos_D1;
    int acessos_D2;
    int dispositivo;
    char operacao;
} MsgSyscall;

ProcInfo proc[NPROC];
pid_t fila_D1[NPROC], fila_D2[NPROC];
int inicio_D1 = 0, fim_D1 = 0;
int inicio_D2 = 0, fim_D2 = 0;

pid_t apps[NPROC];
int current = 0;
int fd_fifo;

void escalona_proximo() {
    if (proc[current].estado == 2)
        proc[current].estado = 0;
    kill(apps[current], SIGSTOP);
    int tentativas = 0;
    do {
        current = (current + 1) % NPROC;
        tentativas++;
        if (tentativas > NPROC) return;
    } while (proc[current].estado != 0);
    kill(apps[current], SIGCONT);
    proc[current].estado = 2;
}

void bloqueia_processo(pid_t pid, int dispositivo, char operacao) {
    for (int i = 0; i < NPROC; i++) {
        if (proc[i].pid == pid) {
            proc[i].estado = 1; // BLOQUEADO
            proc[i].dispositivo = dispositivo;
            proc[i].operacao = operacao;

            if (dispositivo == 1)
                proc[i].acessos_D1++;
            else if (dispositivo == 2)
                proc[i].acessos_D2++;

            kill(pid, SIGSTOP);
            printf("[KernelSim] Processo %d bloqueado em D%d.\n", pid, dispositivo);
            break;
        }
    }
}


void desbloqueia_processo(int dispositivo) {
    pid_t pid;
    if (dispositivo == 1 && inicio_D1 < fim_D1) {
        pid = fila_D1[inicio_D1++ % NPROC];
    } else if (dispositivo == 2 && inicio_D2 < fim_D2) {
        pid = fila_D2[inicio_D2++ % NPROC];
    } else return;
    for (int i = 0; i < NPROC; i++) {
        if (proc[i].pid == pid) {
            proc[i].estado = 0;
            proc[i].dispositivo = 0;
            proc[i].operacao = '-';
            printf("[KernelSim] Desbloqueando processo %d\n", pid);
            break;
        }
    }
}

void mostra_status(int sig) {
    printf("\n==================== ESTADO DOS PROCESSOS ====================\n");
    for (int i = 0; i < NPROC; i++) {
        printf("PID %d | ", proc[i].pid);
        switch (proc[i].estado) {
            case 0: printf("PRONTO"); break;
            case 1: printf("BLOQUEADO"); break;
            case 2: printf("EXECUTANDO"); break;
            case 3: printf("TERMINADO"); break;
        }
        printf(" | PC=%d | D1=%d | D2=%d",
               proc[i].pc, proc[i].acessos_D1, proc[i].acessos_D2);
        if (proc[i].estado == 1)
            printf(" | Esperando D%d (%c)", proc[i].dispositivo, proc[i].operacao);
        printf("\n");
    }
    printf("==============================================================\n");
    close(fd_fifo);
    unlink(FIFO_PATH);
    printf("\n[KernelSim] Encerrando simulador por Ctrl+C...\n");
    for (int i = 0; i < NPROC; i++) {
        if (proc[i].estado != 3) {
            kill(proc[i].pid, SIGTERM);
        }
    }
    exit(0);
}

void verifica_terminos() {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < NPROC; i++) {
            if (proc[i].pid == pid) {
                proc[i].estado = 3;
                printf("[KernelSim] Processo %d terminou.\n", pid);
                break;
            }
        }
    }
}

int main() {
    mkfifo(FIFO_PATH, 0666);
    signal(SIGINT, mostra_status);
    for (int i = 0; i < NPROC; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            char prog[10];
            sprintf(prog, "./app%d", i + 1);
            execl(prog, prog, NULL);
            exit(0);
        } else {
            apps[i] = pid;
            proc[i].pid = pid;
            proc[i].estado = 0;
            proc[i].pc = 0;
            proc[i].acessos_D1 = 0;
            proc[i].acessos_D2 = 0;
            proc[i].dispositivo = 0;
            proc[i].operacao = '-';
            kill(apps[i], SIGSTOP);
        }
    }
    sleep(1);
    kill(apps[current], SIGCONT);
    proc[current].estado = 2;
    fd_fifo = open(FIFO_PATH, O_RDONLY);
    int irq;
    MsgSyscall msg;
    while (1) {
        verifica_terminos();
        ssize_t bytes = read(fd_fifo, &msg, sizeof(MsgSyscall));
        if (bytes == sizeof(MsgSyscall)) {
            for (int i = 0; i < NPROC; i++) {
                if (proc[i].pid == msg.pid) {
                    proc[i].pc = msg.pc;
                    proc[i].acessos_D1 = msg.acessos_D1;
                    proc[i].acessos_D2 = msg.acessos_D2;
                    break;
                }
            }
            if (msg.tipo == 11) {
                printf("[KernelSim] Processo %d bloqueado em D1.\n", msg.pid);
                bloqueia_processo(msg.pid, 1, msg.operacao);
                escalona_proximo();
            } else if (msg.tipo == 12) {
                printf("[KernelSim] Processo %d bloqueado em D2.\n", msg.pid);
                bloqueia_processo(msg.pid, 2, msg.operacao);
                escalona_proximo();
            }
        }
        else if (bytes == sizeof(int)) {
            int irq = *((int *)&msg);
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
            }
        }
    }
    close(fd_fifo);
    unlink(FIFO_PATH);
    return 0;
}