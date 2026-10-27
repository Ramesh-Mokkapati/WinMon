// ProcessMonitor.cpp
#include "pch.h"
#include "ProcessMonitor.h"
#include "CpuMonitor.h"
#include "MemoryMonitor.h"

void CProcessMonitor::Collect(std::vector<ProcessInfo>& out)
{
    out.clear();

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return;

    const ULONGLONG systemCpu = CCpuMonitor::GetTotalSystemCpuTime();
    const ULONGLONG cpuDelta  = (m_lastSystemCpu && systemCpu > m_lastSystemCpu) ? systemCpu - m_lastSystemCpu : 0;

    LARGE_INTEGER qpc, freq;
    QueryPerformanceCounter(&qpc);
    QueryPerformanceFrequency(&freq);
    const double elapsedSec = m_lastQpc.QuadPart ? double(qpc.QuadPart - m_lastQpc.QuadPart) / double(freq.QuadPart) : 0.0;

    std::unordered_map<DWORD, History> nextHistory;
    nextHistory.reserve(m_history.size() + 32);

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(hSnap, &pe); ok; ok = Process32NextW(hSnap, &pe))
    {
        ProcessInfo info;
        info.pid          = pe.th32ProcessID;
        info.parentPid    = pe.th32ParentProcessID;
        info.name         = pe.szExeFile;
        info.threads      = pe.cntThreads;
        info.basePriority = pe.pcPriClassBase;

        if (info.pid == 0)
            info.name = _T("System Idle Process");

        HANDLE hProc = info.pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, info.pid) : nullptr;
        if (hProc)
        {
            History h;

            FILETIME ftCreate, ftExit, ftKernel, ftUser;
            if (GetProcessTimes(hProc, &ftCreate, &ftExit, &ftKernel, &ftUser))
            {
                h.createTime = CCpuMonitor::FileTimeToU64(ftCreate);
                h.cpuTime    = CCpuMonitor::FileTimeToU64(ftKernel) + CCpuMonitor::FileTimeToU64(ftUser);
            }

            CMemoryMonitor::QueryProcessMemory(hProc, info.workingSet, info.privateBytes);
            info.priorityClass = GetPriorityClass(hProc);

            IO_COUNTERS io = {};
            if (GetProcessIoCounters(hProc, &io))
            {
                h.readBytes  = io.ReadTransferCount;
                h.writeBytes = io.WriteTransferCount;
            }

            WCHAR path[MAX_PATH * 2] = {};
            DWORD cch = _countof(path);
            if (QueryFullProcessImageNameW(hProc, 0, path, &cch))
                info.imagePath = path;

            CloseHandle(hProc);

            // Deltas are only valid when the PID still refers to the same process instance.
            auto prev = m_history.find(info.pid);
            if (prev != m_history.end() && prev->second.createTime == h.createTime)
            {
                if (cpuDelta && h.cpuTime >= prev->second.cpuTime)
                    info.cpuPercent = min(100.0, 100.0 * double(h.cpuTime - prev->second.cpuTime) / double(cpuDelta));
                if (elapsedSec > 0.0)
                {
                    if (h.readBytes >= prev->second.readBytes)
                        info.ioReadPerSec = double(h.readBytes - prev->second.readBytes) / elapsedSec;
                    if (h.writeBytes >= prev->second.writeBytes)
                        info.ioWritePerSec = double(h.writeBytes - prev->second.writeBytes) / elapsedSec;
                }
            }
            nextHistory[info.pid] = h;
        }
        else if (info.pid != 0)
        {
            info.accessDenied = true;
        }

        out.push_back(std::move(info));
    }
    CloseHandle(hSnap);

    // The idle process cannot be opened; derive its share from what is left.
    if (cpuDelta)
    {
        double busy = 0.0;
        for (const auto& p : out)
            busy += p.cpuPercent;
        for (auto& p : out)
            if (p.pid == 0)
                p.cpuPercent = max(0.0, 100.0 - busy);
    }

    m_history.swap(nextHistory);
    m_lastSystemCpu = systemCpu;
    m_lastQpc = qpc;
}

bool CProcessMonitor::EnumerateModules(DWORD pid, std::vector<ModuleInfo>& out, DWORD& error)
{
    out.clear();
    error = ERROR_SUCCESS;

    HANDLE hSnap = INVALID_HANDLE_VALUE;
    // ERROR_BAD_LENGTH means the module list changed while the snapshot was taken: retry.
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (hSnap != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH)
            break;
    }
    if (hSnap == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }

    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);
    for (BOOL ok = Module32FirstW(hSnap, &me); ok; ok = Module32NextW(hSnap, &me))
    {
        ModuleInfo mi;
        mi.name        = me.szModule;
        mi.path        = me.szExePath;
        mi.baseAddress = reinterpret_cast<ULONG_PTR>(me.modBaseAddr);
        mi.size        = me.modBaseSize;
        out.push_back(std::move(mi));
    }
    CloseHandle(hSnap);
    return true;
}

bool CProcessMonitor::TerminateProcessById(DWORD pid, DWORD& error)
{
    error = ERROR_SUCCESS;
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProc)
    {
        error = GetLastError();
        return false;
    }
    const BOOL ok = TerminateProcess(hProc, 1);
    if (!ok)
        error = GetLastError();
    CloseHandle(hProc);
    return ok != FALSE;
}

CString CProcessMonitor::PriorityClassToString(DWORD priorityClass)
{
    switch (priorityClass)
    {
    case IDLE_PRIORITY_CLASS:         return _T("Low");
    case BELOW_NORMAL_PRIORITY_CLASS: return _T("Below normal");
    case NORMAL_PRIORITY_CLASS:       return _T("Normal");
    case ABOVE_NORMAL_PRIORITY_CLASS: return _T("Above normal");
    case HIGH_PRIORITY_CLASS:         return _T("High");
    case REALTIME_PRIORITY_CLASS:     return _T("Realtime");
    default:                          return _T("");
    }
}
