#include "os_simulator.h"
#include <iostream>
#include <algorithm>
#include <iomanip>
#include <deque>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

bool calculateSchedule(const std::vector<Process>& processes,
                       const std::string& algorithm,
                       int quantum,
                       ScheduleResult& result,
                       std::string& error) {
    if (processes.empty()) {
        error = "Create at least one process before running the scheduler.";
        return false;
    }
    if (processes.size() > 256) {
        error = "The scheduler accepts up to 256 processes per run.";
        return false;
    }
    if (algorithm != "FCFS" && algorithm != "SJF" && algorithm != "RR") {
        error = "Choose FCFS, SJF or Round Robin.";
        return false;
    }
    if (algorithm == "RR" && quantum <= 0) {
        error = "Round Robin requires a positive time quantum.";
        return false;
    }

    std::unordered_set<int> pids;
    long long totalBurst = 0;
    int latestArrival = 0;
    std::vector<Process> scheduled = processes;
    for (auto& process : scheduled) {
        if (process.pid <= 0 || !pids.insert(process.pid).second) {
            error = "Process IDs must be unique positive numbers.";
            return false;
        }
        if (process.arrivalTime < 0 || process.burstTime <= 0) {
            error = "Arrival times must be non-negative and burst times must be positive.";
            return false;
        }
        totalBurst += process.burstTime;
        latestArrival = std::max(latestArrival, process.arrivalTime);
        process.completionTime = 0;
        process.turnaroundTime = 0;
        process.waitingTime = 0;
    }
    if (totalBurst + latestArrival > std::numeric_limits<int>::max()) {
        error = "The combined arrival and burst times are too large to schedule.";
        return false;
    }

    ScheduleResult output;
    output.algorithm = algorithm;
    output.processes = scheduled;
    int time = 0;

    if (algorithm == "RR") {
        std::vector<size_t> arrivalOrder(scheduled.size());
        for (size_t i = 0; i < arrivalOrder.size(); ++i) arrivalOrder[i] = i;
        std::sort(arrivalOrder.begin(), arrivalOrder.end(), [&scheduled](size_t left, size_t right) {
            if (scheduled[left].arrivalTime != scheduled[right].arrivalTime) {
                return scheduled[left].arrivalTime < scheduled[right].arrivalTime;
            }
            return scheduled[left].pid < scheduled[right].pid;
        });

        std::vector<int> remaining(scheduled.size());
        for (size_t i = 0; i < scheduled.size(); ++i) remaining[i] = scheduled[i].burstTime;
        std::deque<size_t> ready;
        size_t nextArrival = 0;
        while (!ready.empty() || nextArrival < arrivalOrder.size()) {
            while (nextArrival < arrivalOrder.size() &&
                   scheduled[arrivalOrder[nextArrival]].arrivalTime <= time) {
                ready.push_back(arrivalOrder[nextArrival++]);
            }
            if (ready.empty()) {
                int nextTime = scheduled[arrivalOrder[nextArrival]].arrivalTime;
                if (nextTime > time) output.gantt.push_back({0, time, nextTime, true});
                time = nextTime;
                continue;
            }

            size_t index = ready.front();
            ready.pop_front();
            int start = time;
            int duration = std::min(quantum, remaining[index]);
            time += duration;
            remaining[index] -= duration;
            output.gantt.push_back({scheduled[index].pid, start, time, false});

            while (nextArrival < arrivalOrder.size() &&
                   scheduled[arrivalOrder[nextArrival]].arrivalTime <= time) {
                ready.push_back(arrivalOrder[nextArrival++]);
            }
            if (remaining[index] > 0) {
                ready.push_back(index);
            } else {
                scheduled[index].completionTime = time;
                scheduled[index].turnaroundTime = time - scheduled[index].arrivalTime;
                scheduled[index].waitingTime = scheduled[index].turnaroundTime - scheduled[index].burstTime;
            }
        }
        output.processes = scheduled;
    } else {
        std::vector<size_t> ready(scheduled.size());
        for (size_t i = 0; i < ready.size(); ++i) ready[i] = i;
        while (!ready.empty()) {
            size_t selected = scheduled.size();
            for (size_t index : ready) {
                if (scheduled[index].arrivalTime > time) continue;
                if (selected == scheduled.size()) {
                    selected = index;
                    continue;
                }
                const Process& candidate = scheduled[index];
                const Process& current = scheduled[selected];
                bool earlier = algorithm == "FCFS"
                    ? candidate.arrivalTime < current.arrivalTime ||
                      (candidate.arrivalTime == current.arrivalTime && candidate.pid < current.pid)
                    : candidate.burstTime < current.burstTime ||
                      (candidate.burstTime == current.burstTime &&
                       (candidate.arrivalTime < current.arrivalTime ||
                        (candidate.arrivalTime == current.arrivalTime && candidate.pid < current.pid)));
                if (earlier) selected = index;
            }
            if (selected == scheduled.size()) {
                int nextTime = std::numeric_limits<int>::max();
                for (size_t index : ready) nextTime = std::min(nextTime, scheduled[index].arrivalTime);
                if (nextTime > time) output.gantt.push_back({0, time, nextTime, true});
                time = nextTime;
                continue;
            }

            Process& process = scheduled[selected];
            int start = time;
            time += process.burstTime;
            process.completionTime = time;
            process.turnaroundTime = time - process.arrivalTime;
            process.waitingTime = process.turnaroundTime - process.burstTime;
            output.gantt.push_back({process.pid, start, time, false});
            ready.erase(std::find(ready.begin(), ready.end(), selected));
        }
        output.processes = scheduled;
    }

    long long totalWaiting = 0;
    long long totalTurnaround = 0;
    for (const auto& process : output.processes) {
        totalWaiting += process.waitingTime;
        totalTurnaround += process.turnaroundTime;
    }
    std::sort(output.processes.begin(), output.processes.end(), [](const Process& left, const Process& right) {
        return left.pid < right.pid;
    });
    output.totalTime = time;
    output.averageWaiting = static_cast<double>(totalWaiting) / output.processes.size();
    output.averageTurnaround = static_cast<double>(totalTurnaround) / output.processes.size();
    output.cpuUtilization = time > 0 ? static_cast<double>(totalBurst) * 100.0 / time : 0;
    output.throughput = time > 0 ? static_cast<double>(output.processes.size()) / time : 0;
    result = std::move(output);
    error.clear();
    return true;
}

std::vector<int> detectDeadlockCycle(const std::vector<int>& processIds,
                                     const std::vector<OSResource>& resources,
                                     const std::vector<OSResourceRequest>& requests) {
    std::unordered_map<int, std::vector<int>> graph;
    for (int pid : processIds) graph.emplace(pid, std::vector<int>{});
    std::unordered_map<int, int> owners;
    for (const auto& resource : resources) owners[resource.id] = resource.ownerPid;
    for (const auto& request : requests) {
        auto owner = owners.find(request.resourceId);
        if (owner != owners.end() && owner->second != 0 && graph.count(request.pid) && graph.count(owner->second)) {
            graph[request.pid].push_back(owner->second);
        }
    }

    std::vector<int> sortedPids = processIds;
    std::sort(sortedPids.begin(), sortedPids.end());
    std::unordered_map<int, int> color;
    std::vector<int> stack;
    std::vector<int> cycle;
    auto visit = [&](auto&& self, int pid) -> bool {
        color[pid] = 1;
        stack.push_back(pid);
        auto neighbors = graph[pid];
        std::sort(neighbors.begin(), neighbors.end());
        for (int next : neighbors) {
            if (color[next] == 1) {
                auto start = std::find(stack.begin(), stack.end(), next);
                cycle.assign(start, stack.end());
                cycle.push_back(next);
                return true;
            }
            if (color[next] == 0 && self(self, next)) return true;
        }
        stack.pop_back();
        color[pid] = 2;
        return false;
    };
    for (int pid : sortedPids) {
        if (color[pid] == 0 && visit(visit, pid)) break;
    }
    return cycle;
}

bool applyResourceAction(const std::vector<int>& processIds,
                         const std::vector<OSResource>& resources,
                         const std::vector<OSResourceRequest>& requests,
                         const std::string& action,
                         int pid,
                         int resourceId,
                         ResourceActionResult& result,
                         std::string& error) {
    if (processIds.size() > 256 || resources.size() > 256 || requests.size() > 1024) {
        error = "The resource simulation has exceeded its supported size.";
        return false;
    }

    std::unordered_set<int> knownProcesses;
    for (int processId : processIds) {
        if (processId <= 0 || !knownProcesses.insert(processId).second) {
            error = "Process IDs must be unique positive numbers.";
            return false;
        }
    }
    std::unordered_set<int> knownResources;
    for (const auto& resource : resources) {
        if (resource.id <= 0 || !knownResources.insert(resource.id).second) {
            error = "Resource IDs must be unique positive numbers.";
            return false;
        }
        if (resource.ownerPid != 0 && !knownProcesses.count(resource.ownerPid)) {
            error = "A resource owner is not in the process list.";
            return false;
        }
    }
    std::unordered_set<std::string> knownRequests;
    for (const auto& request : requests) {
        if (!knownProcesses.count(request.pid) || !knownResources.count(request.resourceId)) {
            error = "A resource request refers to an unknown process or resource.";
            return false;
        }
        std::string key = std::to_string(request.pid) + ":" + std::to_string(request.resourceId);
        if (!knownRequests.insert(key).second) {
            error = "A process cannot request the same resource more than once.";
            return false;
        }
    }

    ResourceActionResult output;
    output.resources = resources;
    output.requests = requests;
    if (action == "add") {
        if (resources.size() >= 256) {
            error = "The resource simulation supports up to 256 resources.";
            return false;
        }
        int nextId = 1;
        for (const auto& resource : resources) {
            if (resource.id == std::numeric_limits<int>::max()) {
                error = "No additional resource IDs are available.";
                return false;
            }
            nextId = std::max(nextId, resource.id + 1);
        }
        output.resources.push_back({nextId, 0});
        output.changedResourceId = nextId;
        output.message = "Resource R" + std::to_string(nextId) + " added with one available instance.";
    } else if (action == "allocate") {
        if (!knownProcesses.count(pid)) {
            error = "Choose a process that exists in the simulation.";
            return false;
        }
        auto resource = std::find_if(output.resources.begin(), output.resources.end(), [resourceId](const OSResource& item) {
            return item.id == resourceId;
        });
        if (resource == output.resources.end()) {
            error = "Choose a resource that exists in the simulation.";
            return false;
        }
        if (resource->ownerPid != 0) {
            error = "R" + std::to_string(resourceId) + " is already allocated to P" + std::to_string(resource->ownerPid) + ".";
            return false;
        }
        resource->ownerPid = pid;
        output.requests.erase(std::remove_if(output.requests.begin(), output.requests.end(), [pid, resourceId](const OSResourceRequest& request) {
            return request.pid == pid && request.resourceId == resourceId;
        }), output.requests.end());
        output.message = "R" + std::to_string(resourceId) + " allocated to P" + std::to_string(pid) + ".";
    } else if (action == "request") {
        if (!knownProcesses.count(pid)) {
            error = "Choose a process that exists in the simulation.";
            return false;
        }
        auto resource = std::find_if(resources.begin(), resources.end(), [resourceId](const OSResource& item) {
            return item.id == resourceId;
        });
        if (resource == resources.end()) {
            error = "Choose a resource that exists in the simulation.";
            return false;
        }
        if (resource->ownerPid == 0) {
            error = "R" + std::to_string(resourceId) + " is available. Allocate it directly instead of creating a wait.";
            return false;
        }
        if (resource->ownerPid == pid) {
            error = "P" + std::to_string(pid) + " already owns R" + std::to_string(resourceId) + ".";
            return false;
        }
        if (knownRequests.count(std::to_string(pid) + ":" + std::to_string(resourceId))) {
            error = "P" + std::to_string(pid) + " is already waiting for R" + std::to_string(resourceId) + ".";
            return false;
        }
        output.requests.push_back({pid, resourceId});
        output.message = "P" + std::to_string(pid) + " is waiting for R" + std::to_string(resourceId) +
            " held by P" + std::to_string(resource->ownerPid) + ".";
    } else if (action == "terminate") {
        if (!knownProcesses.count(pid)) {
            error = "Choose a process that exists in the simulation.";
            return false;
        }
        for (auto& resource : output.resources) {
            if (resource.ownerPid == pid) {
                output.releasedResourceIds.push_back(resource.id);
                resource.ownerPid = 0;
            }
        }
        output.requests.erase(std::remove_if(output.requests.begin(), output.requests.end(), [pid](const OSResourceRequest& request) {
            return request.pid == pid;
        }), output.requests.end());
        output.message = "P" + std::to_string(pid) + " terminated in the simulation.";
    } else if (action == "demo") {
        if (processIds.size() < 2) {
            error = "Create at least two processes before building the deadlock example.";
            return false;
        }
        if (resources.size() > 254 || requests.size() > 1022) {
            error = "Reset the simulation before adding the deadlock example.";
            return false;
        }
        int nextId = 1;
        for (const auto& resource : resources) {
            if (resource.id == std::numeric_limits<int>::max()) {
                error = "No additional resource IDs are available.";
                return false;
            }
            nextId = std::max(nextId, resource.id + 1);
        }
        if (nextId >= std::numeric_limits<int>::max()) {
            error = "No additional resource IDs are available.";
            return false;
        }
        int firstResource = nextId;
        int secondResource = nextId + 1;
        int firstPid = processIds[0];
        int secondPid = processIds[1];
        output.resources.push_back({firstResource, firstPid});
        output.resources.push_back({secondResource, secondPid});
        output.requests.push_back({firstPid, secondResource});
        output.requests.push_back({secondPid, firstResource});
        output.changedResourceId = firstResource;
        output.message = "Built a two-process deadlock example with P" + std::to_string(firstPid) +
            " and P" + std::to_string(secondPid) + ".";
    } else if (action == "reset") {
        output.resources.clear();
        output.requests.clear();
        output.message = "Resource allocation state reset.";
    } else {
        error = "Choose add, allocate, request, terminate, demo or reset.";
        return false;
    }

    result = std::move(output);
    error.clear();
    return true;
}

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
