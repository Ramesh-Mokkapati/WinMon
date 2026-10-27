// MemoryMonitor.h : global RAM usage and per-process memory footprints
#pragma once
#include "Metrics.h"

class CMemoryMonitor
{
public:
    void Collect(MemoryMetrics& out);
    void CollectSummary(SystemSummary& out);

    // Fills working set / private bytes for an already opened process handle.
    static bool QueryProcessMemory(HANDLE hProcess, SIZE_T& workingSet, SIZE_T& privateBytes);
};
