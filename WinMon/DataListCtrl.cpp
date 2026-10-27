// DataListCtrl.cpp
#include "pch.h"
#include "DataListCtrl.h"

BOOL CDataListCtrl::CreateList(CWnd* pParent, UINT nID, DWORD extraStyle)
{
    const DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
                        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | extraStyle;
    if (!Create(style, CRect(0, 0, 10, 10), pParent, nID))
        return FALSE;
    SetExtendedStyle(GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);
    return TRUE;
}

void CDataListCtrl::SetColumns(const std::vector<Column>& columns)
{
    m_columns = columns;
    for (int i = 0; i < int(columns.size()); ++i)
        InsertColumn(i, columns[i].title, columns[i].format, columns[i].width);
}

void CDataListCtrl::SetDefaultSort(int column, bool ascending)
{
    Sort(column, ascending ? TRUE : FALSE);
}

void CDataListCtrl::SetRows(std::vector<ListRow>&& rows)
{
    std::unordered_map<ULONGLONG, ListRow> incoming;
    incoming.reserve(rows.size());
    for (auto& r : rows)
        incoming[r.key] = std::move(r);

    SetRedraw(FALSE);

    // Update or remove the rows already shown; deleting from the end keeps indices valid.
    std::unordered_set<ULONGLONG> shown;
    for (int i = GetItemCount() - 1; i >= 0; --i)
    {
        const ULONGLONG key = GetItemData(i);
        auto it = incoming.find(key);
        if (it == incoming.end())
        {
            DeleteItem(i);
            continue;
        }
        shown.insert(key);
        const ListRow& row = it->second;
        for (int c = 0; c < int(row.text.size()); ++c)
            if (GetItemText(i, c) != row.text[c])
                SetItemText(i, c, row.text[c]);
    }

    // Append new rows.
    int next = GetItemCount();
    for (const auto& kv : incoming)
    {
        if (shown.count(kv.first))
            continue;
        const ListRow& row = kv.second;
        const int idx = InsertItem(next++, row.text.empty() ? _T("") : row.text[0].GetString());
        SetItemData(idx, static_cast<DWORD_PTR>(row.key));
        for (int c = 1; c < int(row.text.size()); ++c)
            SetItemText(idx, c, row.text[c]);
    }

    m_rows.swap(incoming);

    if (m_iSortedColumn >= 0)
        Sort(m_iSortedColumn, m_bAscending);

    SetRedraw(TRUE);
    Invalidate(FALSE);
}

bool CDataListCtrl::GetSelectedKey(ULONGLONG& key) const
{
    const int sel = GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0)
        return false;
    key = GetItemData(sel);
    return true;
}

const ListRow* CDataListCtrl::FindRow(ULONGLONG key) const
{
    auto it = m_rows.find(key);
    return it != m_rows.end() ? &it->second : nullptr;
}

int CDataListCtrl::OnCompareItems(LPARAM lParam1, LPARAM lParam2, int iColumn)
{
    const ListRow* a = FindRow(static_cast<ULONGLONG>(lParam1));
    const ListRow* b = FindRow(static_cast<ULONGLONG>(lParam2));
    if (!a || !b || iColumn < 0 || iColumn >= int(m_columns.size()))
        return 0;

    if (m_columns[iColumn].numeric && iColumn < int(a->value.size()) && iColumn < int(b->value.size()))
    {
        const double va = a->value[iColumn], vb = b->value[iColumn];
        if (va != vb)
            return va < vb ? -1 : 1;
    }
    else if (iColumn < int(a->text.size()) && iColumn < int(b->text.size()))
    {
        const int r = a->text[iColumn].CompareNoCase(b->text[iColumn]);
        if (r != 0)
            return r;
    }
    // Stable tie-break so rows don't jump around between refreshes.
    return a->key < b->key ? -1 : a->key > b->key ? 1 : 0;
}

COLORREF CDataListCtrl::OnGetCellTextColor(int nRow, int nColumn)
{
    if (const ListRow* row = FindRow(GetItemData(nRow)))
        if (row->textColor != CLR_DEFAULT)
            return row->textColor;
    return CMFCListCtrl::OnGetCellTextColor(nRow, nColumn);
}
