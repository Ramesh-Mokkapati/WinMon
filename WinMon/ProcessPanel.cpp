// ProcessPanel.cpp
#include "pch.h"
#include "ProcessPanel.h"
#include "ProcessMonitor.h"
#include "MonitorEngine.h"
#include "Format.h"
#include "resource.h"

namespace
{
    enum { IDC_FILTER = 2001, IDC_END_TASK, IDC_PROCESS_LIST, IDC_MODULE_LIST };

    enum ProcessColumn { colPid, colName, colCpu, colWorkingSet, colPrivate, colThreads, colPriority, colParent, colIoRead, colIoWrite, colPath, colCount };

    CString ErrorText(DWORD error)
    {
        LPWSTR buf = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, error, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
        CString text = buf ? buf : L"";
        if (buf)
            LocalFree(buf);
        text.Trim();
        CString full;
        full.Format(_T("%s (error %u)"), text.GetString(), error);
        return full;
    }

    bool IsCriticalProcess(const CString& name)
    {
        static const LPCTSTR critical[] = { _T("csrss.exe"), _T("wininit.exe"), _T("winlogon.exe"), _T("smss.exe"),
                                            _T("services.exe"), _T("lsass.exe"), _T("svchost.exe"), _T("dwm.exe"),
                                            _T("Registry"), _T("Memory Compression") };
        for (LPCTSTR c : critical)
            if (name.CompareNoCase(c) == 0)
                return true;
        return false;
    }
}

BEGIN_MESSAGE_MAP(CProcessPanel, CPanel)
    ON_EN_CHANGE(IDC_FILTER, &CProcessPanel::OnFilterChanged)
    ON_BN_CLICKED(IDC_END_TASK, &CProcessPanel::OnEndTask)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_PROCESS_LIST, &CProcessPanel::OnProcessItemChanged)
    ON_NOTIFY(NM_RCLICK, IDC_PROCESS_LIST, &CProcessPanel::OnProcessRightClick)
    ON_NOTIFY(LVN_KEYDOWN, IDC_PROCESS_LIST, &CProcessPanel::OnProcessKeyDown)
END_MESSAGE_MAP()

BOOL CProcessPanel::CreateControls()
{
    if (!CreateLabel(m_filterLabel, _T("Filter:")) ||
        !m_filter.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, CRect(0, 0, 10, 10), this, IDC_FILTER) ||
        !m_endTask.Create(_T("&End Task"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, CRect(0, 0, 10, 10), this, IDC_END_TASK) ||
        !CreateLabel(m_count, _T(""), IDC_STATIC, SS_RIGHT) ||
        !m_processes.CreateList(this, IDC_PROCESS_LIST) ||
        !CreateLabel(m_modulesLabel, _T("Loaded modules: select a process")) ||
        !m_modules.CreateList(this, IDC_MODULE_LIST))
        return FALSE;

    m_filter.SetCueBanner(_T("Process name or PID"));

    const int R = LVCFMT_RIGHT;
    m_processes.SetColumns({
        { _T("PID"),             Dpi(60),  true,  R },
        { _T("Process Name"),    Dpi(200), false },
        { _T("CPU %"),           Dpi(60),  true,  R },
        { _T("Memory (Working Set)"), Dpi(140), true, R },
        { _T("Private Bytes"),   Dpi(110), true,  R },
        { _T("Threads"),         Dpi(60),  true,  R },
        { _T("Priority"),        Dpi(90),  false },
        { _T("Parent PID"),      Dpi(75),  true,  R },
        { _T("I/O Read"),        Dpi(90),  true,  R },
        { _T("I/O Write"),       Dpi(90),  true,  R },
        { _T("Image Path"),      Dpi(400), false },
    });
    m_processes.SetDefaultSort(colCpu, false);

    m_modules.SetColumns({
        { _T("Module"),       Dpi(200), false },
        { _T("Base Address"), Dpi(140), true, R },
        { _T("Size"),         Dpi(90),  true, R },
        { _T("Path"),         Dpi(600), false },
    });
    m_modules.SetDefaultSort(0, true);
    return TRUE;
}

void CProcessPanel::Layout(int cx, int cy)
{
    const int m = Dpi(8), gap = Dpi(6), rowH = Dpi(24);

    int x = m;
    m_filterLabel.MoveWindow(x, m + Dpi(4), Dpi(40), rowH - Dpi(4));
    x += Dpi(40);
    m_filter.MoveWindow(x, m, Dpi(220), rowH);
    x += Dpi(220) + gap;
    m_endTask.MoveWindow(x, m - Dpi(1), Dpi(90), rowH + Dpi(2));
    x += Dpi(90) + gap;
    m_count.MoveWindow(x, m + Dpi(4), max(cx - m - x, 0), rowH - Dpi(4));

    const int top = m + rowH + gap;
    const int avail = cy - top - m;
    const int modulesH = max(avail * 30 / 100, Dpi(80));
    const int procH = max(avail - modulesH - rowH, Dpi(80));

    m_processes.MoveWindow(m, top, cx - 2 * m, procH);
    m_modulesLabel.MoveWindow(m, top + procH + Dpi(6), cx - 2 * m, rowH - Dpi(6));
    m_modules.MoveWindow(m, top + procH + rowH, cx - 2 * m, max(cy - (top + procH + rowH) - m, 0));
}

void CProcessPanel::UpdateMetrics(const SystemSnapshot& snap)
{
    if (snap.processSeq == m_lastSeq)
        return;
    m_lastSeq = snap.processSeq;
    m_data = snap.processes;
    RebuildRows();
}

void CProcessPanel::RebuildRows()
{
    CString filter;
    m_filter.GetWindowText(filter);
    filter.Trim();
    filter.MakeLower();

    std::vector<ListRow> rows;
    rows.reserve(m_data.size());
    ULONGLONG totalWs = 0;

    for (const auto& p : m_data)
    {
        totalWs += p.workingSet;
        if (!filter.IsEmpty())
        {
            CString name = p.name;
            name.MakeLower();
            if (name.Find(filter) < 0 && fmt::Number(p.pid).Find(filter) < 0)
                continue;
        }

        ListRow r;
        r.key = p.pid;
        r.text.resize(colCount);
        r.value.resize(colCount);

        r.text[colPid]        = fmt::Number(p.pid);
        r.text[colName]       = p.name;
        r.text[colCpu].Format(_T("%.1f"), p.cpuPercent);
        r.text[colWorkingSet] = p.accessDenied ? CString(_T("Access denied")) : fmt::Kilobytes(p.workingSet);
        r.text[colPrivate]    = p.accessDenied ? CString() : fmt::Kilobytes(p.privateBytes);
        r.text[colThreads]    = fmt::Number(p.threads);
        r.text[colPriority]   = CProcessMonitor::PriorityClassToString(p.priorityClass);
        r.text[colParent]     = p.pid ? fmt::Number(p.parentPid) : CString();
        r.text[colIoRead]     = p.accessDenied ? CString() : fmt::Rate(p.ioReadPerSec);
        r.text[colIoWrite]    = p.accessDenied ? CString() : fmt::Rate(p.ioWritePerSec);
        r.text[colPath]       = p.imagePath;

        r.value[colPid]        = p.pid;
        r.value[colCpu]        = p.cpuPercent;
        r.value[colWorkingSet] = double(p.workingSet);
        r.value[colPrivate]    = double(p.privateBytes);
        r.value[colThreads]    = p.threads;
        r.value[colParent]     = p.parentPid;
        r.value[colIoRead]     = p.ioReadPerSec;
        r.value[colIoWrite]    = p.ioWritePerSec;

        if (p.pid != 0 && p.cpuPercent >= 25.0)
            r.textColor = RGB(200, 0, 0);
        else if (p.accessDenied)
            r.textColor = RGB(128, 128, 128);

        rows.push_back(std::move(r));
    }

    m_processes.SetRows(std::move(rows));

    CString count;
    count.Format(_T("%u of %u processes  |  Total working set: %s"),
                 m_processes.GetItemCount(), unsigned(m_data.size()), fmt::Bytes(double(totalWs)).GetString());
    m_count.SetWindowText(count);
}

const ProcessInfo* CProcessPanel::FindProcess(DWORD pid) const
{
    for (const auto& p : m_data)
        if (p.pid == pid)
            return &p;
    return nullptr;
}

void CProcessPanel::OnFilterChanged()
{
    RebuildRows();
}

void CProcessPanel::OnProcessItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    const auto* pNM = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
    if (!(pNM->uChanged & LVIF_STATE) || !(pNM->uNewState & LVIS_SELECTED) || (pNM->uOldState & LVIS_SELECTED))
        return;

    const DWORD pid = static_cast<DWORD>(m_processes.GetItemData(pNM->iItem));
    if (pid != m_modulesPid)
        LoadModules(pid);
}

void CProcessPanel::LoadModules(DWORD pid)
{
    m_modulesPid = pid;

    std::vector<ModuleInfo> modules;
    DWORD error = ERROR_SUCCESS;
    const ProcessInfo* p = FindProcess(pid);
    const CString name = p ? p->name : CString(_T("?"));

    CString label;
    if (pid == 0 || pid == 4)
    {
        label.Format(_T("Loaded modules of %s (PID %u): not available for system processes"), name.GetString(), pid);
    }
    else if (!CProcessMonitor::EnumerateModules(pid, modules, error))
    {
        label.Format(_T("Loaded modules of %s (PID %u): %s"), name.GetString(), pid, ErrorText(error).GetString());
    }
    else
    {
        label.Format(_T("Loaded modules of %s (PID %u): %u"), name.GetString(), pid, unsigned(modules.size()));
    }
    m_modulesLabel.SetWindowText(label);

    std::vector<ListRow> rows;
    rows.reserve(modules.size());
    for (const auto& mi : modules)
    {
        ListRow r;
        r.key = mi.baseAddress;
        CString base;
        base.Format(_T("0x%016IX"), mi.baseAddress);
        r.text = { mi.name, base, fmt::Kilobytes(mi.size), mi.path };
        r.value = { 0.0, double(mi.baseAddress), double(mi.size), 0.0 };
        rows.push_back(std::move(r));
    }
    m_modules.SetRows(std::move(rows));
}

void CProcessPanel::OnProcessRightClick(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    const auto* pNM = reinterpret_cast<NMITEMACTIVATE*>(pNMHDR);
    if (pNM->iItem < 0)
        return;

    // Resolve the PID now: refreshes during the menu's modal loop may re-sort the list.
    const DWORD pid = static_cast<DWORD>(m_processes.GetItemData(pNM->iItem));

    CMenu menu;
    if (!menu.LoadMenu(IDR_PROCESS_MENU))
        return;
    CMenu* pPopup = menu.GetSubMenu(0);

    CPoint pt;
    GetCursorPos(&pt);
    const UINT cmd = pPopup->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, this);

    switch (cmd)
    {
    case ID_PROCESS_ENDTASK:      OnEndTask(); break;
    case ID_PROCESS_OPENLOCATION: OpenFileLocation(pid); break;
    case ID_PROCESS_REFRESH:
        if (m_pEngine)
            m_pEngine->RequestFullRefresh();
        LoadModules(pid);
        break;
    }
}

void CProcessPanel::OnProcessKeyDown(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    if (reinterpret_cast<NMLVKEYDOWN*>(pNMHDR)->wVKey == VK_DELETE)
        OnEndTask();
}

void CProcessPanel::OnEndTask()
{
    ULONGLONG key = 0;
    if (!m_processes.GetSelectedKey(key))
    {
        AfxMessageBox(_T("Select a process first."), MB_ICONINFORMATION);
        return;
    }

    const DWORD pid = static_cast<DWORD>(key);
    const ProcessInfo* p = FindProcess(pid);
    const CString name = p ? p->name : CString(_T("this process"));

    if (pid == 0 || pid == 4 || pid == GetCurrentProcessId())
    {
        AfxMessageBox(_T("This process cannot be ended."), MB_ICONINFORMATION);
        return;
    }

    CString prompt;
    UINT flags = MB_YESNO | MB_DEFBUTTON2;
    if (IsCriticalProcess(name))
    {
        prompt.Format(_T("%s (PID %u) is a critical Windows process.\n\n")
                      _T("Ending it may make Windows unstable or force an immediate restart, and unsaved data will be lost.\n\n")
                      _T("End it anyway?"), name.GetString(), pid);
        flags |= MB_ICONWARNING;
    }
    else
    {
        prompt.Format(_T("End %s (PID %u)?\n\nUnsaved data in this program will be lost."), name.GetString(), pid);
        flags |= MB_ICONQUESTION;
    }
    if (AfxMessageBox(prompt, flags) != IDYES)
        return;

    DWORD error = ERROR_SUCCESS;
    if (!CProcessMonitor::TerminateProcessById(pid, error))
    {
        CString msg;
        msg.Format(_T("Could not end %s (PID %u):\n%s"), name.GetString(), pid, ErrorText(error).GetString());
        if (error == ERROR_ACCESS_DENIED)
            msg += _T("\n\nTry running WinMon as administrator.");
        AfxMessageBox(msg, MB_ICONERROR);
        return;
    }

    if (m_pEngine)
        m_pEngine->RequestFullRefresh();
}

void CProcessPanel::OpenFileLocation(DWORD pid)
{
    const ProcessInfo* p = FindProcess(pid);
    if (!p || p->imagePath.IsEmpty())
    {
        AfxMessageBox(_T("The image path of this process is not available."), MB_ICONINFORMATION);
        return;
    }
    CString args;
    args.Format(_T("/select,\"%s\""), p->imagePath.GetString());
    ShellExecute(GetSafeHwnd(), _T("open"), _T("explorer.exe"), args, nullptr, SW_SHOWNORMAL);
}
