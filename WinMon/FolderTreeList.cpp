// FolderTreeList.cpp
#include "pch.h"
#include "FolderTreeList.h"
#include "Panel.h"
#include "Format.h"
#include "WinMon.h"

namespace
{
    enum Column { colName, colAllocated, colSize, colPercent, colFiles, colFolders, colModified, colCount };

    enum { kTimerId = 1, kTimerMs = 250 };
    enum { imgNone, imgCollapsed, imgExpanded };
    enum { cmdOpen = 1, cmdCopy, cmdToggle, cmdScan, cmdCollapseAll };

    CString FormatDate(ULONGLONG ft)
    {
        if (!ft)
            return CString();
        FILETIME utc = { DWORD(ft), DWORD(ft >> 32) }, local;
        SYSTEMTIME st;
        if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &st))
            return CString();
        CString s;
        s.Format(_T("%04u-%02u-%02u %02u:%02u"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
        return s;
    }

    CString PercentBar(double pct)
    {
        pct = min(max(pct, 0.0), 100.0);
        const int filled = int(pct / 10.0 + 0.5);
        // Number first so a narrow column truncates the bar, not the value.
        CString s;
        s.Format(_T("%5.1f %%   "), pct);
        s += CString(_T('\x2588'), filled);
        s += CString(_T('\x2591'), 10 - filled);
        return s;
    }

    CString Count(ULONGLONG v)
    {
        // Thousands separators make 7-digit file counts readable.
        CString digits = fmt::Number(v), out;
        const int n = digits.GetLength();
        for (int i = 0; i < n; ++i)
        {
            out += digits[i];
            if ((n - i - 1) % 3 == 0 && i != n - 1)
                out += _T(',');
        }
        return out;
    }
}

BEGIN_MESSAGE_MAP(CFolderTreeList, CMFCListCtrl)
    ON_NOTIFY_REFLECT(LVN_GETDISPINFO, &CFolderTreeList::OnGetDispInfo)
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_KEYDOWN()
    ON_WM_CONTEXTMENU()
    ON_WM_TIMER()
    ON_WM_DESTROY()
END_MESSAGE_MAP()

BOOL CFolderTreeList::CreateTree(CWnd* pParent, UINT nID)
{
    const DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
                        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_OWNERDATA;
    if (!Create(style, CRect(0, 0, 10, 10), pParent, nID))
        return FALSE;
    SetExtendedStyle(GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    // The small image list provides the expand arrows and the indent unit.
    BuildImages();
    SetImageList(&m_images, LVSIL_SMALL);

    const int D = CPanel::Dpi(1);
    InsertColumn(colName,      _T("Folder"),        LVCFMT_LEFT,  360 * D);
    InsertColumn(colAllocated, _T("Size on Disk"),  LVCFMT_RIGHT, 115 * D);
    InsertColumn(colSize,      _T("Size"),          LVCFMT_RIGHT, 100 * D);
    InsertColumn(colPercent,   _T("% of Parent"),   LVCFMT_LEFT,  240 * D);
    InsertColumn(colFiles,     _T("Files"),         LVCFMT_RIGHT, 90 * D);
    InsertColumn(colFolders,   _T("Folders"),       LVCFMT_RIGHT, 80 * D);
    InsertColumn(colModified,  _T("Last Modified"), LVCFMT_LEFT,  130 * D);

    Sort(colAllocated, FALSE);   // largest first
    return TRUE;
}

void CFolderTreeList::BuildImages()
{
    const int sz = CPanel::Dpi(16);
    const COLORREF mask = RGB(255, 0, 255), arrow = RGB(96, 96, 96);
    m_images.Create(sz, sz, ILC_COLOR32 | ILC_MASK, 3, 0);

    CClientDC screen(this);
    for (int i = 0; i < 3; ++i)
    {
        CBitmap bmp;
        bmp.CreateCompatibleBitmap(&screen, sz, sz);
        CDC dc;
        dc.CreateCompatibleDC(&screen);
        CBitmap* pOld = dc.SelectObject(&bmp);
        dc.FillSolidRect(0, 0, sz, sz, mask);

        const int c = sz / 2, h = max(sz / 4, 3);
        POINT tri[3];
        if (i == imgCollapsed)
        {
            tri[0] = { c - h / 2, c - h }; tri[1] = { c - h / 2, c + h }; tri[2] = { c + h / 2 + 1, c };
        }
        else
        {
            tri[0] = { c - h, c - h / 2 }; tri[1] = { c + h, c - h / 2 }; tri[2] = { c, c + h / 2 + 1 };
        }
        if (i != imgNone)
        {
            CBrush brush(arrow);
            CPen pen(PS_SOLID, 1, arrow);
            CBrush* pOldBrush = dc.SelectObject(&brush);
            CPen* pOldPen = dc.SelectObject(&pen);
            dc.Polygon(tri, 3);
            dc.SelectObject(pOldPen);
            dc.SelectObject(pOldBrush);
        }
        dc.SelectObject(pOld);
        m_images.Add(&bmp, mask);
    }
}

// ---------------------------------------------------------------------------
// Drives and scanning

CFolderTreeList::Drive* CFolderTreeList::FindDrive(const std::wstring& root)
{
    for (auto& d : m_drives)
        if (_wcsicmp(d->root.c_str(), root.c_str()) == 0)
            return d.get();
    return nullptr;
}

void CFolderTreeList::SetDrives(const std::vector<VolumeInfo>& volumes)
{
    bool changed = false;

    for (const auto& v : volumes)
    {
        const std::wstring root(v.root.GetString());
        Drive* d = FindDrive(root);
        if (!d)
        {
            auto nd = std::make_unique<Drive>();
            nd->root = root;
            nd->tree = std::make_unique<FolderNode>();
            nd->tree->name = root;
            d = nd.get();
            m_drives.push_back(std::move(nd));
            changed = true;
        }
        d->label = v.label;
        d->totalBytes = v.totalBytes;
        d->usedBytes = v.totalBytes - v.freeBytes;
    }

    // Drop drives that disappeared (unless a scan is still running on them).
    for (auto it = m_drives.begin(); it != m_drives.end();)
    {
        const bool present = std::any_of(volumes.begin(), volumes.end(),
            [&](const VolumeInfo& v) { return _wcsicmp(v.root.GetString(), (*it)->root.c_str()) == 0; });
        if (!present && !(*it)->scanner)
        {
            it = m_drives.erase(it);
            changed = true;
        }
        else
            ++it;
    }

    if (changed)
    {
        std::sort(m_drives.begin(), m_drives.end(),
                  [](const auto& a, const auto& b) { return _wcsicmp(a->root.c_str(), b->root.c_str()) < 0; });
        RebuildRows();
    }
    else if (GetSafeHwnd())
    {
        Invalidate(FALSE);
    }
}

bool CFolderTreeList::ScanSelectedDrive()
{
    const int sel = SelectedRow();
    if (sel >= 0)
        return ScanDrive(m_rows[sel].drive->root);
    return !m_drives.empty() && ScanDrive(m_drives.front()->root);
}

bool CFolderTreeList::ScanDrive(const std::wstring& root)
{
    for (const auto& d : m_drives)
        if (d->scanner)
            return false;   // one scan at a time keeps the disk from thrashing

    Drive* d = FindDrive(root);
    if (!d)
        return false;

    d->scanner = std::make_unique<CFolderScanner>();
    d->scanner->Start(root);

    if (!m_timerRunning)
    {
        SetTimer(kTimerId, kTimerMs, nullptr);
        m_timerRunning = true;
    }
    Invalidate(FALSE);
    NotifyParent();
    return true;
}

void CFolderTreeList::CancelScan()
{
    for (auto& d : m_drives)
        if (d->scanner)
        {
            d->scanner->Cancel();
            d->scanner.reset();
            m_lastSummary.Format(_T("Scan of %s was stopped."), d->root.c_str());
        }
    Invalidate(FALSE);
    NotifyParent();
}

void CFolderTreeList::CompleteScans()
{
    bool anyRunning = false;
    for (auto& d : m_drives)
    {
        if (!d->scanner)
            continue;
        if (!d->scanner->IsDone())
        {
            anyRunning = true;
            continue;
        }

        const ULONGLONG ms = d->scanner->ElapsedMs();
        if (!d->scanner->IsCancelled())
        {
            const bool wasExpanded = d->tree && d->tree->expanded;
            d->tree = d->scanner->TakeResult();
            d->tree->expanded = wasExpanded || !d->scanned;
            d->scanned = true;

            const FolderNode& t = *d->tree;
            m_lastSummary.Format(_T("%s scanned in %.1f s: %s files in %s folders, %s on disk."),
                                 d->root.c_str(), ms / 1000.0, Count(t.files).GetString(), Count(t.folders).GetString(),
                                 fmt::Bytes(double(t.allocated)).GetString());
            if (t.deniedFolders)
            {
                CString denied;
                denied.Format(_T("  %s folders could not be read%s."), Count(t.deniedFolders).GetString(),
                              theApp.IsElevated() ? _T("") : _T(" (run as administrator for complete totals)"));
                m_lastSummary += denied;
            }
            d->scanner.reset();
            RebuildRows(d->tree.get());
        }
        else
        {
            d->scanner.reset();
        }
    }

    if (!anyRunning && m_timerRunning)
    {
        KillTimer(kTimerId);
        m_timerRunning = false;
    }
}

CFolderTreeList::ScanStatus CFolderTreeList::GetScanStatus() const
{
    ScanStatus s;
    for (const auto& d : m_drives)
    {
        if (!d->scanner)
            continue;
        const auto p = d->scanner->GetProgress();
        s.running = true;
        s.root = d->root;
        s.percent = d->usedBytes ? int(min(99.0, 100.0 * double(p.allocated) / double(d->usedBytes))) : 0;
        s.text.Format(_T("Scanning %s  %s files, %s folders, %s  |  %s"), d->root.c_str(),
                      Count(p.files).GetString(), Count(p.folders).GetString(),
                      fmt::Bytes(double(p.allocated)).GetString(), p.currentPath.c_str());
        return s;
    }
    s.text = m_lastSummary.IsEmpty()
        ? CString(_T("Expand a drive, or select it and press Scan, to measure its folders."))
        : m_lastSummary;
    return s;
}

void CFolderTreeList::NotifyParent()
{
    if (CWnd* pParent = GetParent())
        pParent->SendMessage(WM_FOLDER_SCAN_UPDATE);
}

void CFolderTreeList::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent != kTimerId)
    {
        CMFCListCtrl::OnTimer(nIDEvent);
        return;
    }
    CompleteScans();
    Invalidate(FALSE);   // drive rows show live progress
    NotifyParent();
}

void CFolderTreeList::OnDestroy()
{
    if (m_timerRunning)
        KillTimer(kTimerId);
    m_timerRunning = false;
    for (auto& d : m_drives)
        d->scanner.reset();   // cancels and joins the worker threads
    CMFCListCtrl::OnDestroy();
}

// ---------------------------------------------------------------------------
// Rows

void CFolderTreeList::Sort(int iColumn, BOOL bAscending, BOOL /*bAdd*/)
{
    // Sizes and counts are most useful largest-first, so a newly chosen numeric
    // column starts descending; clicking it again toggles the direction.
    if (iColumn != m_iSortedColumn && iColumn != colName)
        bAscending = FALSE;

    GetHeaderCtrl().SetSortColumn(iColumn, bAscending, FALSE);
    m_iSortedColumn = iColumn;
    m_bAscending = bAscending;
    ++m_sortStamp;
    RebuildRows();
}

void CFolderTreeList::SortChildren(FolderNode& node)
{
    if (node.sortStamp == m_sortStamp)
        return;
    node.sortStamp = m_sortStamp;

    const int col = m_iSortedColumn;
    const bool asc = m_bAscending != FALSE;

    auto key = [col](const FolderNode& n) -> double {
        switch (col)
        {
        case colSize:     return double(n.size);
        case colFiles:    return double(n.files);
        case colFolders:  return double(n.folders);
        case colModified: return double(n.lastWrite);
        default:          return double(n.allocated);   // size on disk and % of parent
        }
    };
    auto byName = [](const FolderNode& a, const FolderNode& b) {
        return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS,
                               a.name.c_str(), -1, b.name.c_str(), -1, nullptr, nullptr, 0) - CSTR_EQUAL;
    };

    std::stable_sort(node.children.begin(), node.children.end(),
        [&](const std::unique_ptr<FolderNode>& a, const std::unique_ptr<FolderNode>& b) {
            int r = 0;
            if (col == colName)
                r = byName(*a, *b);
            else
            {
                const double ka = key(*a), kb = key(*b);
                r = ka < kb ? -1 : ka > kb ? 1 : 0;
            }
            if (r == 0)
                return byName(*a, *b) < 0;
            return asc ? r < 0 : r > 0;
        });
}

void CFolderTreeList::AddRows(FolderNode* node, Drive* drive, int depth)
{
    m_rows.push_back({ node, drive, depth });
    if (!node->expanded || node->children.empty())
        return;
    SortChildren(*node);
    for (auto& child : node->children)
        AddRows(child.get(), drive, depth + 1);
}

void CFolderTreeList::RebuildRows(FolderNode* keepSelected)
{
    if (!GetSafeHwnd())
        return;

    if (!keepSelected)
    {
        const int sel = SelectedRow();
        if (sel >= 0)
            keepSelected = m_rows[sel].node;
    }

    m_rows.clear();
    for (auto& d : m_drives)
        AddRows(d->tree.get(), d.get(), 0);

    SetItemCountEx(int(m_rows.size()), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);

    int newSel = -1;
    for (int i = 0; i < int(m_rows.size()); ++i)
        if (m_rows[i].node == keepSelected)
        {
            newSel = i;
            break;
        }
    SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (newSel >= 0)
        SetItemState(newSel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    Invalidate(FALSE);
}

bool CFolderTreeList::CanExpand(const Row& r) const
{
    if (r.depth == 0 && !r.drive->scanned)
        return true;   // expanding starts the scan
    return r.node->HasChildren();
}

void CFolderTreeList::Expand(int row, bool expand)
{
    if (row < 0 || row >= int(m_rows.size()))
        return;
    Row r = m_rows[row];

    if (expand && r.depth == 0 && !r.drive->scanned)
    {
        r.node->expanded = true;   // show the tree as soon as the scan completes
        if (!r.drive->scanner && !ScanDrive(r.drive->root))
            AfxMessageBox(_T("Another drive is being scanned. Wait for it to finish or press Stop."), MB_ICONINFORMATION);
        return;
    }
    if (!r.node->HasChildren() || r.node->expanded == expand)
        return;

    r.node->expanded = expand;
    RebuildRows(r.node);
    if (expand)
        EnsureVisible(min(row + int(r.node->children.size()), GetItemCount() - 1), FALSE);
    EnsureVisible(row, FALSE);
}

void CFolderTreeList::Toggle(int row)
{
    if (row < 0 || row >= int(m_rows.size()))
        return;
    const Row& r = m_rows[row];
    const bool expanded = r.node->expanded && (r.depth > 0 || r.drive->scanned);
    Expand(row, !expanded);
}

int CFolderTreeList::SelectedRow() const
{
    const int sel = GetNextItem(-1, LVNI_SELECTED);
    return sel >= 0 && sel < int(m_rows.size()) ? sel : -1;
}

void CFolderTreeList::SelectRow(int row)
{
    if (row < 0 || row >= int(m_rows.size()))
        return;
    SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    EnsureVisible(row, FALSE);
}

CString CFolderTreeList::CellText(const Row& r, int column) const
{
    const FolderNode& n = *r.node;
    const Drive& d = *r.drive;
    const bool driveRow = r.depth == 0;

    if (driveRow && !d.scanned)
    {
        switch (column)
        {
        case colName:
        {
            CString s;
            s.Format(_T("%s  %s"), d.root.c_str(), d.label.GetString());
            return s;
        }
        case colAllocated:
            if (d.scanner)
                return fmt::Bytes(double(d.scanner->GetProgress().allocated)) + _T("...");
            return _T("Not scanned");
        case colPercent:
            if (d.scanner)
            {
                CString s;
                s.Format(_T("Scanning... %d %%"), d.usedBytes ? int(min(99.0, 100.0 * double(d.scanner->GetProgress().allocated) / double(d.usedBytes))) : 0);
                return s;
            }
            return _T("Expand to scan");
        default:
            return CString();
        }
    }

    switch (column)
    {
    case colName:
    {
        CString s;
        if (driveRow)
        {
            s.Format(_T("%s  %s"), d.root.c_str(), d.label.GetString());
            if (d.scanner)
                s += _T("   (rescanning...)");
        }
        else
        {
            s = n.name.c_str();
            if (n.accessDenied)
                s += _T("   (access denied)");
        }
        return s;
    }
    case colAllocated: return fmt::Bytes(double(n.allocated));
    case colSize:      return fmt::Bytes(double(n.size));
    case colPercent:
    {
        // Drives: share of the volume's capacity; folders: share of the parent folder.
        const ULONGLONG whole = driveRow ? d.totalBytes : (n.parent ? n.parent->allocated : 0);
        return whole ? PercentBar(100.0 * double(n.allocated) / double(whole)) : CString();
    }
    case colFiles:     return Count(n.files);
    case colFolders:   return n.filesEntry ? CString() : Count(n.folders);
    case colModified:  return FormatDate(n.lastWrite);
    default:           return CString();
    }
}

void CFolderTreeList::OnGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    LVITEM& item = reinterpret_cast<NMLVDISPINFO*>(pNMHDR)->item;
    if (item.iItem < 0 || item.iItem >= int(m_rows.size()))
        return;
    const Row& r = m_rows[item.iItem];

    if ((item.mask & LVIF_TEXT) && item.pszText && item.cchTextMax > 0)
        _tcsncpy_s(item.pszText, item.cchTextMax, CellText(r, item.iSubItem), _TRUNCATE);

    if (item.mask & LVIF_IMAGE)
    {
        const bool expanded = r.node->expanded && (r.depth > 0 || r.drive->scanned);
        item.iImage = CanExpand(r) ? (expanded ? imgExpanded : imgCollapsed) : imgNone;
    }
    if (item.mask & LVIF_INDENT)
        item.iIndent = r.depth;
}

COLORREF CFolderTreeList::OnGetCellTextColor(int nRow, int nColumn)
{
    if (nRow >= 0 && nRow < int(m_rows.size()))
    {
        const FolderNode& n = *m_rows[nRow].node;
        if (n.accessDenied)
            return RGB(150, 150, 150);
        if (n.filesEntry)
            return RGB(70, 90, 150);
    }
    return CMFCListCtrl::OnGetCellTextColor(nRow, nColumn);
}

// ---------------------------------------------------------------------------
// Input

void CFolderTreeList::OnLButtonDown(UINT nFlags, CPoint point)
{
    LVHITTESTINFO hit = {};
    hit.pt = point;
    SubItemHitTest(&hit);

    CMFCListCtrl::OnLButtonDown(nFlags, point);

    if (hit.iItem >= 0 && hit.iSubItem == 0 && (hit.flags & LVHT_ONITEMICON))
        Toggle(hit.iItem);
}

void CFolderTreeList::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    LVHITTESTINFO hit = {};
    hit.pt = point;
    SubItemHitTest(&hit);
    if (hit.iItem >= 0)
        Toggle(hit.iItem);
    else
        CMFCListCtrl::OnLButtonDblClk(nFlags, point);
}

void CFolderTreeList::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    const int sel = SelectedRow();
    if (sel >= 0)
    {
        const Row r = m_rows[sel];
        const bool expanded = r.node->expanded && (r.depth > 0 || r.drive->scanned) && r.node->HasChildren();
        switch (nChar)
        {
        case VK_RIGHT:
            if (!expanded && CanExpand(r))
                Expand(sel, true);
            else if (expanded)
                SelectRow(sel + 1);
            return;
        case VK_LEFT:
            if (expanded)
                Expand(sel, false);
            else
                for (int i = sel - 1; i >= 0; --i)
                    if (m_rows[i].depth == r.depth - 1)
                    {
                        SelectRow(i);
                        break;
                    }
            return;
        case VK_ADD:
            Expand(sel, true);
            return;
        case VK_SUBTRACT:
            Expand(sel, false);
            return;
        }
    }
    CMFCListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CFolderTreeList::OnContextMenu(CWnd*, CPoint point)
{
    int row = -1;
    if (point.x == -1 && point.y == -1)
    {
        row = SelectedRow();
        CRect rc;
        if (row < 0 || !GetItemRect(row, &rc, LVIR_LABEL))
            return;
        point = rc.CenterPoint();
        ClientToScreen(&point);
    }
    else
    {
        CPoint client = point;
        ScreenToClient(&client);
        UINT flags = 0;
        row = HitTest(client, &flags);
        if (row >= 0)
            SelectRow(row);
    }
    if (row < 0)
        return;

    const Row r = m_rows[row];
    const bool expanded = r.node->expanded && (r.depth > 0 || r.drive->scanned) && r.node->HasChildren();
    const bool scanning = std::any_of(m_drives.begin(), m_drives.end(), [](const auto& d) { return d->scanner != nullptr; });

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, cmdOpen, _T("Open in &Explorer"));
    menu.AppendMenu(MF_STRING, cmdCopy, _T("&Copy Path"));
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING | (CanExpand(r) ? 0 : MF_GRAYED), cmdToggle, expanded ? _T("C&ollapse") : _T("E&xpand"));
    menu.AppendMenu(MF_STRING, cmdCollapseAll, _T("Collapse &All"));
    menu.AppendMenu(MF_SEPARATOR);
    CString scanText;
    scanText.Format(r.drive->scanned ? _T("&Rescan %s") : _T("&Scan %s"), r.drive->root.c_str());
    menu.AppendMenu(MF_STRING | (scanning ? MF_GRAYED : 0), cmdScan, scanText);

    const UINT cmd = menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, this);
    // Re-resolve: a finished scan may have rebuilt the rows while the menu was open.
    row = -1;
    for (int i = 0; i < int(m_rows.size()); ++i)
        if (m_rows[i].node == r.node)
            row = i;

    switch (cmd)
    {
    case cmdOpen:
    case cmdCopy:
    {
        if (row < 0)
            break;
        const std::wstring path = r.node->FullPath();
        if (cmd == cmdOpen)
        {
            CString args;
            args.Format(_T("\"%s\""), path.c_str());
            ShellExecute(GetSafeHwnd(), _T("open"), _T("explorer.exe"), args, nullptr, SW_SHOWNORMAL);
        }
        else if (OpenClipboard())
        {
            EmptyClipboard();
            const size_t bytes = (path.size() + 1) * sizeof(wchar_t);
            if (HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes))
            {
                memcpy(GlobalLock(hMem), path.c_str(), bytes);
                GlobalUnlock(hMem);
                if (!SetClipboardData(CF_UNICODETEXT, hMem))
                    GlobalFree(hMem);
            }
            CloseClipboard();
        }
        break;
    }
    case cmdToggle:
        Toggle(row);
        break;
    case cmdCollapseAll:
    {
        std::function<void(FolderNode&)> collapse = [&](FolderNode& n) {
            n.expanded = false;
            for (auto& c : n.children)
                collapse(*c);
        };
        for (auto& d : m_drives)
            collapse(*d->tree);
        RebuildRows(r.drive->tree.get());
        break;
    }
    case cmdScan:
        ScanDrive(r.drive->root);
        break;
    }
}
