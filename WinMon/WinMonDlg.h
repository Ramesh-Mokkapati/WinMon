// WinMonDlg.h : main window (dialog-based) hosting the tab pages
#pragma once
#include "MonitorEngine.h"
#include "PerformancePanel.h"
#include "ProcessPanel.h"
#include "NetworkPanel.h"
#include "VolumePanel.h"

class CWinMonDlg : public CDialogEx
{
public:
    explicit CWinMonDlg(CWnd* pParent = nullptr);

    enum { IDD = IDD_WINMON_DIALOG };

protected:
    BOOL OnInitDialog() override;
    BOOL PreTranslateMessage(MSG* pMsg) override;
    void OnOK() override {}       // Enter must not close the monitor
    void OnCancel() override {}   // nor Escape; closing goes through WM_CLOSE

    afx_msg void OnClose();
    afx_msg void OnDestroy();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
    afx_msg LRESULT OnUpdateSysMetrics(WPARAM wParam, LPARAM lParam);
    afx_msg void OnFileExit();
    afx_msg void OnViewRefresh();
    afx_msg void OnAlwaysOnTop();
    afx_msg void OnSpeed(UINT nID);
    afx_msg void OnHelpAbout();
    DECLARE_MESSAGE_MAP()

private:
    void LayoutControls();
    void UpdateSpeedMenu();
    void UpdateStatus(const SystemSnapshot& snap);

    HICON          m_hIcon = nullptr;
    HACCEL         m_hAccel = nullptr;
    CFont          m_font;
    CMFCTabCtrl    m_tabs;
    CStatic        m_status;

    CPerformancePanel m_perfPanel;
    CProcessPanel     m_processPanel;
    CNetworkPanel     m_networkPanel;
    CVolumePanel      m_volumePanel;

    CMonitorEngine m_engine;
    bool           m_alwaysOnTop = false;
};
