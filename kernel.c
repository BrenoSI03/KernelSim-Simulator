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
#define READY    0
#define BLOCKED  1
#define RUNNING  2
#define FINISHED 3


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
static volatile sig_atomic_t handling_sigint = 0;

pid_t apps[NPROC];
int current = 0;
int fd_fifo;
volatile sig_atomic_t paused = 0;
pid_t intercontroller_pid = -1;
int last_running_idx = -1;

static int ler_pc_do_contexto(pid_t pid, int *out_pc) {
    char fname[64];
    snprintf(fname, sizeof(fname), "/tmp/context_%d", pid);
    FILE *f = fopen(fname, "r");
    if (!f) return 0;
    int tmp;
    int ok = (fscanf(f, "%d", &tmp) == 1);
    fclose(f);
    if (ok) { *out_pc = tmp; return 1; }
    return 0;
}

static int find_running_index(void) {
    for (int i = 0; i < NPROC; i++) if (proc[i].estado == RUNNING) return i;
    return -1;
}

static int find_next_ready_index_from(int start) {
    for (int k = 1; k <= NPROC; k++) {
        int idx = (start + k) % NPROC;
        if (proc[idx].estado == READY) return idx;
    }
    return -1;
}

static int exists_ready(void) {
    for (int i = 0; i < NPROC; i++) if (proc[i].estado == READY) return 1;
    return 0;
}

/* Sobe alguém se ninguém estiver rodando */
static void schedule_if_idle(void) {
    if (find_running_index() != -1) return;  // já tem alguém RUNNING
    int idx = find_next_ready_index_from(current);
    if (idx == -1) return;
    current = idx;
    printf("[KernelSim] Escalonando (idle) PID=%d (idx=%d)\n", proc[current].pid, current);
    proc[current].estado = RUNNING;
    kill(apps[current], SIGCONT);
}


void escalona_proximo() {
    int next = find_next_ready_index_from(current);
    if (next == -1) {
        // Ninguém pronto: não pare o atual se ele ainda está RUNNING
        if (proc[current].estado != RUNNING) {
            // Nada rodando: fica aguardando até alguém desbloquear
        }
        return;
    }

    // Existe um candidato READY. Se o atual está RUNNING, pare-o.
    if (proc[current].estado == RUNNING) {
        proc[current].estado = READY;
        kill(apps[current], SIGSTOP);
    }

    current = next;
    printf("[KernelSim] Escalonando PID=%d (idx=%d)\n", proc[current].pid, current);
    proc[current].estado = RUNNING;
    kill(apps[current], SIGCONT);
}


void bloqueia_processo(pid_t pid, int dispositivo, char operacao) {
    for (int i = 0; i < NPROC; i++) {
        if (proc[i].pid == pid) {
            proc[i].estado = BLOCKED;
            proc[i].dispositivo = dispositivo;
            proc[i].operacao = operacao;

            // Contabiliza uma vez por syscall:
            if (dispositivo == 1) {
                proc[i].acessos_D1++;
                fila_D1[fim_D1++ % NPROC] = pid;
            } else if (dispositivo == 2) {
                proc[i].acessos_D2++;
                fila_D2[fim_D2++ % NPROC] = pid;
            }

            kill(pid, SIGSTOP);
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
            proc[i].estado = READY;
            proc[i].dispositivo = 0;
            proc[i].operacao = '-';

            printf("[KernelSim] Liberado de D%d -> PID %d marcado READY\n", dispositivo, pid);

            // Não damos SIGCONT aqui! Apenas sobe alguém se não houver RUNNING.
            schedule_if_idle();
            break;
        }
    }
}


void mostra_status(int sig) {
    if (handling_sigint) return;
    handling_sigint = 1;

    if (!paused) {
        paused = 1;
        last_running_idx = find_running_index();

        printf("\n=== PAUSANDO SIMULAÇÃO ===\n");

        // Pare todo mundo primeiro
        for (int i = 0; i < NPROC; i++)
            if (proc[i].estado != FINISHED) {
                kill(proc[i].pid, SIGSTOP);
            }

        // Pare o InterController
        if (intercontroller_pid > 0) kill(intercontroller_pid, SIGSTOP);

        // Atualize PC "de verdade" lendo /tmp/context_<pid>
        for (int i = 0; i < NPROC; i++) {
            int pc_atual;
            if (ler_pc_do_contexto(proc[i].pid, &pc_atual)) {
                proc[i].pc = pc_atual;
            }
        }

        // Status
        for (int i = 0; i < NPROC; i++) {
            printf("PID %d | Estado %d | PC=%d | D1=%d | D2=%d | Disp=%d | Op=%c\n",
                   proc[i].pid, proc[i].estado, proc[i].pc,
                   proc[i].acessos_D1, proc[i].acessos_D2,
                   proc[i].dispositivo, proc[i].operacao);
        }

        printf("Pressione Ctrl+C novamente para retomar.\n");        
    } else {
        paused = 0;
        printf("\n=== RETOMANDO SIMULAÇÃO ===\n");

        // Retome SOMENTE quem estava RUNNING
        if (last_running_idx >= 0 && proc[last_running_idx].estado != FINISHED) {
            // Fixar o RR no mesmo índice que rodava
            current = last_running_idx;
            kill(proc[last_running_idx].pid, SIGCONT);
        } else {
            // Se não tinha RUNNING (ex.: todos bloqueados), ligue alguém READY
            schedule_if_idle();
        }

        // Retome InterController
        if (intercontroller_pid > 0) kill(intercontroller_pid, SIGCONT);
    }

    handling_sigint = 0;
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
    proc[current].estado = RUNNING;
    fd_fifo = open(FIFO_PATH, O_RDONLY);
    int irq;
    MsgSyscall msg;
    intercontroller_pid = fork();
    if (intercontroller_pid == 0) {
        execl("./inter_controller", "./inter_controller", NULL);
        perror("Erro ao iniciar InterControllerSim");
        exit(1);
    }

    while (1) {
        if (paused) {
            usleep(200000); // espera 200ms enquanto pausado
            continue;
        }

        verifica_terminos();
        ssize_t bytes = read(fd_fifo, &msg, sizeof(MsgSyscall));
        if (bytes == sizeof(MsgSyscall)) {
            for (int i = 0; i < NPROC; i++) {
                if (proc[i].pid == msg.pid) {
                    proc[i].pc = msg.pc;
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

            if (!paused) {
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
    }
    close(fd_fifo);
    unlink(FIFO_PATH);
    return 0;
}