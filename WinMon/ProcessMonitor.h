// ProcessMonitor.h : process / thread / module enumeration and process management
#pragma once
#include "Metrics.h"

struct ModuleInfo
{
    CString   name;
    CString   path;
    ULONG_PTR baseAddress = 0;
    DWORD     size = 0;
};

class CProcessMonitor
{
public:
    // Enumerates processes and computes per-process CPU % and I/O rates from the
    // deltas against the previous call.
    void Collect(std::vector<ProcessInfo>& out);

    static bool EnumerateModules(DWORD pid, std::vector<ModuleInfo>& out, DWORD& error);
    static bool TerminateProcessById(DWORD pid, DWORD& error);
    static CString PriorityClassToString(DWORD priorityClass);

private:
    struct History
    {
        ULONGLONG createTime = 0;
        ULONGLONG cpuTime = 0;
        ULONGLONG readBytes = 0;
        ULONGLONG writeBytes = 0;
    };

    std::unordered_map<DWORD, History> m_history;
    ULONGLONG     m_lastSystemCpu = 0;
    LARGE_INTEGER m_lastQpc = {};
};
