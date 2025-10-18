#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define TIME_SLICE_US 500000  // 500 ms
#define FIFO_PATH "/tmp/kernel_fifo"

int main() {
    srand(time(NULL));

    int fd = open(FIFO_PATH, O_WRONLY);
    if (fd < 0) {
        perror("[InterControllerSim] Erro ao abrir FIFO");
        exit(1);
    }

    printf("[InterControllerSim] Iniciado...\n");

    while (1) {
        usleep(TIME_SLICE_US);

        int irq = 0;
        write(fd, &irq, sizeof(int));
        printf("[InterControllerSim] IRQ0 (TimeSlice)\n");

        if ((rand() % 1000) < 100) {   // IRQ1 com P_1 = 1
            irq = 1;
            write(fd, &irq, sizeof(int));
            printf("[InterControllerSim] IRQ1 (D1 terminado)\n");
        }
        if ((rand() % 1000) < 5) {    // IRQ2 com P_2 = 0.05 (20x menor)
            irq = 2;
            write(fd, &irq, sizeof(int));
            printf("[InterControllerSim] IRQ2 (D2 terminado)\n");
        }


        fflush(stdout);
    }

    close(fd);
    return 0;
}
