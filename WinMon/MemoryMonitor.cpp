// MemoryMonitor.cpp
#include "pch.h"
#include "MemoryMonitor.h"

void CMemoryMonitor::Collect(MemoryMetrics& out)
{
    MEMORYSTATUSEX ms = {};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms))
        return;

    out.memoryLoad    = ms.dwMemoryLoad;
    out.totalPhys     = ms.ullTotalPhys;
    out.availPhys     = ms.ullAvailPhys;
    out.totalPageFile = ms.ullTotalPageFile;
    out.availPageFile = ms.ullAvailPageFile;
}

void CMemoryMonitor::CollectSummary(SystemSummary& out)
{
    PERFORMANCE_INFORMATION pi = {};
    pi.cb = sizeof(pi);
    if (GetPerformanceInfo(&pi, sizeof(pi)))
    {
        out.processCount = pi.ProcessCount;
        out.threadCount  = pi.ThreadCount;
        out.handleCount  = pi.HandleCount;
    }
    out.uptimeMs = GetTickCount64();
}

bool CMemoryMonitor::QueryProcessMemory(HANDLE hProcess, SIZE_T& workingSet, SIZE_T& privateBytes)
{
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(hProcess, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return false;
    workingSet   = pmc.WorkingSetSize;
    privateBytes = pmc.PrivateUsage;
    return true;
}
