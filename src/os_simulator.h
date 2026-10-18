#ifndef OS_SIMULATOR_H
#define OS_SIMULATOR_H

#include <string>
#include <vector>

struct Process {
    int pid = 0;
    int arrivalTime = 0;
    int burstTime = 0;
    int completionTime = 0;
    int turnaroundTime = 0;
    int waitingTime = 0;
    std::string name;
};

struct GanttSegment {
    int pid = 0;
    int start = 0;
    int end = 0;
    bool idle = false;
};

struct ScheduleResult {
    std::string algorithm;
    std::vector<GanttSegment> gantt;
    std::vector<Process> processes;
    int totalTime = 0;
    double averageWaiting = 0;
    double averageTurnaround = 0;
    double cpuUtilization = 0;
    double throughput = 0;
};

struct OSResource {
    int id = 0;
    int ownerPid = 0;
};

struct OSResourceRequest {
    int pid = 0;
    int resourceId = 0;
};

struct ResourceActionResult {
    std::vector<OSResource> resources;
    std::vector<OSResourceRequest> requests;
    std::vector<int> releasedResourceIds;
    int changedResourceId = 0;
    std::string message;
};

bool calculateSchedule(const std::vector<Process>& processes,
                       const std::string& algorithm,
                       int quantum,
                       ScheduleResult& result,
                       std::string& error);

std::vector<int> detectDeadlockCycle(const std::vector<int>& processIds,
                                     const std::vector<OSResource>& resources,
                                     const std::vector<OSResourceRequest>& requests);

bool applyResourceAction(const std::vector<int>& processIds,
                         const std::vector<OSResource>& resources,
                         const std::vector<OSResourceRequest>& requests,
                         const std::string& action,
                         int pid,
                         int resourceId,
                         ResourceActionResult& result,
                         std::string& error);

void runOSSimulator();

#endif
