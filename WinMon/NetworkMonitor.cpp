// NetworkMonitor.cpp
#include "pch.h"
#include "NetworkMonitor.h"

void CNetworkMonitor::CollectAdapters(std::vector<AdapterInfo>& out, double& totalInPerSec, double& totalOutPerSec)
{
    out.clear();
    totalInPerSec = totalOutPerSec = 0.0;

    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || !table)
        return;

    LARGE_INTEGER qpc, freq;
    QueryPerformanceCounter(&qpc);
    QueryPerformanceFrequency(&freq);
    const double elapsedSec = m_lastQpc.QuadPart ? double(qpc.QuadPart - m_lastQpc.QuadPart) / double(freq.QuadPart) : 0.0;

    std::unordered_map<ULONG64, Counters> next;
    double hwIn = 0.0, hwOut = 0.0, anyIn = 0.0, anyOut = 0.0;
    bool haveHardware = false;

    for (ULONG i = 0; i < table->NumEntries; ++i)
    {
        const MIB_IF_ROW2& row = table->Table[i];

        // Skip loopback and the NDIS filter / WFP shim interfaces that duplicate real adapters.
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.InterfaceAndOperStatusFlags.FilterInterface)
            continue;

        const bool up = row.OperStatus == IfOperStatusUp;
        const bool hardware = row.InterfaceAndOperStatusFlags.HardwareInterface != FALSE;
        // Hide virtual interfaces that have never carried traffic (WAN Miniports, unused tunnels).
        if (!hardware && (!up || (row.InOctets == 0 && row.OutOctets == 0)))
            continue;

        AdapterInfo a;
        a.luid         = row.InterfaceLuid.Value;
        a.name         = row.Alias;
        a.description  = row.Description;
        a.type         = InterfaceTypeToString(row.Type);
        a.isUp         = up;
        a.status       = up ? _T("Connected") : _T("Disconnected");
        a.linkSpeedBps = up ? row.ReceiveLinkSpeed : 0;
        a.totalIn      = row.InOctets;
        a.totalOut     = row.OutOctets;

        auto prev = m_last.find(a.luid);
        if (prev != m_last.end() && elapsedSec > 0.0)
        {
            if (row.InOctets >= prev->second.in)
                a.inPerSec = double(row.InOctets - prev->second.in) / elapsedSec;
            if (row.OutOctets >= prev->second.out)
                a.outPerSec = double(row.OutOctets - prev->second.out) / elapsedSec;
        }
        next[a.luid] = { row.InOctets, row.OutOctets };

        if (up)
        {
            anyIn += a.inPerSec;
            anyOut += a.outPerSec;
            if (hardware)
            {
                haveHardware = true;
                hwIn += a.inPerSec;
                hwOut += a.outPerSec;
            }
        }
        out.push_back(std::move(a));
    }
    FreeMibTable(table);

    // Virtual switches (Hyper-V, VPN) re-carry physical traffic, so prefer hardware totals.
    totalInPerSec  = haveHardware ? hwIn : anyIn;
    totalOutPerSec = haveHardware ? hwOut : anyOut;

    m_last.swap(next);
    m_lastQpc = qpc;
}

void CNetworkMonitor::CollectConnections(std::vector<ConnectionInfo>& out)
{
    out.clear();
    CollectTcp(AF_INET, out);
    CollectTcp(AF_INET6, out);
    CollectUdp(AF_INET, out);
    CollectUdp(AF_INET6, out);
}

void CNetworkMonitor::CollectTcp(int family, std::vector<ConnectionInfo>& out)
{
    std::vector<BYTE> buffer;
    DWORD size = 0;
    DWORD rc = ERROR_INSUFFICIENT_BUFFER;
    // The table can grow between the size query and the fetch, so loop.
    for (int attempt = 0; attempt < 4 && rc == ERROR_INSUFFICIENT_BUFFER; ++attempt)
    {
        buffer.resize(size ? size : 1);
        rc = GetExtendedTcpTable(buffer.data(), &size, FALSE, family, TCP_TABLE_OWNER_PID_ALL, 0);
    }
    if (rc != NO_ERROR)
        return;

    if (family == AF_INET)
    {
        const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i)
        {
            const auto& r = table->table[i];
            ConnectionInfo c;
            c.protocol   = _T("TCP");
            c.localAddr  = AddressToString(AF_INET, &r.dwLocalAddr);
            c.localPort  = ntohs(static_cast<u_short>(r.dwLocalPort));
            c.remoteAddr = AddressToString(AF_INET, &r.dwRemoteAddr);
            c.remotePort = r.dwState == MIB_TCP_STATE_LISTEN ? 0 : ntohs(static_cast<u_short>(r.dwRemotePort));
            c.state      = TcpStateToString(r.dwState);
            c.pid        = r.dwOwningPid;
            out.push_back(std::move(c));
        }
    }
    else
    {
        const auto* table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i)
        {
            const auto& r = table->table[i];
            ConnectionInfo c;
            c.protocol   = _T("TCPv6");
            c.localAddr  = AddressToString(AF_INET6, r.ucLocalAddr);
            c.localPort  = ntohs(static_cast<u_short>(r.dwLocalPort));
            c.remoteAddr = AddressToString(AF_INET6, r.ucRemoteAddr);
            c.remotePort = r.dwState == MIB_TCP_STATE_LISTEN ? 0 : ntohs(static_cast<u_short>(r.dwRemotePort));
            c.state      = TcpStateToString(r.dwState);
            c.pid        = r.dwOwningPid;
            out.push_back(std::move(c));
        }
    }
}

void CNetworkMonitor::CollectUdp(int family, std::vector<ConnectionInfo>& out)
{
    std::vector<BYTE> buffer;
    DWORD size = 0;
    DWORD rc = ERROR_INSUFFICIENT_BUFFER;
    for (int attempt = 0; attempt < 4 && rc == ERROR_INSUFFICIENT_BUFFER; ++attempt)
    {
        buffer.resize(size ? size : 1);
        rc = GetExtendedUdpTable(buffer.data(), &size, FALSE, family, UDP_TABLE_OWNER_PID, 0);
    }
    if (rc != NO_ERROR)
        return;

    if (family == AF_INET)
    {
        const auto* table = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i)
        {
            const auto& r = table->table[i];
            ConnectionInfo c;
            c.protocol  = _T("UDP");
            c.localAddr = AddressToString(AF_INET, &r.dwLocalAddr);
            c.localPort = ntohs(static_cast<u_short>(r.dwLocalPort));
            c.pid       = r.dwOwningPid;
            out.push_back(std::move(c));
        }
    }
    else
    {
        const auto* table = reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID*>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i)
        {
            const auto& r = table->table[i];
            ConnectionInfo c;
            c.protocol  = _T("UDPv6");
            c.localAddr = AddressToString(AF_INET6, r.ucLocalAddr);
            c.localPort = ntohs(static_cast<u_short>(r.dwLocalPort));
            c.pid       = r.dwOwningPid;
            out.push_back(std::move(c));
        }
    }
}

CString CNetworkMonitor::AddressToString(int family, const void* addr)
{
    WCHAR buf[INET6_ADDRSTRLEN] = {};
    if (!InetNtopW(family, addr, buf, _countof(buf)))
        return CString();
    return CString(buf);
}

CString CNetworkMonitor::TcpStateToString(DWORD state)
{
    switch (state)
    {
    case MIB_TCP_STATE_CLOSED:     return _T("Closed");
    case MIB_TCP_STATE_LISTEN:     return _T("Listening");
    case MIB_TCP_STATE_SYN_SENT:   return _T("SYN sent");
    case MIB_TCP_STATE_SYN_RCVD:   return _T("SYN received");
    case MIB_TCP_STATE_ESTAB:      return _T("Established");
    case MIB_TCP_STATE_FIN_WAIT1:  return _T("FIN wait 1");
    case MIB_TCP_STATE_FIN_WAIT2:  return _T("FIN wait 2");
    case MIB_TCP_STATE_CLOSE_WAIT: return _T("Close wait");
    case MIB_TCP_STATE_CLOSING:    return _T("Closing");
    case MIB_TCP_STATE_LAST_ACK:   return _T("Last ACK");
    case MIB_TCP_STATE_TIME_WAIT:  return _T("Time wait");
    case MIB_TCP_STATE_DELETE_TCB: return _T("Delete TCB");
    default:                       return _T("Unknown");
    }
}

CString CNetworkMonitor::InterfaceTypeToString(IFTYPE type)
{
    switch (type)
    {
    case IF_TYPE_ETHERNET_CSMACD: return _T("Ethernet");
    case IF_TYPE_IEEE80211:       return _T("Wi-Fi");
    case IF_TYPE_PPP:             return _T("PPP");
    case IF_TYPE_TUNNEL:          return _T("Tunnel");
    case IF_TYPE_WWANPP:
    case IF_TYPE_WWANPP2:         return _T("Mobile broadband");
    case IF_TYPE_IEEE1394:        return _T("FireWire");
    default:                      return _T("Other");
    }
}
