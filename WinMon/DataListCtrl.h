// DataListCtrl.h : CMFCListCtrl that keeps rows keyed by a stable id, updates them
// in place (keeping selection and scroll position) and sorts text/numeric columns.
#pragma once

struct ListRow
{
    ULONGLONG             key = 0;     // stable identity (PID, LUID, hash, ...)
    std::vector<CString>  text;        // one entry per column
    std::vector<double>   value;       // sort value per column (numeric columns only)
    COLORREF              textColor = CLR_DEFAULT;
};

class CDataListCtrl : public CMFCListCtrl
{
public:
    struct Column
    {
        LPCTSTR title;
        int     width;
        bool    numeric;
        int     format = LVCFMT_LEFT;
    };

    BOOL CreateList(CWnd* pParent, UINT nID, DWORD extraStyle = 0);
    void SetColumns(const std::vector<Column>& columns);
    void SetRows(std::vector<ListRow>&& rows);

    bool       GetSelectedKey(ULONGLONG& key) const;
    const ListRow* FindRow(ULONGLONG key) const;
    void       SetDefaultSort(int column, bool ascending);

protected:
    int      OnCompareItems(LPARAM lParam1, LPARAM lParam2, int iColumn) override;
    COLORREF OnGetCellTextColor(int nRow, int nColumn) override;

private:
    std::vector<Column>                       m_columns;
    std::unordered_map<ULONGLONG, ListRow>    m_rows;
};
