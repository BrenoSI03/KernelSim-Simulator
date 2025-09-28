#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>

#define NPROC 5
#define TIMESLICE 1

pid_t apps[NPROC];
int current = 0;

void escalona_proximo() {
    kill(apps[current], SIGSTOP);
    current = (current + 1) % NPROC;
    kill(apps[current], SIGCONT);
}

int main() {
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

    while (1) {
        sleep(TIMESLICE);
        escalona_proximo();
    }
    return 0;
}
