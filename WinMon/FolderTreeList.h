// FolderTreeList.h : tree-style, sortable folder size view (owner-data CMFCListCtrl)
#pragma once
#include "Metrics.h"
#include "FolderScanner.h"

// Sent to the parent whenever scan state or progress changes.
#define WM_FOLDER_SCAN_UPDATE (WM_APP + 20)

class CFolderTreeList : public CMFCListCtrl
{
public:
    struct ScanStatus
    {
        bool         running = false;
        std::wstring root;
        CString      text;
        int          percent = -1;   // -1 when not scanning
    };

    BOOL CreateTree(CWnd* pParent, UINT nID);

    // Keeps one top-level row per fixed volume.
    void SetDrives(const std::vector<VolumeInfo>& volumes);

    // Scans the drive of the selected row (or the first drive); returns false if one is already running.
    bool ScanSelectedDrive();
    bool ScanDrive(const std::wstring& root);
    void CancelScan();
    ScanStatus GetScanStatus() const;

    void Sort(int iColumn, BOOL bAscending = TRUE, BOOL bAdd = FALSE) override;

protected:
    COLORREF OnGetCellTextColor(int nRow, int nColumn) override;

    afx_msg void OnGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
    afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnDestroy();
    DECLARE_MESSAGE_MAP()

private:
    struct Drive
    {
        std::wstring                    root;       // "C:\"
        CString                         label;
        ULONGLONG                       totalBytes = 0;
        ULONGLONG                       usedBytes = 0;
        std::unique_ptr<FolderNode>     tree;       // placeholder until scanned
        std::unique_ptr<CFolderScanner> scanner;    // set while scanning
        bool                            scanned = false;
    };

    struct Row
    {
        FolderNode* node;
        Drive*      drive;
        int         depth;
    };

    void   RebuildRows(FolderNode* keepSelected = nullptr);
    void   AddRows(FolderNode* node, Drive* drive, int depth);
    void   SortChildren(FolderNode& node);
    void   Toggle(int row);
    void   Expand(int row, bool expand);
    void   SelectRow(int row);
    int    SelectedRow() const;
    bool   CanExpand(const Row& r) const;
    void   CompleteScans();
    CString CellText(const Row& r, int column) const;
    Drive* FindDrive(const std::wstring& root);
    void   NotifyParent();
    void   BuildImages();

    std::vector<std::unique_ptr<Drive>> m_drives;
    std::vector<Row>                    m_rows;
    CImageList                          m_images;
    int                                 m_sortStamp = 0;
    bool                                m_timerRunning = false;
    CString                             m_lastSummary;   // result line of the last finished scan
};
