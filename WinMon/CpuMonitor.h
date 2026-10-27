// CpuMonitor.h : global CPU utilisation via the Performance Data Helper (PDH)
#pragma once
#include "Metrics.h"

class CCpuMonitor
{
public:
    CCpuMonitor();
    ~CCpuMonitor();

    bool Initialize();
    // Must be called at most once per polling cycle (>= 1 s apart for an accurate delta).
    void Collect(CpuMetrics& out);

    // Helpers shared with the process monitor for per-process CPU %.
    static ULONGLONG FileTimeToU64(const FILETIME& ft);
    static ULONGLONG GetTotalSystemCpuTime();   // kernel + user of all CPUs (100 ns units)

private:
    static CString ReadProcessorName();

    PDH_HQUERY   m_hQuery = nullptr;
    PDH_HCOUNTER m_hTotal = nullptr;
    DWORD        m_logicalProcessors = 0;
    CString      m_processorName;
    double       m_lastValue = 0.0;
};
