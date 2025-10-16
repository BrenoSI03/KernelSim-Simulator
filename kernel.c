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

// -------------------------
// Estrutura de controle de processo
// -------------------------
typedef struct {
    pid_t pid;
    int estado;            // 0=pronto, 1=bloqueado, 2=executando, 3=terminado
    int dispositivo;       // 1 ou 2 (se bloqueado)
    char operacao;         // 'R', 'W', 'X'
    int acessos_D1;
    int acessos_D2;
    int pc;                // contador de programa (valor aproximado)
} ProcInfo;

typedef struct {
    pid_t pid;
    int tipo;         // 11 ou 12 (syscall), ou 0/1/2 (IRQ)
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

// -------------------------
// Escalonador Round-Robin
// -------------------------
void escalona_proximo() {
    // Marca o atual como "pronto" (só se não estiver bloqueado/terminado)
    if (proc[current].estado == 2)
        proc[current].estado = 0;

    // Pausa o atual
    kill(apps[current], SIGSTOP);

    // Procura o próximo processo pronto
    int tentativas = 0;
    do {
        current = (current + 1) % NPROC;
        tentativas++;
        if (tentativas > NPROC) return; // todos bloqueados ou terminados
    } while (proc[current].estado != 0);

    // Retoma o processo selecionado
    kill(apps[current], SIGCONT);
    proc[current].estado = 2; // executando
}

// -------------------------
// Gerenciamento de bloqueio/desbloqueio
// -------------------------
void bloqueia_processo(pid_t pid, int dispositivo, char op) {
    for (int i = 0; i < NPROC; i++) {
        if (proc[i].pid == pid) {
            proc[i].estado = 1;
            proc[i].dispositivo = dispositivo;
            proc[i].operacao = op;
            if (dispositivo == 1)
                fila_D1[fim_D1++ % NPROC] = pid;
            else
                fila_D2[fim_D2++ % NPROC] = pid;
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

    // Marca como pronto
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

// -------------------------
// Captura do estado (SIGINT)
// -------------------------
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

    // Fecha FIFO e remove arquivo temporário
    close(fd_fifo);
    unlink(FIFO_PATH);

    printf("\n[KernelSim] Encerrando simulador por Ctrl+C...\n");

    // Envia SIGTERM para todos os processos de aplicação ainda vivos
    for (int i = 0; i < NPROC; i++) {
        if (proc[i].estado != 3) { // se não terminou
            kill(proc[i].pid, SIGTERM);
        }
    }

    exit(0); // encerra o KernelSim
}


// -------------------------
// Verifica se algum app terminou
// -------------------------
void verifica_terminos() {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < NPROC; i++) {
            if (proc[i].pid == pid) {
                proc[i].estado = 3; // terminado
                printf("[KernelSim] Processo %d terminou.\n", pid);
                break;
            }
        }
    }
}

// -------------------------
// Programa principal
// -------------------------
int main() {
    mkfifo(FIFO_PATH, 0666);
    signal(SIGINT, mostra_status);

    // Cria processos de aplicação
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
            proc[i].estado = 0;  // pronto inicialmente
            proc[i].pc = 0;
            proc[i].acessos_D1 = 0;
            proc[i].acessos_D2 = 0;
            proc[i].dispositivo = 0;
            proc[i].operacao = '-';
            kill(apps[i], SIGSTOP); // pausa todos
        }
    }

    // Inicia o primeiro processo
    sleep(1);
    kill(apps[current], SIGCONT);
    proc[current].estado = 2;

    // Abre FIFO para leitura
    fd_fifo = open(FIFO_PATH, O_RDONLY);
    int irq;

    MsgSyscall msg;

    while (1) {
        verifica_terminos();

        // lê estrutura completa
        ssize_t bytes = read(fd_fifo, &msg, sizeof(MsgSyscall));

        if (bytes == sizeof(MsgSyscall)) {
            // atualização de estado vinda de app
            for (int i = 0; i < NPROC; i++) {
                if (proc[i].pid == msg.pid) {
                    proc[i].pc = msg.pc;
                    proc[i].acessos_D1 = msg.acessos_D1;
                    proc[i].acessos_D2 = msg.acessos_D2;
                    break;
                }
            }

            if (msg.tipo == 11) { // D1 syscall
                printf("[KernelSim] Processo %d bloqueado em D1.\n", msg.pid);
                bloqueia_processo(msg.pid, 1, msg.operacao);
                escalona_proximo();
            } else if (msg.tipo == 12) { // D2 syscall
                printf("[KernelSim] Processo %d bloqueado em D2.\n", msg.pid);
                bloqueia_processo(msg.pid, 2, msg.operacao);
                escalona_proximo();
            }
        }
        else if (bytes == sizeof(int)) {
            // mensagens do InterController (IRQ0/1/2)
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
