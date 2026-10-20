#include "os_simulator.h"

#include <algorithm>
#include <deque>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr int kMaxInt = std::numeric_limits<int>::max();

std::string P(int pid) { return "P" + std::to_string(pid); }
std::string R(int id) { return "R" + std::to_string(id); }

void complete(Process& p, int time) {
    p.completionTime = time;
    p.turnaroundTime = time - p.arrivalTime;
    p.waitingTime = p.turnaroundTime - p.burstTime;
}

int nextResourceId(const std::vector<OSResource>& resources) {
    int next = 1;
    for (const auto& resource : resources) {
        if (resource.id == kMaxInt) return 0;
        next = std::max(next, resource.id + 1);
    }
    return next;
}

}

bool calculateSchedule(const std::vector<Process>& processes, const std::string& algorithm,
                       int quantum, ScheduleResult& result, std::string& error) {
    auto fail = [&error](const std::string& message) { error = message; return false; };

    if (processes.empty()) return fail("Create at least one process before running the scheduler.");
    if (processes.size() > 256) return fail("The scheduler accepts up to 256 processes per run.");
    const bool rr = algorithm == "RR";
    if (!rr && algorithm != "FCFS" && algorithm != "SJF") return fail("Choose FCFS, SJF or Round Robin.");
    if (rr && quantum <= 0) return fail("Round Robin requires a positive time quantum.");

    std::unordered_set<int> pids;
    long long totalBurst = 0, latestArrival = 0;
    std::vector<Process> scheduled = processes;
    for (auto& p : scheduled) {
        if (p.pid <= 0 || !pids.insert(p.pid).second) return fail("Process IDs must be unique positive numbers.");
        if (p.arrivalTime < 0 || p.burstTime <= 0) return fail("Arrival times must be non-negative and burst times must be positive.");
        totalBurst += p.burstTime;
        latestArrival = std::max<long long>(latestArrival, p.arrivalTime);
        p.completionTime = p.turnaroundTime = p.waitingTime = 0;
    }
    if (totalBurst + latestArrival > kMaxInt) return fail("The combined arrival and burst times are too large to schedule.");

    ScheduleResult output;
    output.algorithm = algorithm;
    int time = 0;
    auto idleUntil = [&](int next) {
        output.gantt.push_back({0, time, next, true});
        time = next;
    };

    if (rr) {
        std::vector<size_t> order(scheduled.size());
        std::iota(order.begin(), order.end(), size_t{0});
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return std::make_pair(scheduled[a].arrivalTime, scheduled[a].pid) <
                   std::make_pair(scheduled[b].arrivalTime, scheduled[b].pid);
        });
        std::vector<int> remaining;
        for (const auto& p : scheduled) remaining.push_back(p.burstTime);

        std::deque<size_t> ready;
        size_t next = 0;
        auto admit = [&] {
            while (next < order.size() && scheduled[order[next]].arrivalTime <= time) ready.push_back(order[next++]);
        };
        while (next < order.size() || !ready.empty()) {
            admit();
            if (ready.empty()) { idleUntil(scheduled[order[next]].arrivalTime); continue; }

            const size_t i = ready.front();
            ready.pop_front();
            const int start = time, run = std::min(quantum, remaining[i]);
            time += run;
            remaining[i] -= run;
            output.gantt.push_back({scheduled[i].pid, start, time, false});
            admit();
            if (remaining[i] > 0) ready.push_back(i);
            else complete(scheduled[i], time);
        }
    } else {
        const bool sjf = algorithm == "SJF";
        auto key = [sjf](const Process& p) { return std::make_tuple(sjf ? p.burstTime : 0, p.arrivalTime, p.pid); };

        std::vector<size_t> pending(scheduled.size());
        std::iota(pending.begin(), pending.end(), size_t{0});
        while (!pending.empty()) {
            auto best = pending.end();
            for (auto it = pending.begin(); it != pending.end(); ++it) {
                if (scheduled[*it].arrivalTime <= time &&
                    (best == pending.end() || key(scheduled[*it]) < key(scheduled[*best]))) best = it;
            }
            if (best == pending.end()) {
                int next = kMaxInt;
                for (size_t i : pending) next = std::min(next, scheduled[i].arrivalTime);
                idleUntil(next);
                continue;
            }
            Process& p = scheduled[*best];
            output.gantt.push_back({p.pid, time, time + p.burstTime, false});
            time += p.burstTime;
            complete(p, time);
            pending.erase(best);
        }
    }

    output.processes = std::move(scheduled);
    long long waiting = 0, turnaround = 0;
    for (const auto& p : output.processes) {
        waiting += p.waitingTime;
        turnaround += p.turnaroundTime;
    }
    std::sort(output.processes.begin(), output.processes.end(),
              [](const Process& a, const Process& b) { return a.pid < b.pid; });

    const double count = static_cast<double>(output.processes.size());
    output.totalTime = time;
    output.averageWaiting = static_cast<double>(waiting) / count;
    output.averageTurnaround = static_cast<double>(turnaround) / count;
    output.cpuUtilization = time > 0 ? static_cast<double>(totalBurst) * 100.0 / time : 0;
    output.throughput = time > 0 ? count / time : 0;
    result = std::move(output);
    error.clear();
    return true;
}

std::vector<int> detectDeadlockCycle(const std::vector<int>& processIds,
                                     const std::vector<OSResource>& resources,
                                     const std::vector<OSResourceRequest>& requests) {
    std::unordered_map<int, std::vector<int>> graph;
    for (int pid : processIds) graph[pid];
    std::unordered_map<int, int> owners;
    for (const auto& resource : resources) owners[resource.id] = resource.ownerPid;
    for (const auto& request : requests) {
        const auto owner = owners.find(request.resourceId);
        if (owner != owners.end() && owner->second != 0 && graph.count(request.pid) && graph.count(owner->second)) {
            graph[request.pid].push_back(owner->second);
        }
    }
    for (auto& edge : graph) std::sort(edge.second.begin(), edge.second.end());

    std::vector<int> sortedPids = processIds;
    std::sort(sortedPids.begin(), sortedPids.end());

    std::unordered_map<int, int> color;
    std::vector<int> stack, cycle;
    auto visit = [&](auto&& self, int pid) -> bool {
        color[pid] = 1;
        stack.push_back(pid);
        for (int next : graph[pid]) {
            if (color[next] == 1) {
                cycle.assign(std::find(stack.begin(), stack.end(), next), stack.end());
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

bool applyResourceAction(const std::vector<int>& processIds, const std::vector<OSResource>& resources,
                         const std::vector<OSResourceRequest>& requests, const std::string& action,
                         int pid, int resourceId, ResourceActionResult& result, std::string& error) {
    auto fail = [&error](const std::string& message) { error = message; return false; };

    if (processIds.size() > 256 || resources.size() > 256 || requests.size() > 1024) {
        return fail("The resource simulation has exceeded its supported size.");
    }

    std::unordered_set<int> knownProcesses, knownResources;
    std::set<std::pair<int, int>> knownRequests;
    for (int id : processIds) {
        if (id <= 0 || !knownProcesses.insert(id).second) return fail("Process IDs must be unique positive numbers.");
    }
    for (const auto& resource : resources) {
        if (resource.id <= 0 || !knownResources.insert(resource.id).second) return fail("Resource IDs must be unique positive numbers.");
        if (resource.ownerPid != 0 && !knownProcesses.count(resource.ownerPid)) return fail("A resource owner is not in the process list.");
    }
    for (const auto& request : requests) {
        if (!knownProcesses.count(request.pid) || !knownResources.count(request.resourceId)) {
            return fail("A resource request refers to an unknown process or resource.");
        }
        if (!knownRequests.insert({request.pid, request.resourceId}).second) {
            return fail("A process cannot request the same resource more than once.");
        }
    }

    ResourceActionResult out;
    out.resources = resources;
    out.requests = requests;
    auto findResource = [&] {
        return std::find_if(out.resources.begin(), out.resources.end(),
                            [&](const OSResource& r) { return r.id == resourceId; });
    };

    if (action == "allocate" || action == "request" || action == "terminate") {
        if (!knownProcesses.count(pid)) return fail("Choose a process that exists in the simulation.");
    }

    if (action == "add") {
        if (resources.size() >= 256) return fail("The resource simulation supports up to 256 resources.");
        const int id = nextResourceId(resources);
        if (!id) return fail("No additional resource IDs are available.");
        out.resources.push_back({id, 0});
        out.changedResourceId = id;
        out.message = "Resource " + R(id) + " added with one available instance.";
    } else if (action == "allocate") {
        const auto resource = findResource();
        if (resource == out.resources.end()) return fail("Choose a resource that exists in the simulation.");
        if (resource->ownerPid != 0) return fail(R(resourceId) + " is already allocated to " + P(resource->ownerPid) + ".");
        resource->ownerPid = pid;
        out.requests.erase(std::remove_if(out.requests.begin(), out.requests.end(),
                               [&](const OSResourceRequest& r) { return r.pid == pid && r.resourceId == resourceId; }),
                           out.requests.end());
        out.message = R(resourceId) + " allocated to " + P(pid) + ".";
    } else if (action == "request") {
        const auto resource = findResource();
        if (resource == out.resources.end()) return fail("Choose a resource that exists in the simulation.");
        if (resource->ownerPid == 0) return fail(R(resourceId) + " is available. Allocate it directly instead of creating a wait.");
        if (resource->ownerPid == pid) return fail(P(pid) + " already owns " + R(resourceId) + ".");
        if (knownRequests.count({pid, resourceId})) return fail(P(pid) + " is already waiting for " + R(resourceId) + ".");
        out.requests.push_back({pid, resourceId});
        out.message = P(pid) + " is waiting for " + R(resourceId) + " held by " + P(resource->ownerPid) + ".";
    } else if (action == "terminate") {
        for (auto& resource : out.resources) {
            if (resource.ownerPid == pid) {
                out.releasedResourceIds.push_back(resource.id);
                resource.ownerPid = 0;
            }
        }
        out.requests.erase(std::remove_if(out.requests.begin(), out.requests.end(),
                               [&](const OSResourceRequest& r) { return r.pid == pid; }),
                           out.requests.end());
        out.message = P(pid) + " terminated in the simulation.";
    } else if (action == "demo") {
        if (processIds.size() < 2) return fail("Create at least two processes before building the deadlock example.");
        if (resources.size() > 254 || requests.size() > 1022) return fail("Reset the simulation before adding the deadlock example.");
        const int first = nextResourceId(resources);
        if (!first || first == kMaxInt) return fail("No additional resource IDs are available.");
        const int second = first + 1, firstPid = processIds[0], secondPid = processIds[1];
        out.resources.push_back({first, firstPid});
        out.resources.push_back({second, secondPid});
        out.requests.push_back({firstPid, second});
        out.requests.push_back({secondPid, first});
        out.changedResourceId = first;
        out.message = "Built a two-process deadlock example with " + P(firstPid) + " and " + P(secondPid) + ".";
    } else if (action == "reset") {
        out.resources.clear();
        out.requests.clear();
        out.message = "Resource allocation state reset.";
    } else {
        return fail("Choose add, allocate, request, terminate, demo or reset.");
    }

    result = std::move(out);
    error.clear();
    return true;
}

namespace {

void discardInput() {
    std::cin.clear();
    std::cin.ignore(10000, '\n');
}

void runFCFS() {
    int n = 0;
    std::cout << "\nEnter number of processes: ";
    if (!(std::cin >> n) || n <= 0 || n > 256) {
        std::cout << "Invalid number of processes.\n";
        discardInput();
        return;
    }

    std::vector<Process> processes(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        Process& p = processes[static_cast<size_t>(i)];
        p.pid = i + 1;
        std::cout << "Arrival time for P" << p.pid << ": ";
        if (!(std::cin >> p.arrivalTime) || p.arrivalTime < 0) { std::cout << "Invalid arrival time.\n"; discardInput(); return; }
        std::cout << "Burst time for P" << p.pid << ": ";
        if (!(std::cin >> p.burstTime) || p.burstTime <= 0) { std::cout << "Invalid burst time.\n"; discardInput(); return; }
    }

    ScheduleResult result;
    std::string error;
    if (!calculateSchedule(processes, "FCFS", 0, result, error)) {
        std::cout << error << '\n';
        return;
    }

    std::cout << "\nGantt Chart:\n";
    for (const auto& segment : result.gantt) std::cout << "| " << (segment.idle ? "Idle" : P(segment.pid)) << ' ';
    std::cout << "|\n\n" << std::left << std::setw(10) << "Process" << std::setw(12) << "Arrival"
              << std::setw(10) << "Burst" << std::setw(12) << "Completion" << std::setw(12) << "Turnaround"
              << "Waiting\n";
    for (const auto& p : result.processes) {
        std::cout << std::setw(10) << P(p.pid) << std::setw(12) << p.arrivalTime << std::setw(10) << p.burstTime
                  << std::setw(12) << p.completionTime << std::setw(12) << p.turnaroundTime << p.waitingTime << '\n';
    }
    std::cout << "\nAverage waiting time: " << result.averageWaiting
              << "\nAverage turnaround time: " << result.averageTurnaround << '\n';
}

}

void runOSSimulator() {
    int choice = -1;
    do {
        std::cout << "\n===== OS Simulator =====\n1. FCFS Scheduling\n0. Back\nEnter choice: ";
        if (!(std::cin >> choice)) {
            discardInput();
            std::cout << "Invalid choice.\n";
            choice = -1;
        } else if (choice == 1) {
            runFCFS();
        } else if (choice != 0) {
            std::cout << "Invalid choice.\n";
        }
    } while (choice != 0);
}