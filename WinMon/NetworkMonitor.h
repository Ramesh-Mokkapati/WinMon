// NetworkMonitor.h : adapter throughput and TCP/UDP socket tables
#pragma once
#include "Metrics.h"

class CNetworkMonitor
{
public:
    // Fills adapters and the aggregate in/out rates (physical, connected adapters).
    void CollectAdapters(std::vector<AdapterInfo>& out, double& totalInPerSec, double& totalOutPerSec);
    void CollectConnections(std::vector<ConnectionInfo>& out);

private:
    struct Counters
    {
        ULONG64 in = 0;
        ULONG64 out = 0;
    };

    static CString TcpStateToString(DWORD state);
    static CString InterfaceTypeToString(IFTYPE type);
    static CString AddressToString(int family, const void* addr);
    void CollectTcp(int family, std::vector<ConnectionInfo>& out);
    void CollectUdp(int family, std::vector<ConnectionInfo>& out);

    std::unordered_map<ULONG64, Counters> m_last;
    LARGE_INTEGER m_lastQpc = {};
};
