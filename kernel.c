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
#define FIFO_PATH "/tmp/kernel_fifo"

ProcInfo proc[NPROC];
pid_t fila_D1[NPROC], fila_D2[NPROC];
int inicio_D1 = 0, fim_D1 = 0;
int inicio_D2 = 0, fim_D2 = 0;

pid_t apps[NPROC];
int current = 0;
int fd_fifo = -1;
int paused = 0;          // Observar sem o volatile sig_atomic_t
int tratando_sigint = 0; // Observar sem o volatile sig_atomic_t

pid_t intercontroller_pid = -1;
int ultimo_running_idx = -1;

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

static int encontrar_running_idx(void) {
    for (int i = 0; i < NPROC; i++) {
        if (proc[i].estado == RUNNING) {
            return i;
        }
    }
    return -1;
}

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

/* Sobe alguém se ninguém estiver rodando */
static void escalona_caso_nao_running(void) {
    if (encontrar_running_idx() != -1) return;  // já tem alguém RUNNING
    int idx = proximo_ready(current);
    if (idx == -1) return;
    current = idx;
    printf("[KernelSim] Escalonando (idle) PID=%d (idx=%d)\n", proc[current].pid, current);
    proc[current].estado = RUNNING;
    kill(apps[current], SIGCONT);
}


void escalona_proximo() 
{
    int atual = current;

    // Se não há ninguém rodando, apenas tenta subir alguém READY
    if (proc[atual].estado != RUNNING) {
        int idx = proximo_ready(atual);
        if (idx != -1) {
            current = idx;
            proc[current].estado = RUNNING;
            kill(apps[current], SIGCONT);
        }
        return;
    }

    // 1) Parar SEMPRE o atual (Round-Robin literal)
    proc[atual].estado = READY;
    salvar_contexto_pid(proc[atual].pid, proc[atual].pc);
    kill(apps[atual], SIGSTOP);

    // 2) Buscar o próximo READY depois do atual
    int prox = proximo_ready(atual);

    if (prox == -1) {
        // 3) Não há outro READY: reative o mesmo imediatamente
        current = atual;
        proc[current].estado = RUNNING;
        kill(apps[current], SIGCONT);
        return;
    }

    // 4) Há outro READY: escale-o
    current = prox;
    printf("[KernelSim] Escalonando PID=%d (idx=%d)\n", proc[current].pid, current);
    proc[current].estado = RUNNING;
    kill(apps[current], SIGCONT);
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


void desbloqueia_processo(int dispositivo) 
{
    pid_t pid;

    if (dispositivo == 1 && inicio_D1 < fim_D1)
        pid = fila_D1[inicio_D1++ % NPROC];
    else if (dispositivo == 2 && inicio_D2 < fim_D2)
        pid = fila_D2[inicio_D2++ % NPROC];
    else return;

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


void mostra_status(int sig) 
{
    if (tratando_sigint) return; // evita reentrância
    tratando_sigint = 1;


    if (!paused) 
    {
        paused = 1;
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

        
        // close(fd_fifo); // Pare o FIFO

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
        paused = 0;
        printf("\n=== RETOMANDO SIMULAÇÃO ===\n");

        // Retome SOMENTE quem estava RUNNING
        if (ultimo_running_idx >= 0 && proc[ultimo_running_idx].estado != FINISHED) {
            // Fixar o RR no mesmo índice que rodava
            current = ultimo_running_idx;
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


void verifica_terminos() 
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

    if (terminou_running && !paused) 
    {
        escalona_caso_nao_running();
    }
}

int main() 
{
    mkfifo(FIFO_PATH, 0666);
    signal(SIGINT, mostra_status);
    
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
    proc[current].estado = RUNNING;
    kill(apps[current], SIGCONT);
    

    fd_fifo = open(FIFO_PATH, O_RDONLY);
    if (fd_fifo < 0) {
        perror("open FIFO");
        return 1;
    }
    
    int irq;
    MsgSyscall msg;
    
    intercontroller_pid = fork();
    if (intercontroller_pid == 0) 
    {
        execl("./inter_controller", "./inter_controller", NULL);
        perror("Erro ao iniciar InterControllerSim");
        exit(1);
    }


    int hasProcessAlive = 1;
    while (hasProcessAlive) 
    {
        if (paused) 
        {
            usleep(200000); // espera 200ms enquanto pausado
            continue;
        }
        
        verifica_terminos();
        ssize_t bytes = read(fd_fifo, &msg, sizeof(MsgSyscall));
        
        if (bytes == sizeof(MsgSyscall)) 
        {
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
                printf("[KernelSim] Processo %d bloqueado em D1.\n", msg.pid);
                bloqueia_processo(msg.pid, 1, msg.operacao);
                escalona_proximo();
            } 
            else if (msg.tipo == 12) 
            {
                printf("[KernelSim] Processo %d bloqueado em D2.\n", msg.pid);
                bloqueia_processo(msg.pid, 2, msg.operacao);
                escalona_proximo();
            }
        }
        else if (bytes == sizeof(int)) 
        {
            int irq = *((int *)&msg);
            
            if (!paused) 
            {
                switch (irq) 
                {
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

        hasProcessAlive = 0;
        for(int i = 0; i < NPROC; i++)
        {
            if (proc[i].estado != FINISHED){
                hasProcessAlive = 1;
                break;
            }
        }
    }

    kill(intercontroller_pid, SIGUSR1);
    close(fd_fifo);
    unlink(FIFO_PATH);
    system("rm -f /tmp/context_*");
    return 0;
}