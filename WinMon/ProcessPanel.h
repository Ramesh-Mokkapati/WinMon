// ProcessPanel.h : Tab 2 - interactive process manager with module view
#pragma once
#include "Panel.h"
#include "DataListCtrl.h"

class CProcessPanel : public CPanel
{
public:
    void UpdateMetrics(const SystemSnapshot& snap) override;

protected:
    BOOL CreateControls() override;
    void Layout(int cx, int cy) override;

    afx_msg void OnFilterChanged();
    afx_msg void OnEndTask();
    afx_msg void OnProcessItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnProcessRightClick(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnProcessKeyDown(NMHDR* pNMHDR, LRESULT* pResult);
    DECLARE_MESSAGE_MAP()

private:
    void RebuildRows();
    void LoadModules(DWORD pid);
    void OpenFileLocation(DWORD pid);
    const ProcessInfo* FindProcess(DWORD pid) const;

    CStatic       m_filterLabel;
    CEdit         m_filter;
    CButton       m_endTask;
    CStatic       m_count;
    CDataListCtrl m_processes;
    CStatic       m_modulesLabel;
    CDataListCtrl m_modules;

    std::vector<ProcessInfo> m_data;
    ULONGLONG     m_lastSeq = 0;
    DWORD         m_modulesPid = MAXDWORD;
};
