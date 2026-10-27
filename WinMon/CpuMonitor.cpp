// CpuMonitor.cpp
#include "pch.h"
#include "CpuMonitor.h"

CCpuMonitor::CCpuMonitor()
{
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    m_logicalProcessors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (m_logicalProcessors == 0)
        m_logicalProcessors = si.dwNumberOfProcessors;
    m_processorName = ReadProcessorName();
}

CCpuMonitor::~CCpuMonitor()
{
    if (m_hQuery)
        PdhCloseQuery(m_hQuery);
}

bool CCpuMonitor::Initialize()
{
    if (PdhOpenQueryW(nullptr, 0, &m_hQuery) != ERROR_SUCCESS)
        return false;

    // "% Processor Utility" matches Task Manager on modern Windows; fall back to the classic counter.
    if (PdhAddEnglishCounterW(m_hQuery, L"\\Processor Information(_Total)\\% Processor Utility", 0, &m_hTotal) != ERROR_SUCCESS &&
        PdhAddEnglishCounterW(m_hQuery, L"\\Processor(_Total)\\% Processor Time", 0, &m_hTotal) != ERROR_SUCCESS)
    {
        PdhCloseQuery(m_hQuery);
        m_hQuery = nullptr;
        return false;
    }

    // Prime the query: rate counters need two samples.
    PdhCollectQueryData(m_hQuery);
    return true;
}

void CCpuMonitor::Collect(CpuMetrics& out)
{
    out.logicalProcessors = m_logicalProcessors;
    out.processorName = m_processorName;

    if (m_hQuery && PdhCollectQueryData(m_hQuery) == ERROR_SUCCESS)
    {
        PDH_FMT_COUNTERVALUE value = {};
        if (PdhGetFormattedCounterValue(m_hTotal, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &value) == ERROR_SUCCESS &&
            value.CStatus == PDH_CSTATUS_VALID_DATA)
        {
            m_lastValue = min(max(value.doubleValue, 0.0), 100.0);
        }
    }
    out.totalUsage = m_lastValue;
}

ULONGLONG CCpuMonitor::FileTimeToU64(const FILETIME& ft)
{
    return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

ULONGLONG CCpuMonitor::GetTotalSystemCpuTime()
{
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user))
        return 0;
    // Kernel time already includes idle time, so kernel + user is the total CPU time available.
    return FileTimeToU64(kernel) + FileTimeToU64(user);
}

CString CCpuMonitor::ReadProcessorName()
{
    CString name;
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        WCHAR buf[256] = {};
        DWORD cb = sizeof(buf) - sizeof(WCHAR);
        if (RegQueryValueExW(hKey, L"ProcessorNameString", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &cb) == ERROR_SUCCESS)
            name = buf;
        RegCloseKey(hKey);
    }
    name.Trim();
    return name.IsEmpty() ? CString(_T("Unknown processor")) : name;
}
