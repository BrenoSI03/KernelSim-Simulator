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
int fd_fifo;
int paused = 0;
pid_t intercontroller_pid = -1;


void escalona_proximo() 
{
    if (proc[current].estado == RUNNING)
        proc[current].estado = READY;
    
    kill(apps[current], SIGSTOP);
    int tentativas = 0;
    
    while (proc[current].estado != READY)
    {
        current = (current + 1) % NPROC;
        tentativas++;
        if (tentativas > NPROC) return;
    }
    
    kill(apps[current], SIGCONT);
    proc[current].estado = RUNNING;
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

            kill(pid, SIGCONT);
            break;
        }
    }
}

void mostra_status(int sig) 
{
    if (!paused) 
    {
        paused = 1;

        printf("\n=== PAUSANDO SIMULAÇÃO ===\n");
        for (int i = 0; i < NPROC; i++)
        {
            if (proc[i].estado != FINISHED)
                kill(proc[i].pid, SIGSTOP);
        }
        if (intercontroller_pid > 0) 
            kill(intercontroller_pid, SIGSTOP);

        for (int i = 0; i < NPROC; i++) 
        {
            printf("PID %d | Estado %s | PC=%d | D1=%d | D2=%d\n",
                proc[i].pid, state2string(proc[i].estado), proc[i].pc, proc[i].acessos_D1, proc[i].acessos_D2);
        }
        printf("Pressione Ctrl+C novamente para retomar.\n");
    } 
    else 
    {
        paused = 0;

        printf("\n=== RETOMANDO SIMULAÇÃO ===\n");
        for (int i = 0; i < NPROC; i++)
        {
            if (proc[i].estado != FINISHED) 
                kill(proc[i].pid, SIGCONT);
        }

        if (intercontroller_pid > 0) 
            kill(intercontroller_pid, SIGCONT);
    }
}


void verifica_terminos() 
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) 
    {
        for (int i = 0; i < NPROC; i++) 
        {
            if (proc[i].pid == pid) 
            {
                proc[i].estado = FINISHED;
                printf("[KernelSim] Processo %d terminou.\n", pid);
                break;
            }
        }
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
    
    kill(apps[current], SIGCONT);
    proc[current].estado = RUNNING;
    fd_fifo = open(FIFO_PATH, O_RDONLY);
    
    int irq;
    MsgSyscall msg;
    
    intercontroller_pid = fork();
    if (intercontroller_pid == 0) 
    {
        execl("./inter_controller", "./inter_controller", NULL);
        perror("Erro ao iniciar InterControllerSim");
        exit(1);
    }

    while (1) 
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
                bloqueia_processo(msg.pid, 1, msg.operacao);
                escalona_proximo();
            } 
            else if (msg.tipo == 12) 
            {
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
    }
    
    close(fd_fifo);
    unlink(FIFO_PATH);
    return 0;
}