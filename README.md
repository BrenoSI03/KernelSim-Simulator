# KernelSim-Simulator
Simulador de kernel em C que gerencia 5 processos via fork/exec, sinais e IPC no Linux. Utiliza escalonamento preemptivo (Round Robin) e simula E/S com dois dispositivos (D1 e D2). Um processo extra emula o controlador de interrupções (IRQ0: time slice, IRQ1/IRQ2: fim de E/S).
