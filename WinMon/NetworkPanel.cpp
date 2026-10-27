// NetworkPanel.cpp
#include "pch.h"
#include "NetworkPanel.h"
#include "Format.h"

namespace
{
    enum { IDC_ADAPTER_LIST = 3001, IDC_CONNECTION_LIST };

    ULONGLONG ConnectionKey(const ConnectionInfo& c)
    {
        CString id;
        id.Format(_T("%s|%s|%u|%s|%u|%u"), c.protocol.GetString(), c.localAddr.GetString(), c.localPort,
                  c.remoteAddr.GetString(), c.remotePort, c.pid);
        return std::hash<std::wstring>()(std::wstring(id.GetString()));
    }

    CString Port(USHORT port)
    {
        return port ? fmt::Number(port) : CString(_T("*"));
    }
}

BOOL CNetworkPanel::CreateControls()
{
    if (!CreateLabel(m_adaptersLabel, _T("Network adapters")) ||
        !m_adapters.CreateList(this, IDC_ADAPTER_LIST) ||
        !CreateLabel(m_connectionsLabel, _T("Active connections")) ||
        !m_connections.CreateList(this, IDC_CONNECTION_LIST))
        return FALSE;

    const int R = LVCFMT_RIGHT;
    m_adapters.SetColumns({
        { _T("Name"),           Dpi(160), false },
        { _T("Description"),    Dpi(260), false },
        { _T("Type"),           Dpi(80),  false },
        { _T("Status"),         Dpi(90),  false },
        { _T("Link Speed"),     Dpi(90),  true, R },
        { _T("Receive"),        Dpi(100), true, R },
        { _T("Send"),           Dpi(100), true, R },
        { _T("Total Received"), Dpi(110), true, R },
        { _T("Total Sent"),     Dpi(110), true, R },
    });
    m_adapters.SetDefaultSort(3, true);

    m_connections.SetColumns({
        { _T("Process"),        Dpi(170), false },
        { _T("PID"),            Dpi(60),  true, R },
        { _T("Protocol"),       Dpi(65),  false },
        { _T("Local Address"),  Dpi(200), false },
        { _T("Local Port"),     Dpi(75),  true, R },
        { _T("Remote Address"), Dpi(200), false },
        { _T("Remote Port"),    Dpi(85),  true, R },
        { _T("State"),          Dpi(100), false },
    });
    m_connections.SetDefaultSort(0, true);
    return TRUE;
}

void CNetworkPanel::Layout(int cx, int cy)
{
    const int m = Dpi(8), labelH = Dpi(20);
    const int avail = cy - 2 * m - 2 * labelH;
    const int adaptersH = max(avail * 30 / 100, Dpi(70));

    int y = m;
    m_adaptersLabel.MoveWindow(m, y, cx - 2 * m, labelH - Dpi(4));
    y += labelH;
    m_adapters.MoveWindow(m, y, cx - 2 * m, adaptersH);
    y += adaptersH + Dpi(6);
    m_connectionsLabel.MoveWindow(m, y, cx - 2 * m, labelH - Dpi(4));
    y += labelH;
    m_connections.MoveWindow(m, y, cx - 2 * m, max(cy - y - m, 0));
}

void CNetworkPanel::UpdateMetrics(const SystemSnapshot& snap)
{
    if (snap.networkSeq == m_lastSeq)
        return;
    m_lastSeq = snap.networkSeq;

    std::vector<ListRow> adapters;
    for (const auto& a : snap.adapters)
    {
        ListRow r;
        r.key = a.luid;
        r.text = { a.name, a.description, a.type, a.status, fmt::BitRate(a.linkSpeedBps),
                   fmt::Rate(a.inPerSec), fmt::Rate(a.outPerSec), fmt::Bytes(double(a.totalIn)), fmt::Bytes(double(a.totalOut)) };
        r.value = { 0, 0, 0, 0, double(a.linkSpeedBps), a.inPerSec, a.outPerSec, double(a.totalIn), double(a.totalOut) };
        if (!a.isUp)
            r.textColor = RGB(128, 128, 128);
        adapters.push_back(std::move(r));
    }
    m_adapters.SetRows(std::move(adapters));

    std::vector<ListRow> connections;
    connections.reserve(snap.connections.size());
    unsigned established = 0, listening = 0;
    for (const auto& c : snap.connections)
    {
        if (c.state == _T("Established")) ++established;
        if (c.state == _T("Listening"))   ++listening;

        ListRow r;
        r.key = ConnectionKey(c);
        const bool udp = c.protocol.Left(3) == _T("UDP");
        r.text = { c.processName.IsEmpty() ? CString(_T("<unknown>")) : c.processName, fmt::Number(c.pid), c.protocol,
                   c.localAddr, Port(c.localPort),
                   udp ? CString(_T("*")) : c.remoteAddr, udp ? CString(_T("*")) : Port(c.remotePort), c.state };
        r.value = { 0, double(c.pid), 0, 0, double(c.localPort), 0, double(c.remotePort), 0 };
        connections.push_back(std::move(r));
    }
    m_connections.SetRows(std::move(connections));

    CString label;
    label.Format(_T("Active connections: %u  (%u established, %u listening TCP; %u UDP endpoints)"),
                 unsigned(snap.connections.size()), established, listening,
                 unsigned(std::count_if(snap.connections.begin(), snap.connections.end(),
                                        [](const ConnectionInfo& c) { return c.protocol.Left(3) == _T("UDP"); })));
    m_connectionsLabel.SetWindowText(label);
}
