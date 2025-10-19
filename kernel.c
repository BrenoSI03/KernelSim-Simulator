#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "MsgSyscall.h"
#include "State.h"
#include "Procinfo.h"

#define NPROC 5
#define FIFO_SYSCALL "/tmp/fifo_syscall"
#define FIFO_IRQ     "/tmp/fifo_irq"

ProcInfo proc[NPROC];
pid_t fila_D1[NPROC], fila_D2[NPROC];
int inicio_D1 = 0, fim_D1 = 0;
int inicio_D2 = 0, fim_D2 = 0;

pid_t apps[NPROC];
int atual_idx = 0;
int fd_sys = -1, fd_irq = -1;
int pausado = 0;          // Observar sem o volatile sig_atomic_t
int tratando_sigint = 0; // Observar sem o volatile sig_atomic_t

pid_t intercontroller_pid = -1;
int ultimo_running_idx = -1;

/* Salva o PC do processo em arquivo simples */
static void salvar_contexto_pid(pid_t pid, int pc)
{
    char fname[64];
    snprintf(fname, sizeof(fname), "/tmp/context_%d", pid);
    FILE *f = fopen(fname, "w");
    if (f) {
        fprintf(f, "%d\n", pc);
        fclose(f);
    }
}

/* Encontra índice do que está RUNNING (ou -1) */
static int encontrar_running_idx(void) {
    for (int i = 0; i < NPROC; i++) {
        if (proc[i].estado == RUNNING) {
            return i;
        }
    }
    return -1;
}

/* Acha próximo READY em Round-Robin depois de start_idx (ou -1) */
static int proximo_ready(int start_idx) {
    for (int k = 1; k <= NPROC; k++) {
        int j = (start_idx + k) % NPROC;
        if (proc[j].estado == READY) return j;
    }
    return -1;
}

static int existe_ready(void) {
    for (int i = 0; i < NPROC; i++) {
        if (proc[i].estado == READY) {
            return 1;
        }
    }
    return 0;
}

/* Sobe alguém READY caso ninguém esteja RUNNING */
static void escalona_caso_nao_running(void) {
    if (encontrar_running_idx() != -1) return;  // já tem alguém RUNNING
    int idx = proximo_ready(atual_idx);
    if (idx == -1) return;
    atual_idx = idx;
    printf("[KernelSim] Escalonando (idle) PID=%d (idx=%d)\n", proc[atual_idx].pid, atual_idx);
    proc[atual_idx].estado = RUNNING;
    kill(apps[atual_idx], SIGCONT);
}

/* Aplica RR “literal”: sempre para o atual e tenta subir o próximo */
void escalona_proximo() 
{
    int idx = atual_idx;

    // Se não há ninguém rodando, apenas tenta subir alguém READY
    if (proc[idx].estado != RUNNING) {
        int prox = proximo_ready(idx);
        if (prox != -1) {
            atual_idx = prox;
            proc[atual_idx].estado = RUNNING;
            kill(apps[atual_idx], SIGCONT);
        }
        return;
    }

    // 1) Parar SEMPRE o atual (Round-Robin literal)
    proc[idx].estado = READY;
    salvar_contexto_pid(proc[idx].pid, proc[idx].pc);
    kill(apps[idx], SIGSTOP);

    // 2) Buscar o próximo READY depois do atual
    int prox = proximo_ready(idx);

    if (prox == -1) {
        // 3) Não há outro READY: reative o mesmo imediatamente
        atual_idx = idx;
        proc[atual_idx].estado = RUNNING;
        kill(apps[atual_idx], SIGCONT);
        return;
    }

    // 4) Há outro READY: escale-o
    atual_idx = prox;
    printf("[KernelSim] Escalonando PID=%d (idx=%d)\n", proc[atual_idx].pid, atual_idx);
    proc[atual_idx].estado = RUNNING;
    kill(apps[atual_idx], SIGCONT);
}

void bloqueia_processo(pid_t pid, int dispositivo, char operacao) 
{
    for (int i = 0; i < NPROC; i++) 
    {
        if (proc[i].pid == pid) 
        {
            proc[i].estado = BLOCKED; 
            proc[i].dispositivo = dispositivo;
            proc[i].operacao = operacao;

            if (dispositivo == 1) {
                proc[i].acessos_D1++;
                fila_D1[fim_D1++ % NPROC] = pid;
            } 
            else if (dispositivo == 2) {
                proc[i].acessos_D2++;
                fila_D2[fim_D2++ % NPROC] = pid;
            }

            salvar_contexto_pid(pid, proc[i].pc);
            kill(pid, SIGSTOP);
            printf("[KernelSim] Processo %d bloqueado em D%d.\n", pid, dispositivo);
            break;
        }
    }
}


/* Desbloqueia o primeiro da fila do dispositivo */
void desbloqueia_processo(int dispositivo) 
{
    pid_t pid;

    if (dispositivo == 1 && inicio_D1 < fim_D1)
        pid = fila_D1[inicio_D1++ % NPROC];
    else if (dispositivo == 2 && inicio_D2 < fim_D2)
        pid = fila_D2[inicio_D2++ % NPROC];
    else 
        return;

    for (int i = 0; i < NPROC; i++) 
    {
        if (proc[i].pid == pid) 
        {
            proc[i].estado = READY;
            proc[i].dispositivo = 0;
            proc[i].operacao = '-';

            printf("[KernelSim] Desbloqueando processo %d (D%d concluído)\n", pid, dispositivo);

            escalona_caso_nao_running();
            break;
        }
    }
}

/* Exibe/retoma status com Ctrl+C (pausa/continua) */
void mostra_status(int sig) 
{
    if (tratando_sigint) return; // evita reentrância
    tratando_sigint = 1;


    if (!pausado) 
    {
        pausado = 1;
        ultimo_running_idx = encontrar_running_idx();

        printf("\n=== PAUSANDO SIMULAÇÃO ===\n");

        // Pare todos os processos primeiro
        for (int i = 0; i < NPROC; i++)
        {
            if (proc[i].estado != FINISHED)
                kill(proc[i].pid, SIGSTOP);
        }

        // Pare o InterController em seguida
        if (intercontroller_pid > 0) 
            kill(intercontroller_pid, SIGSTOP);


        // Status
        printf("%-8s %-10s %-6s %-6s %-6s %-10s %-10s\n",
       "PID", "ESTADO", "PC", "D1", "D2", "DISP", "OPERACAO");
        printf("----------------------------------------------------------\n");

        for (int i = 0; i < NPROC; i++) 
        {
            printf("%-8d %-10s %-6d %-6d %-6d %-10d %-10c\n",
                proc[i].pid, state2string(proc[i].estado), proc[i].pc,
                proc[i].acessos_D1, proc[i].acessos_D2,
                proc[i].dispositivo, proc[i].operacao);
        }
        printf("Pressione Ctrl+C novamente para retomar.\n");
    } 
    else 
    {
        pausado = 0;
        printf("\n=== RETOMANDO SIMULAÇÃO ===\n");

        // Retome SOMENTE quem estava RUNNING
        if (ultimo_running_idx >= 0 && proc[ultimo_running_idx].estado != FINISHED) {
            // Fixar o RR no mesmo índice que rodava
            atual_idx = ultimo_running_idx;
            kill(proc[ultimo_running_idx].pid, SIGCONT);
        } else {
            // Se não tinha RUNNING (ex.: todos bloqueados), ligue alguém READY
            escalona_caso_nao_running();
        }

        // Retome o InterController em seguida
        if (intercontroller_pid > 0) {
            kill(intercontroller_pid, SIGCONT);
        }
    }

    tratando_sigint = 0;
}

/* Trata terminações dos apps e sobe outro se necessário */
void verifica_terminos(void) 
{
    int status;
    pid_t pid;
    int terminou_running = 0;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) 
    {
        for (int i = 0; i < NPROC; i++) 
        {
            if (proc[i].pid == pid) 
            {
                int era_running = (proc[i].estado == RUNNING);
                proc[i].estado = FINISHED;
                printf("[KernelSim] Processo %d terminou.\n", pid);
                if (era_running) {
                    terminou_running = 1;
                }
                break;
            }
        }
    }

    if (terminou_running && !pausado) 
    {
        escalona_caso_nao_running();
    }
}

int main(void) 
{
    /* Cria os dois FIFOs (syscall e irq) */
    mkfifo(FIFO_SYSCALL, 0666);
    mkfifo(FIFO_IRQ, 0666);

    signal(SIGINT, mostra_status);
    
    /* Cria os 5 apps e inicia parados */
    for (int i = 0; i < NPROC; i++) 
    {
        pid_t pid = fork();
        if (pid == 0) 
        {
            char prog[10];
            sprintf(prog, "./app%d", i + 1);
            execl(prog, prog, NULL);
            exit(0);
        } 
        else 
        {
            apps[i] = pid;
            proc[i].pid = pid;
            proc[i].estado = READY;
            proc[i].pc = 0;
            proc[i].acessos_D1 = 0;
            proc[i].acessos_D2 = 0;
            proc[i].dispositivo = 0;
            proc[i].operacao = '-';
            kill(apps[i], SIGSTOP);
        }
    }

    sleep(1);
    proc[atual_idx].estado = RUNNING;
    kill(apps[atual_idx], SIGCONT);
    

    /* Abre os dois FIFOs em não-bloqueante para drenar */
    fd_sys = open(FIFO_SYSCALL, O_RDONLY | O_NONBLOCK);
    if (fd_sys < 0) { 
        perror("open FIFO_SYSCALL");
        return 1; 
    }
    fd_irq = open(FIFO_IRQ, O_RDONLY | O_NONBLOCK);
    if (fd_irq < 0) { 
        perror("open FIFO_IRQ");
        return 1; 
    }

    /* Sobe o InterController depois dos FIFOs estarem abertos */  
    intercontroller_pid = fork();
    if (intercontroller_pid == 0) 
    {
        execl("./inter_controller", "./inter_controller", NULL);
        perror("Erro ao iniciar InterControllerSim");
        exit(1);
    }


    while (1) 
    {
        if (pausado) 
        {
            usleep(200000); // espera 200ms enquanto pausado
            continue;
        }
        
        verifica_terminos();

        /* 1) Drenar todas as syscalls pendentes */
        MsgSyscall msg;

        while (read(fd_sys, &msg, sizeof(MsgSyscall)) == sizeof(MsgSyscall)) {
            for (int i = 0; i < NPROC; i++) 
            {
                if (proc[i].pid == msg.pid) 
                {
                    proc[i].pc = msg.pc;
                    break;
                }
            }

            if (msg.tipo == 11) 
            {
                bloqueia_processo(msg.pid, 1, msg.operacao);
                escalona_proximo();
            } 
            else if (msg.tipo == 12) 
            {
                bloqueia_processo(msg.pid, 2, msg.operacao);
                escalona_proximo();
            }
        }

        /* 2) Drenar todos os IRQs pendentes */
        int irq;
        while (read(fd_irq, &irq, sizeof(int)) == sizeof(int)) {
            if (!pausado) {
                switch (irq) {
                    case 0:
                        printf("[KernelSim] IRQ0 recebido: Troca de processo.\n");
                        escalona_proximo();
                    case 1:
                        printf("[KernelSim] IRQ1 recebido: operação em D1 terminou.\n");
                        desbloqueia_processo(1);
                    case 2:
                        printf("[KernelSim] IRQ2 recebido: operação em D2 terminou.\n");
                        desbloqueia_processo(2);
                }
            }

        }

        usleep(10000); // espera 10ms antes do próximo ciclo
    }

    /* Finalização */
    if (intercontroller_pid > 0) {
        kill(intercontroller_pid, SIGTERM);
    }

    close(fd_sys);
    close(fd_irq);
    unlink(FIFO_SYSCALL);
    unlink(FIFO_IRQ);
    system("rm -f /tmp/context_*");
    return 0;
}