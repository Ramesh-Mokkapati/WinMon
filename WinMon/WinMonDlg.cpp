// WinMonDlg.cpp
#include "pch.h"
#include "WinMon.h"
#include "WinMonDlg.h"
#include "Format.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
    enum { IDC_TABS = 1100, IDC_STATUS, IDC_PANEL_PERF, IDC_PANEL_PROC, IDC_PANEL_NET, IDC_PANEL_DISK };

    struct SpeedOption { UINT id; DWORD ms; };
    const SpeedOption kSpeeds[] = {
        { ID_SPEED_HIGH,    500 },
        { ID_SPEED_NORMAL,  1000 },
        { ID_SPEED_LOW,     2000 },
        { ID_SPEED_VERYLOW, 5000 },
    };
}

// ---------------------------------------------------------------------------
// About box

class CAboutDlg : public CDialogEx
{
public:
    CAboutDlg() : CDialogEx(IDD_ABOUTBOX) {}
};

// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CWinMonDlg, CDialogEx)
    ON_WM_CLOSE()
    ON_WM_DESTROY()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_MESSAGE(WM_UPDATE_SYS_METRICS, &CWinMonDlg::OnUpdateSysMetrics)
    ON_COMMAND(ID_FILE_EXIT, &CWinMonDlg::OnFileExit)
    ON_COMMAND(ID_VIEW_REFRESH, &CWinMonDlg::OnViewRefresh)
    ON_COMMAND(ID_OPTIONS_ALWAYSONTOP, &CWinMonDlg::OnAlwaysOnTop)
    ON_COMMAND_RANGE(ID_SPEED_HIGH, ID_SPEED_VERYLOW, &CWinMonDlg::OnSpeed)
    ON_COMMAND(ID_HELP_ABOUTWINMON, &CWinMonDlg::OnHelpAbout)
END_MESSAGE_MAP()

CWinMonDlg::CWinMonDlg(CWnd* pParent)
    : CDialogEx(IDD_WINMON_DIALOG, pParent)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

BOOL CWinMonDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);
    m_hAccel = LoadAccelerators(AfxGetResourceHandle(), MAKEINTRESOURCE(IDR_ACCELERATORS));

    CString title;
    title.Format(_T("WinMon - System Monitor%s"), theApp.IsElevated() ? _T(" (Administrator)") : _T(""));
    SetWindowText(title);

    // UI font: the system message font (Segoe UI on Windows 10/11) at the current DPI.
    NONCLIENTMETRICS ncm = { sizeof(ncm) };
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    m_font.CreateFontIndirect(&ncm.lfMessageFont);
    SetFont(&m_font);

    m_tabs.Create(CMFCTabCtrl::STYLE_3D_ONENOTE, CRect(0, 0, 10, 10), this, IDC_TABS, CMFCTabCtrl::LOCATION_TOP);
    m_tabs.SetFont(&m_font);
    m_tabs.EnableTabSwap(FALSE);
    m_tabs.AutoDestroyWindow(FALSE);   // panels are members of this dialog

    // Tab pages are children of the tab control, as CMFCTabCtrl requires.
    m_perfPanel.CreatePanel(&m_tabs, IDC_PANEL_PERF, &m_font, &m_engine);
    m_processPanel.CreatePanel(&m_tabs, IDC_PANEL_PROC, &m_font, &m_engine);
    m_networkPanel.CreatePanel(&m_tabs, IDC_PANEL_NET, &m_font, &m_engine);
    m_volumePanel.CreatePanel(&m_tabs, IDC_PANEL_DISK, &m_font, &m_engine);

    m_tabs.AddTab(&m_perfPanel, _T("Performance"), (UINT)-1, FALSE);
    m_tabs.AddTab(&m_processPanel, _T("Processes"), (UINT)-1, FALSE);
    m_tabs.AddTab(&m_networkPanel, _T("Network && Connections"), (UINT)-1, FALSE);
    m_tabs.AddTab(&m_volumePanel, _T("Disk Volumes"), (UINT)-1, FALSE);
    m_tabs.SetActiveTab(0);

    m_status.Create(_T("Starting monitor..."), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS | SS_SUNKEN,
                    CRect(0, 0, 10, 10), this, IDC_STATUS);
    m_status.SetFont(&m_font);

    // Initial size: 1200 x 800 at 96 DPI, capped to the work area, centred.
    CRect rc(0, 0, CPanel::Dpi(1200), CPanel::Dpi(800));
    CalcWindowRect(&rc);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(MonitorFromWindow(GetSafeHwnd(), MONITOR_DEFAULTTONEAREST), &mi);
    const CRect work(mi.rcWork);
    SetWindowPos(nullptr, 0, 0, min(rc.Width(), work.Width() * 95 / 100), min(rc.Height(), work.Height() * 95 / 100),
                 SWP_NOMOVE | SWP_NOZORDER);
    CenterWindow();
    LayoutControls();

    UpdateSpeedMenu();
    if (!m_engine.Start(GetSafeHwnd()))
        AfxMessageBox(_T("Failed to start the monitoring thread."), MB_ICONERROR);

    return TRUE;
}

BOOL CWinMonDlg::PreTranslateMessage(MSG* pMsg)
{
    if (m_hAccel && pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST &&
        ::TranslateAccelerator(GetSafeHwnd(), m_hAccel, pMsg))
        return TRUE;
    return CDialogEx::PreTranslateMessage(pMsg);
}

void CWinMonDlg::OnClose()
{
    m_engine.Stop();
    EndDialog(IDCANCEL);
}

void CWinMonDlg::OnDestroy()
{
    // Make sure the worker never posts to a destroyed window.
    m_engine.Stop();
    CDialogEx::OnDestroy();
}

void CWinMonDlg::OnSize(UINT nType, int cx, int cy)
{
    CDialogEx::OnSize(nType, cx, cy);
    if (nType != SIZE_MINIMIZED)
        LayoutControls();
}

void CWinMonDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
    lpMMI->ptMinTrackSize.x = CPanel::Dpi(760);
    lpMMI->ptMinTrackSize.y = CPanel::Dpi(520);
    CDialogEx::OnGetMinMaxInfo(lpMMI);
}

void CWinMonDlg::LayoutControls()
{
    if (!m_tabs.GetSafeHwnd() || !m_status.GetSafeHwnd())
        return;
    CRect rc;
    GetClientRect(&rc);
    const int statusH = CPanel::Dpi(22);
    m_tabs.MoveWindow(0, 0, rc.Width(), rc.Height() - statusH);
    m_status.MoveWindow(0, rc.Height() - statusH, rc.Width(), statusH);
}

LRESULT CWinMonDlg::OnUpdateSysMetrics(WPARAM, LPARAM)
{
    // Drop stale notifications that queued up behind a modal box: one snapshot is enough.
    MSG msg;
    while (::PeekMessage(&msg, GetSafeHwnd(), WM_UPDATE_SYS_METRICS, WM_UPDATE_SYS_METRICS, PM_REMOVE)) {}

    const SystemSnapshot snap = m_engine.Data().GetSnapshot();

    // The performance graphs must record history even while another tab is shown.
    m_perfPanel.UpdateMetrics(snap);
    m_processPanel.UpdateMetrics(snap);
    m_networkPanel.UpdateMetrics(snap);
    m_volumePanel.UpdateMetrics(snap);

    UpdateStatus(snap);
    return 0;
}

void CWinMonDlg::UpdateStatus(const SystemSnapshot& snap)
{
    CString status;
    status.Format(_T("  Updated %02u:%02u:%02u   |   CPU %.0f %%   |   Memory %u %%   |   Refresh every %.1f s   |   %s"),
                  snap.timestamp.wHour, snap.timestamp.wMinute, snap.timestamp.wSecond,
                  snap.cpu.totalUsage, snap.memory.memoryLoad,
                  m_engine.GetIntervalMs() / 1000.0,
                  theApp.IsElevated() ? _T("Running as administrator")
                                      : _T("Standard user: some process details are unavailable"));
    m_status.SetWindowText(status);
}

void CWinMonDlg::OnFileExit()
{
    PostMessage(WM_CLOSE);
}

void CWinMonDlg::OnViewRefresh()
{
    m_engine.RequestFullRefresh();
}

void CWinMonDlg::OnAlwaysOnTop()
{
    m_alwaysOnTop = !m_alwaysOnTop;
    SetWindowPos(m_alwaysOnTop ? &wndTopMost : &wndNoTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    if (CMenu* pMenu = GetMenu())
        pMenu->CheckMenuItem(ID_OPTIONS_ALWAYSONTOP, MF_BYCOMMAND | (m_alwaysOnTop ? MF_CHECKED : MF_UNCHECKED));
}

void CWinMonDlg::OnSpeed(UINT nID)
{
    for (const auto& s : kSpeeds)
        if (s.id == nID)
            m_engine.SetIntervalMs(s.ms);
    UpdateSpeedMenu();
}

void CWinMonDlg::UpdateSpeedMenu()
{
    CMenu* pMenu = GetMenu();
    if (!pMenu)
        return;
    for (const auto& s : kSpeeds)
        if (s.ms == m_engine.GetIntervalMs())
            pMenu->CheckMenuRadioItem(ID_SPEED_HIGH, ID_SPEED_VERYLOW, s.id, MF_BYCOMMAND);
}

void CWinMonDlg::OnHelpAbout()
{
    CAboutDlg dlg;
    dlg.DoModal();
}
