// VolumePanel.h : Tab 4 - fixed disk volumes, storage usage and folder sizes
#pragma once
#include "Panel.h"
#include "DataListCtrl.h"
#include "FolderTreeList.h"

class CVolumePanel : public CPanel
{
public:
    void UpdateMetrics(const SystemSnapshot& snap) override;

protected:
    BOOL CreateControls() override;
    void Layout(int cx, int cy) override;

    afx_msg void OnScan();
    afx_msg void OnStop();
    afx_msg void OnVolumeDblClk(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg LRESULT OnScanUpdate(WPARAM, LPARAM);
    DECLARE_MESSAGE_MAP()

private:
    CStatic         m_summary;
    CDataListCtrl   m_volumes;
    CStatic         m_foldersLabel;
    CButton         m_scan;
    CButton         m_stop;
    CProgressCtrl   m_progress;
    CStatic         m_scanStatus;
    CFolderTreeList m_tree;
    CFont           m_boldFont;
    ULONGLONG       m_lastSeq = 0;
    size_t          m_volumeCount = 0;
};
