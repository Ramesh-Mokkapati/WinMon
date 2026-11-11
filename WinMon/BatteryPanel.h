// BatteryPanel.h : Tab 5 - power source, battery charge, health and telemetry
#pragma once
#include "Panel.h"
#include "DataListCtrl.h"

class CBatteryPanel : public CPanel
{
public:
    void UpdateMetrics(const SystemSnapshot& snap) override;

protected:
    BOOL CreateControls() override;
    void Layout(int cx, int cy) override;

private:
    CStatic       m_summary;
    CStatic       m_chargeLabel;
    CProgressCtrl m_chargeBar;
    CStatic       m_chargeValue;
    CStatic       m_detailsLabel;
    CDataListCtrl m_details;
    CFont         m_boldFont;
    ULONGLONG     m_lastSeq = 0;
};
