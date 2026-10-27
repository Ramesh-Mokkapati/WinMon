// Panel.h : base class for the tab pages hosted by CMFCTabCtrl
#pragma once
#include "Metrics.h"

class CMonitorEngine;

class CPanel : public CWnd
{
public:
    BOOL CreatePanel(CWnd* pParent, UINT nID, CFont* pFont, CMonitorEngine* pEngine)
    {
        m_pFont = pFont;
        m_pEngine = pEngine;
        LPCTSTR cls = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW), ::GetSysColorBrush(COLOR_BTNFACE), nullptr);
        if (!CWnd::Create(cls, nullptr, WS_CHILD | WS_CLIPCHILDREN, CRect(0, 0, 10, 10), pParent, nID))
            return FALSE;
        if (!CreateControls())
            return FALSE;
        ApplyFont(this);
        m_controlsCreated = true;
        CRect rc;
        GetClientRect(&rc);
        Layout(rc.Width(), rc.Height());
        return TRUE;
    }

    // Called on the UI thread with the latest snapshot after every worker cycle.
    virtual void UpdateMetrics(const SystemSnapshot& snap) = 0;

    // Scales a 96-DPI pixel value to the current system DPI.
    static int Dpi(int px)
    {
        static const int dpi = [] {
            HDC hdc = ::GetDC(nullptr);
            const int v = ::GetDeviceCaps(hdc, LOGPIXELSY);
            ::ReleaseDC(nullptr, hdc);
            return v;
        }();
        return ::MulDiv(px, dpi, 96);
    }

protected:
    virtual BOOL CreateControls() = 0;
    virtual void Layout(int cx, int cy) = 0;

    void ApplyFont(CWnd* pWnd)
    {
        if (!m_pFont)
            return;
        for (CWnd* pChild = pWnd->GetWindow(GW_CHILD); pChild; pChild = pChild->GetNextWindow())
            pChild->SetFont(m_pFont, FALSE);
    }

    // Single-line label unless multiLine is set (SS_ENDELLIPSIS forces a single line).
    BOOL CreateLabel(CStatic& label, LPCTSTR text, UINT nID = IDC_STATIC, DWORD style = SS_LEFT, bool multiLine = false)
    {
        const DWORD ellipsis = multiLine ? 0 : SS_ENDELLIPSIS;
        return label.Create(text, WS_CHILD | WS_VISIBLE | style | SS_NOPREFIX | ellipsis, CRect(0, 0, 10, 10), this, nID);
    }

    afx_msg void OnSize(UINT nType, int cx, int cy)
    {
        CWnd::OnSize(nType, cx, cy);
        // WM_SIZE arrives during Create(), before the child controls exist.
        if (m_controlsCreated && cx > 0 && cy > 0)
            Layout(cx, cy);
    }

    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
    {
        HBRUSH hbr = CWnd::OnCtlColor(pDC, pWnd, nCtlColor);
        if (nCtlColor == CTLCOLOR_STATIC)
        {
            pDC->SetBkColor(::GetSysColor(COLOR_BTNFACE));
            return ::GetSysColorBrush(COLOR_BTNFACE);
        }
        return hbr;
    }

    DECLARE_MESSAGE_MAP()

    CFont*          m_pFont = nullptr;
    CMonitorEngine* m_pEngine = nullptr;
    bool            m_controlsCreated = false;
};
