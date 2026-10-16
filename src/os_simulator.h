#ifndef OS_SIMULATOR_H
#define OS_SIMULATOR_H

#include <string>
#include <vector>

struct Process {
    int pid;
    int arrivalTime;
    int burstTime;
    int completionTime = 0;
    int turnaroundTime = 0;
    int waitingTime = 0;
};

void runOSSimulator();

#endif