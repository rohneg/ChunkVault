#include "os_simulator.h"
#include <iostream>
#include <algorithm>
#include <iomanip>

void runFCFS() {
    int n;

    std::cout << "\nEnter number of processes: ";
    if (!(std::cin >> n) || n <= 0) {
        std::cout << "Invalid number of processes.\n";
        std::cin.clear();
        std::cin.ignore(10000, '\n');
        return;
    }

    std::vector<Process> processes(n);

    for (int i = 0; i < n; i++) {
        processes[i].pid = i + 1;

        std::cout << "Arrival time for P" << i + 1 << ": ";
        if (!(std::cin >> processes[i].arrivalTime) || processes[i].arrivalTime < 0) {
            std::cout << "Invalid arrival time.\n";
            return;
        }

        std::cout << "Burst time for P" << i + 1 << ": ";
        if (!(std::cin >> processes[i].burstTime) || processes[i].burstTime <= 0) {
            std::cout << "Invalid burst time.\n";
            return;
        }
    }

    std::sort(processes.begin(), processes.end(),
        [](const Process& a, const Process& b) {
            if (a.arrivalTime != b.arrivalTime)
                return a.arrivalTime < b.arrivalTime;
            return a.pid < b.pid;
        });

    int currentTime = 0;
    double totalWaiting = 0;
    double totalTurnaround = 0;

    std::cout << "\nGantt Chart:\n";

    for (auto& p : processes) {
        if (currentTime < p.arrivalTime) {
            std::cout << "| Idle ";
            currentTime = p.arrivalTime;
        }

        std::cout << "| P" << p.pid << " ";

        currentTime += p.burstTime;
        p.completionTime = currentTime;
        p.turnaroundTime = p.completionTime - p.arrivalTime;
        p.waitingTime = p.turnaroundTime - p.burstTime;

        totalWaiting += p.waitingTime;
        totalTurnaround += p.turnaroundTime;
    }

    std::cout << "|\n\n";

    std::cout << std::left
              << std::setw(10) << "Process"
              << std::setw(12) << "Arrival"
              << std::setw(10) << "Burst"
              << std::setw(12) << "Completion"
              << std::setw(12) << "Turnaround"
              << "Waiting\n";

    for (const auto& p : processes) {
        std::cout << std::left
                  << std::setw(10) << ("P" + std::to_string(p.pid))
                  << std::setw(12) << p.arrivalTime
                  << std::setw(10) << p.burstTime
                  << std::setw(12) << p.completionTime
                  << std::setw(12) << p.turnaroundTime
                  << p.waitingTime << '\n';
    }

    std::cout << "\nAverage waiting time: " << totalWaiting / n << '\n';
    std::cout << "Average turnaround time: " << totalTurnaround / n << '\n';
}

void runOSSimulator() {
    int choice;

    do {
        std::cout << "\n===== OS Simulator =====\n";
        std::cout << "1. FCFS Scheduling\n";
        std::cout << "0. Back\n";
        std::cout << "Enter choice: ";

        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::cin.ignore(10000, '\n');
            std::cout << "Invalid choice.\n";
            continue;
        }

        switch (choice) {
            case 1:
                runFCFS();
                break;
            case 0:
                break;
            default:
                std::cout << "Invalid choice.\n";
        }
    } while (choice != 0);
}