// VolumePanel.cpp
#include "pch.h"
#include "VolumePanel.h"
#include "Format.h"

namespace
{
    enum { IDC_VOLUME_LIST = 4001, IDC_SCAN, IDC_STOP, IDC_FOLDER_TREE };
}

BEGIN_MESSAGE_MAP(CVolumePanel, CPanel)
    ON_BN_CLICKED(IDC_SCAN, &CVolumePanel::OnScan)
    ON_BN_CLICKED(IDC_STOP, &CVolumePanel::OnStop)
    ON_NOTIFY(NM_DBLCLK, IDC_VOLUME_LIST, &CVolumePanel::OnVolumeDblClk)
    ON_MESSAGE(WM_FOLDER_SCAN_UPDATE, &CVolumePanel::OnScanUpdate)
END_MESSAGE_MAP()

BOOL CVolumePanel::CreateControls()
{
    if (!CreateLabel(m_summary, _T("Fixed volumes")) ||
        !m_volumes.CreateList(this, IDC_VOLUME_LIST) ||
        !CreateLabel(m_foldersLabel, _T("Folder sizes")) ||
        !m_scan.Create(_T("&Scan"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this, IDC_SCAN) ||
        !m_stop.Create(_T("S&top"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this, IDC_STOP) ||
        !m_progress.Create(WS_CHILD | PBS_SMOOTH, CRect(0, 0, 10, 10), this, IDC_STATIC) ||
        !CreateLabel(m_scanStatus, _T("")) ||
        !m_tree.CreateTree(this, IDC_FOLDER_TREE))
        return FALSE;

    m_progress.SetRange(0, 100);

    const int R = LVCFMT_RIGHT;
    m_volumes.SetColumns({
        { _T("Drive"),       Dpi(60),  false },
        { _T("Label"),       Dpi(180), false },
        { _T("File System"), Dpi(90),  false },
        { _T("Capacity"),    Dpi(100), true, R },
        { _T("Used"),        Dpi(100), true, R },
        { _T("Free"),        Dpi(100), true, R },
        { _T("% Used"),      Dpi(70),  true, R },
        { _T("Usage"),       Dpi(260), true },
    });
    m_volumes.SetDefaultSort(0, true);

    OnScanUpdate(0, 0);
    return TRUE;
}

void CVolumePanel::Layout(int cx, int cy)
{
    if (!m_boldFont.GetSafeHandle() && m_pFont)
    {
        LOGFONT lf = {};
        m_pFont->GetLogFont(&lf);
        lf.lfWeight = FW_BOLD;
        m_boldFont.CreateFontIndirect(&lf);
        m_foldersLabel.SetFont(&m_boldFont, FALSE);
    }

    const int m = Dpi(8), gap = Dpi(6), labelH = Dpi(22), rowH = Dpi(24);

    m_summary.MoveWindow(m, m, cx - 2 * m, labelH - Dpi(4));

    // Volumes list: tall enough for its rows, but leave most of the space to the folder tree.
    const int listRows = m_volumeCount ? int(m_volumeCount) : 1;
    const int volumesH = min(Dpi(26) + listRows * Dpi(24) + Dpi(6), max(Dpi(80), cy / 4));
    int y = m + labelH;
    m_volumes.MoveWindow(m, y, cx - 2 * m, volumesH);
    y += volumesH + Dpi(10);

    int x = m;
    m_foldersLabel.MoveWindow(x, y + Dpi(4), Dpi(90), rowH - Dpi(4));
    x += Dpi(90) + gap;
    m_scan.MoveWindow(x, y, Dpi(80), rowH);
    x += Dpi(80) + gap;
    m_stop.MoveWindow(x, y, Dpi(80), rowH);
    x += Dpi(80) + gap;
    m_progress.MoveWindow(x, y + Dpi(5), Dpi(140), rowH - Dpi(10));
    const int statusX = x + Dpi(140) + gap;
    m_scanStatus.MoveWindow(statusX, y + Dpi(4), max(cx - m - statusX, 0), rowH - Dpi(4));
    y += rowH + gap;

    m_tree.MoveWindow(m, y, cx - 2 * m, max(cy - y - m, 0));
}

void CVolumePanel::UpdateMetrics(const SystemSnapshot& snap)
{
    if (snap.volumeSeq == m_lastSeq)
        return;
    m_lastSeq = snap.volumeSeq;

    ULONGLONG total = 0, freeBytes = 0;
    std::vector<ListRow> rows;
    for (const auto& v : snap.volumes)
    {
        const ULONGLONG used = v.totalBytes - v.freeBytes;
        const double pct = v.totalBytes ? 100.0 * double(used) / double(v.totalBytes) : 0.0;
        total += v.totalBytes;
        freeBytes += v.freeBytes;

        // Text bar so usage is visible at a glance without owner-drawing the list.
        const int filled = int(pct / 5.0 + 0.5);
        CString bar(_T('\x2588'), filled);
        bar += CString(_T('\x2591'), 20 - filled);

        ListRow r;
        r.key = v.root.IsEmpty() ? 0 : ULONGLONG(v.root[0]);
        r.text = { v.root, v.label, v.fileSystem, fmt::Bytes(double(v.totalBytes)), fmt::Bytes(double(used)),
                   fmt::Bytes(double(v.freeBytes)), fmt::Percent(pct), bar };
        r.value = { 0, 0, 0, double(v.totalBytes), double(used), double(v.freeBytes), pct, pct };
        if (pct >= 90.0)
            r.textColor = RGB(200, 0, 0);
        rows.push_back(std::move(r));
    }
    m_volumes.SetRows(std::move(rows));
    m_tree.SetDrives(snap.volumes);

    if (snap.volumes.size() != m_volumeCount)
    {
        m_volumeCount = snap.volumes.size();
        CRect rc;
        GetClientRect(&rc);
        Layout(rc.Width(), rc.Height());
    }

    CString summary;
    summary.Format(_T("%u fixed volume%s  |  Total capacity: %s  |  Free: %s   (double-click a volume to scan its folders)"),
                   unsigned(snap.volumes.size()), snap.volumes.size() == 1 ? _T("") : _T("s"), fmt::Bytes(double(total)).GetString(), fmt::Bytes(double(freeBytes)).GetString());
    m_summary.SetWindowText(summary);
}

void CVolumePanel::OnScan()
{
    if (!m_tree.ScanSelectedDrive())
        AfxMessageBox(_T("A scan is already running. Wait for it to finish or press Stop."), MB_ICONINFORMATION);
}

void CVolumePanel::OnStop()
{
    m_tree.CancelScan();
}

void CVolumePanel::OnVolumeDblClk(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    const int item = reinterpret_cast<NMITEMACTIVATE*>(pNMHDR)->iItem;
    if (item < 0)
        return;
    const CString root = m_volumes.GetItemText(item, 0);
    if (!m_tree.ScanDrive(std::wstring(root.GetString())))
        AfxMessageBox(_T("A scan is already running. Wait for it to finish or press Stop."), MB_ICONINFORMATION);
}

LRESULT CVolumePanel::OnScanUpdate(WPARAM, LPARAM)
{
    const auto status = m_tree.GetScanStatus();

    m_scan.EnableWindow(!status.running);
    m_stop.EnableWindow(status.running);
    m_progress.ShowWindow(status.running ? SW_SHOW : SW_HIDE);
    if (status.running)
        m_progress.SetPos(max(status.percent, 0));

    CString old;
    m_scanStatus.GetWindowText(old);
    if (old != status.text)
        m_scanStatus.SetWindowText(status.text);
    return 0;
}
