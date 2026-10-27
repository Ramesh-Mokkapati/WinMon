// DiskMonitor.h : volume capacities and physical disk throughput
#pragma once
#include "Metrics.h"

class CDiskMonitor
{
public:
    CDiskMonitor() = default;
    ~CDiskMonitor();

    bool Initialize();
    void CollectVolumes(std::vector<VolumeInfo>& out);
    void CollectIo(DiskIoMetrics& out);

private:
    PDH_HQUERY   m_hQuery = nullptr;
    PDH_HCOUNTER m_hRead = nullptr;
    PDH_HCOUNTER m_hWrite = nullptr;
};
