// PerformancePanel.h : Tab 1 - CPU / memory / disk / network graphs and usage bars
#pragma once
#include "Panel.h"
#include "GraphCtrl.h"

class CPerformancePanel : public CPanel
{
public:
    void UpdateMetrics(const SystemSnapshot& snap) override;

protected:
    BOOL CreateControls() override;
    void Layout(int cx, int cy) override;

private:
    struct UsageBar
    {
        CStatic       label;
        CProgressCtrl bar;
        CStatic       value;
    };

    BOOL CreateBar(UsageBar& bar, LPCTSTR text);
    static void SetBar(UsageBar& bar, double percent, const CString& text);

    CStatic    m_info;
    CGraphCtrl m_cpuGraph;
    CGraphCtrl m_memGraph;
    CGraphCtrl m_diskGraph;
    CGraphCtrl m_netGraph;
    UsageBar   m_ramBar;
    UsageBar   m_commitBar;
    UsageBar   m_driveBar;
    CString    m_systemRoot;      // e.g. "C:\"
    ULONGLONG  m_lastSample = 0;
};
