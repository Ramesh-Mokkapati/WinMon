// MonitorEngine.h : background polling engine (worker thread)
#pragma once
#include "DataManager.h"
#include "CpuMonitor.h"
#include "MemoryMonitor.h"
#include "ProcessMonitor.h"
#include "DiskMonitor.h"
#include "NetworkMonitor.h"

#define WM_UPDATE_SYS_METRICS (WM_USER + 101)

class CMonitorEngine
{
public:
    CMonitorEngine();
    ~CMonitorEngine();

    // Starts the worker; WM_UPDATE_SYS_METRICS is posted to hNotify after every cycle.
    bool Start(HWND hNotify);
    // Signals the stop event and waits for the worker to exit.
    void Stop();

    void SetIntervalMs(DWORD ms) { m_intervalMs = ms < 500 ? 500 : ms; }
    DWORD GetIntervalMs() const  { return m_intervalMs; }
    // Asks the worker to refresh every section on its next cycle.
    void RequestFullRefresh()    { m_forceFull = true; SetEvent(m_hWakeEvent); }

    const CDataManager& Data() const { return m_data; }

private:
    static UINT __cdecl MonitorWorkerThread(LPVOID pParam);
    void Run();
    void Collect(SystemSnapshot& snap, ULONGLONG cycle, bool full);

    CDataManager        m_data;
    CWinThread*         m_pThread = nullptr;
    HANDLE              m_hStopEvent = nullptr;
    HANDLE              m_hWakeEvent = nullptr;
    HWND                m_hNotify = nullptr;
    std::atomic<DWORD>  m_intervalMs { 1000 };
    std::atomic<bool>   m_forceFull { false };

    // Owned and used exclusively by the worker thread.
    CCpuMonitor         m_cpu;
    CMemoryMonitor      m_memory;
    CProcessMonitor     m_process;
    CDiskMonitor        m_disk;
    CNetworkMonitor     m_network;
};
