// DiskMonitor.cpp
#include "pch.h"
#include "DiskMonitor.h"

CDiskMonitor::~CDiskMonitor()
{
    if (m_hQuery)
        PdhCloseQuery(m_hQuery);
}

bool CDiskMonitor::Initialize()
{
    if (PdhOpenQueryW(nullptr, 0, &m_hQuery) != ERROR_SUCCESS)
        return false;

    if (PdhAddEnglishCounterW(m_hQuery, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &m_hRead) != ERROR_SUCCESS ||
        PdhAddEnglishCounterW(m_hQuery, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &m_hWrite) != ERROR_SUCCESS)
    {
        PdhCloseQuery(m_hQuery);
        m_hQuery = nullptr;
        return false;
    }
    PdhCollectQueryData(m_hQuery);
    return true;
}

void CDiskMonitor::CollectIo(DiskIoMetrics& out)
{
    out.available = false;
    if (!m_hQuery || PdhCollectQueryData(m_hQuery) != ERROR_SUCCESS)
        return;

    PDH_FMT_COUNTERVALUE rd = {}, wr = {};
    if (PdhGetFormattedCounterValue(m_hRead, PDH_FMT_DOUBLE, nullptr, &rd) == ERROR_SUCCESS &&
        PdhGetFormattedCounterValue(m_hWrite, PDH_FMT_DOUBLE, nullptr, &wr) == ERROR_SUCCESS)
    {
        out.readBytesPerSec  = rd.doubleValue;
        out.writeBytesPerSec = wr.doubleValue;
        out.available = true;
    }
}

void CDiskMonitor::CollectVolumes(std::vector<VolumeInfo>& out)
{
    out.clear();

    WCHAR drives[512] = {};
    const DWORD len = GetLogicalDriveStringsW(_countof(drives) - 1, drives);
    if (len == 0 || len >= _countof(drives))
        return;

    // Don't let an unreachable drive pop up a "no disk" error box.
    DWORD oldMode = 0;
    SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &oldMode);

    for (const WCHAR* root = drives; *root; root += wcslen(root) + 1)
    {
        if (GetDriveTypeW(root) != DRIVE_FIXED)
            continue;

        VolumeInfo vi;
        vi.root = root;

        ULARGE_INTEGER avail = {}, total = {}, totalFree = {};
        if (!GetDiskFreeSpaceExW(root, &avail, &total, &totalFree))
            continue;
        vi.totalBytes = total.QuadPart;
        vi.freeBytes  = totalFree.QuadPart;
        vi.availBytes = avail.QuadPart;

        WCHAR label[MAX_PATH + 1] = {}, fs[MAX_PATH + 1] = {};
        if (GetVolumeInformationW(root, label, _countof(label), nullptr, nullptr, nullptr, fs, _countof(fs)))
        {
            vi.label = label;
            vi.fileSystem = fs;
        }
        out.push_back(std::move(vi));
    }

    SetThreadErrorMode(oldMode, nullptr);
}
