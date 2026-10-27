// NetworkPanel.h : Tab 3 - network adapters and TCP/UDP connections
#pragma once
#include "Panel.h"
#include "DataListCtrl.h"

class CNetworkPanel : public CPanel
{
public:
    void UpdateMetrics(const SystemSnapshot& snap) override;

protected:
    BOOL CreateControls() override;
    void Layout(int cx, int cy) override;

private:
    CStatic       m_adaptersLabel;
    CDataListCtrl m_adapters;
    CStatic       m_connectionsLabel;
    CDataListCtrl m_connections;
    ULONGLONG     m_lastSeq = 0;
};
