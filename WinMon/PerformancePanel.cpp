// PerformancePanel.cpp
#include "pch.h"
#include "PerformancePanel.h"
#include "Format.h"

BOOL CPerformancePanel::CreateControls()
{
    WCHAR winDir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(winDir, _countof(winDir)) >= 3)
        m_systemRoot = CString(winDir).Left(3);

    if (!CreateLabel(m_info, _T("Collecting system information..."), IDC_STATIC, SS_LEFT, true))
        return FALSE;

    const CRect r(0, 0, 10, 10);
    if (!m_cpuGraph.Create(r, this, 1, _T("CPU"), CGraphCtrl::Scale::Percent) ||
        !m_memGraph.Create(r, this, 2, _T("Memory"), CGraphCtrl::Scale::Percent) ||
        !m_diskGraph.Create(r, this, 3, _T("Disk I/O"), CGraphCtrl::Scale::AutoBytes) ||
        !m_netGraph.Create(r, this, 4, _T("Network"), CGraphCtrl::Scale::AutoBytes))
        return FALSE;

    m_cpuGraph.SetSeries(0, _T("Utilization"), RGB(0, 255, 0));
    m_memGraph.SetSeries(0, _T("In use"), RGB(180, 120, 255));
    m_diskGraph.SetSeries(0, _T("Read"), RGB(0, 200, 255));
    m_diskGraph.SetSeries(1, _T("Write"), RGB(255, 160, 0));
    m_netGraph.SetSeries(0, _T("Receive"), RGB(255, 100, 180));
    m_netGraph.SetSeries(1, _T("Send"), RGB(255, 230, 0));

    CString driveLabel;
    driveLabel.Format(_T("System drive (%s)"), m_systemRoot.GetString());
    return CreateBar(m_ramBar, _T("Physical memory")) &&
           CreateBar(m_commitBar, _T("Committed memory")) &&
           CreateBar(m_driveBar, driveLabel);
}

BOOL CPerformancePanel::CreateBar(UsageBar& bar, LPCTSTR text)
{
    if (!CreateLabel(bar.label, text) || !CreateLabel(bar.value, _T("")))
        return FALSE;
    if (!bar.bar.Create(WS_CHILD | WS_VISIBLE | PBS_SMOOTH, CRect(0, 0, 10, 10), this, IDC_STATIC))
        return FALSE;
    bar.bar.SetRange(0, 1000);
    return TRUE;
}

void CPerformancePanel::SetBar(UsageBar& bar, double percent, const CString& text)
{
    const int pos = int(min(max(percent, 0.0), 100.0) * 10.0);
    // The themed progress bar animates towards the target; stepping past it and back
    // makes it jump straight to the value.
    if (pos < 1000)
    {
        bar.bar.SetPos(pos + 1);
        bar.bar.SetPos(pos);
    }
    else
    {
        bar.bar.SetRange(0, 1001);
        bar.bar.SetPos(1001);
        bar.bar.SetRange(0, 1000);
    }
    bar.bar.SendMessage(PBM_SETSTATE, percent >= 90.0 ? PBST_ERROR : percent >= 75.0 ? PBST_PAUSED : PBST_NORMAL);

    CString old;
    bar.value.GetWindowText(old);
    if (old != text)
        bar.value.SetWindowText(text);
}

void CPerformancePanel::Layout(int cx, int cy)
{
    const int m = Dpi(10), gap = Dpi(8);
    const int infoH = Dpi(36), rowH = Dpi(22), labelW = Dpi(150), valueW = Dpi(260);

    m_info.MoveWindow(m, m, cx - 2 * m, infoH);

    const int barsH = 3 * rowH + 2 * Dpi(4);
    const int graphTop = m + infoH + gap;
    const int graphBottom = cy - m - barsH - gap;
    const int gw = (cx - 2 * m - gap) / 2;
    const int gh = max((graphBottom - graphTop - gap) / 2, Dpi(40));

    m_cpuGraph.MoveWindow(m, graphTop, gw, gh);
    m_memGraph.MoveWindow(m + gw + gap, graphTop, cx - 2 * m - gw - gap, gh);
    m_diskGraph.MoveWindow(m, graphTop + gh + gap, gw, gh);
    m_netGraph.MoveWindow(m + gw + gap, graphTop + gh + gap, cx - 2 * m - gw - gap, gh);

    int y = graphTop + 2 * gh + 2 * gap;
    for (UsageBar* bar : { &m_ramBar, &m_commitBar, &m_driveBar })
    {
        const int barW = max(cx - 2 * m - labelW - valueW - 2 * gap, Dpi(60));
        bar->label.MoveWindow(m, y + Dpi(3), labelW, rowH - Dpi(3));
        bar->bar.MoveWindow(m + labelW + gap, y + Dpi(2), barW, rowH - Dpi(6));
        bar->value.MoveWindow(m + labelW + gap + barW + gap, y + Dpi(3), valueW, rowH - Dpi(3));
        y += rowH + Dpi(4);
    }
}

void CPerformancePanel::UpdateMetrics(const SystemSnapshot& snap)
{
    if (snap.sampleSeq == m_lastSample)
        return;
    m_lastSample = snap.sampleSeq;

    const double gb = 1024.0 * 1024.0 * 1024.0;
    const MemoryMetrics& mem = snap.memory;
    const ULONGLONG usedPhys = mem.totalPhys - mem.availPhys;
    const ULONGLONG usedCommit = mem.totalPageFile - mem.availPageFile;
    const double physPct = mem.totalPhys ? 100.0 * double(usedPhys) / double(mem.totalPhys) : 0.0;
    const double commitPct = mem.totalPageFile ? 100.0 * double(usedCommit) / double(mem.totalPageFile) : 0.0;

    // Graphs
    m_cpuGraph.AddValues(snap.cpu.totalUsage);
    m_cpuGraph.SetCaption(fmt::Percent(snap.cpu.totalUsage));

    m_memGraph.AddValues(physPct);
    CString memCap;
    memCap.Format(_T("%.1f / %.1f GB  (%u %%)"), usedPhys / gb, mem.totalPhys / gb, mem.memoryLoad);
    m_memGraph.SetCaption(memCap);

    m_diskGraph.AddValues(snap.diskIo.readBytesPerSec, snap.diskIo.writeBytesPerSec);
    m_diskGraph.SetCaption(snap.diskIo.available
        ? _T("R ") + fmt::Rate(snap.diskIo.readBytesPerSec) + _T("   W ") + fmt::Rate(snap.diskIo.writeBytesPerSec)
        : CString(_T("Counters unavailable")));

    m_netGraph.AddValues(snap.netInPerSec, snap.netOutPerSec);
    m_netGraph.SetCaption(_T("Rx ") + fmt::Rate(snap.netInPerSec) + _T("   Tx ") + fmt::Rate(snap.netOutPerSec));

    // Info line
    CString info;
    info.Format(_T("%s  |  %u logical processors\r\nProcesses: %u    Threads: %u    Handles: %u    Up time: %s"),
        snap.cpu.processorName.GetString(), snap.cpu.logicalProcessors,
        snap.summary.processCount, snap.summary.threadCount, snap.summary.handleCount,
        fmt::Duration(snap.summary.uptimeMs).GetString());
    CString oldInfo;
    m_info.GetWindowText(oldInfo);
    if (oldInfo != info)
        m_info.SetWindowText(info);

    // Usage bars
    CString text;
    text.Format(_T("%s used of %s  (%.0f %%)"), fmt::Bytes(double(usedPhys)).GetString(), fmt::Bytes(double(mem.totalPhys)).GetString(), physPct);
    SetBar(m_ramBar, physPct, text);

    text.Format(_T("%s used of %s  (%.0f %%)"), fmt::Bytes(double(usedCommit)).GetString(), fmt::Bytes(double(mem.totalPageFile)).GetString(), commitPct);
    SetBar(m_commitBar, commitPct, text);

    for (const auto& v : snap.volumes)
    {
        if (v.root.CompareNoCase(m_systemRoot) != 0 || v.totalBytes == 0)
            continue;
        const ULONGLONG used = v.totalBytes - v.freeBytes;
        const double pct = 100.0 * double(used) / double(v.totalBytes);
        text.Format(_T("%s free of %s  (%.0f %% used)"), fmt::Bytes(double(v.freeBytes)).GetString(), fmt::Bytes(double(v.totalBytes)).GetString(), pct);
        SetBar(m_driveBar, pct, text);
    }
}
