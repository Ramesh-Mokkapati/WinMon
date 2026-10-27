// MonitorEngine.cpp
#include "pch.h"
#include "MonitorEngine.h"

namespace
{
    // Expensive sections are refreshed less often than the 1 s CPU / memory sample.
    constexpr ULONGLONG kProcessEvery = 2;   // cycles
    constexpr ULONGLONG kNetworkEvery = 2;
    constexpr ULONGLONG kVolumeEvery  = 5;
}

CMonitorEngine::CMonitorEngine()
{
    m_hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_hWakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

CMonitorEngine::~CMonitorEngine()
{
    Stop();
    if (m_hStopEvent) CloseHandle(m_hStopEvent);
    if (m_hWakeEvent) CloseHandle(m_hWakeEvent);
}

bool CMonitorEngine::Start(HWND hNotify)
{
    if (m_pThread)
        return true;

    m_hNotify = hNotify;
    ResetEvent(m_hStopEvent);

    m_pThread = AfxBeginThread(MonitorWorkerThread, this, THREAD_PRIORITY_BELOW_NORMAL, 0, CREATE_SUSPENDED);
    if (!m_pThread)
        return false;
    m_pThread->m_bAutoDelete = FALSE;   // we wait on and delete it ourselves in Stop()
    m_pThread->ResumeThread();
    return true;
}

void CMonitorEngine::Stop()
{
    if (!m_pThread)
        return;

    SetEvent(m_hStopEvent);
    WaitForSingleObject(m_pThread->m_hThread, INFINITE);
    delete m_pThread;
    m_pThread = nullptr;
}

UINT __cdecl CMonitorEngine::MonitorWorkerThread(LPVOID pParam)
{
    reinterpret_cast<CMonitorEngine*>(pParam)->Run();
    return 0;
}

void CMonitorEngine::Run()
{
    // PDH queries are created on the worker so all PDH calls stay on one thread.
    m_cpu.Initialize();
    m_disk.Initialize();

    SystemSnapshot snap;
    ULONGLONG cycle = 0;

    // First pass establishes the baselines for every delta-based metric.
    Collect(snap, cycle++, true);

    const HANDLE waits[] = { m_hStopEvent, m_hWakeEvent };
    for (;;)
    {
        const DWORD rc = WaitForMultipleObjects(_countof(waits), waits, FALSE, m_intervalMs);
        if (rc == WAIT_OBJECT_0)
            break;  // stop requested

        const bool full = m_forceFull.exchange(false);
        Collect(snap, cycle++, full);

        m_data.Update(SystemSnapshot(snap));
        if (m_hNotify && IsWindow(m_hNotify))
            ::PostMessage(m_hNotify, WM_UPDATE_SYS_METRICS, 0, 0);
    }
}

void CMonitorEngine::Collect(SystemSnapshot& snap, ULONGLONG cycle, bool full)
{
    m_cpu.Collect(snap.cpu);
    m_memory.Collect(snap.memory);
    m_memory.CollectSummary(snap.summary);
    m_disk.CollectIo(snap.diskIo);

    const bool doProcesses = full || cycle % kProcessEvery == 0;
    const bool doNetwork   = full || cycle % kNetworkEvery == 0;
    const bool doVolumes   = full || cycle % kVolumeEvery == 0;

    // Throughput is cheap and drives the graph, so sample adapters every cycle.
    std::vector<AdapterInfo> adapters;
    m_network.CollectAdapters(adapters, snap.netInPerSec, snap.netOutPerSec);

    if (doProcesses)
    {
        m_process.Collect(snap.processes);
        ++snap.processSeq;
    }

    if (doNetwork)
    {
        snap.adapters = std::move(adapters);
        m_network.CollectConnections(snap.connections);

        std::unordered_map<DWORD, const CString*> names;
        for (const auto& p : snap.processes)
            names[p.pid] = &p.name;
        for (auto& c : snap.connections)
        {
            auto it = names.find(c.pid);
            c.processName = it != names.end() ? *it->second : CString();
        }
        ++snap.networkSeq;
    }

    if (doVolumes)
    {
        m_disk.CollectVolumes(snap.volumes);
        ++snap.volumeSeq;
    }

    ++snap.sampleSeq;
    GetLocalTime(&snap.timestamp);
}
