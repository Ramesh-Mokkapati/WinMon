// DataManager.h : thread-safe store for the latest system snapshot
#pragma once
#include "Metrics.h"

class CDataManager
{
public:
    void Update(SystemSnapshot&& snapshot)
    {
        CSingleLock lock(&m_cs, TRUE);
        m_snapshot = std::move(snapshot);
    }

    SystemSnapshot GetSnapshot() const
    {
        CSingleLock lock(&m_cs, TRUE);
        return m_snapshot;
    }

private:
    mutable CCriticalSection m_cs;
    SystemSnapshot           m_snapshot;
};
