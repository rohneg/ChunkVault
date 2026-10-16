#ifndef OS_SIMULATOR_H
#define OS_SIMULATOR_H

#include <string>
#include <vector>

struct Process {
    int pid;
    int arrivalTime;
    int burstTime;
    int priority;
    int completionTime;
    int turnaroundTime;
    int waitingTime;
    std::string state;
};

void runOSSimulator();
void runFCFS();
void runSJF();
void runRoundRobin();
void runDeadlockSimulator();

#endif