#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>

#define FIFO_PATH "/tmp/kernel_fifo"
#define MAX 10

typedef struct {
    pid_t pid;
    int tipo;         // 11 ou 12
    int pc;
    int acessos_D1;
    int acessos_D2;
    int dispositivo;
    char operacao;
} MsgSyscall;

int pc = 0;
int acessos_D1 = 0, acessos_D2 = 0;
int continuar = 1;

void sigcont_handler(int sig) {
    FILE *f;
    char fname[50];
    sprintf(fname, "/tmp/context_%d", getpid());
    f = fopen(fname, "r");
    if (f) {
        fscanf(f, "%d", &pc);
        fclose(f);
    }
}

void salvar_contexto() {
    FILE *f;
    char fname[50];
    sprintf(fname, "/tmp/context_%d", getpid());
    f = fopen(fname, "w");
    fprintf(f, "%d\n", pc);
    fclose(f);
}


void sys_call(int dispositivo) {
    int fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    if (fd < 0) return;

    if (dispositivo == 1) acessos_D1++;
    else acessos_D2++;

    MsgSyscall msg;
    msg.pid = getpid();
    msg.tipo = 10 + dispositivo; // 11 ou 12
    msg.pc = pc;
    msg.acessos_D1 = acessos_D1;
    msg.acessos_D2 = acessos_D2;
    msg.dispositivo = dispositivo;

    int op = rand() % 3;
    msg.operacao = (op == 0) ? 'R' : (op == 1) ? 'W' : 'X';

    write(fd, &msg, sizeof(msg));
    close(fd);

    printf("[App %d] -> Bloqueando por D%d (Operação %c, PC=%d)\n",
           msg.pid, msg.dispositivo, msg.operacao, msg.pc);

    kill(getpid(), SIGSTOP);
}

int main() {
    srand(time(NULL) ^ getpid());
    printf("[App %d] iniciado.\n", getpid());

    signal(SIGCONT, sigcont_handler);
    while (pc < MAX) {
        salvar_contexto();
        sleep(1);
        if (rand() % 100 < 15) {
            int dispositivo = (rand() % 2) + 1;
            sys_call(dispositivo);
        }
        pc++;
        salvar_contexto();
    }

    printf("[App %d] terminou. PC final=%d | D1=%d | D2=%d\n",
           getpid(), pc, acessos_D1, acessos_D2);
    salvar_contexto();
    return 0;
}
