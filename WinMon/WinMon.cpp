// WinMon.cpp : application entry point and initialisation
#include "pch.h"
#include "WinMon.h"
#include "WinMonDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CWinMonApp, CWinAppEx)
END_MESSAGE_MAP()

CWinMonApp theApp;

CWinMonApp::CWinMonApp()
{
    SetAppID(_T("WinMon.SystemMonitor.1.0"));
}

BOOL CWinMonApp::InitInstance()
{
#ifdef _DEBUG
    // Report CRT leaks on exit (Output window when debugging).
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    CWinAppEx::InitInstance();

    WSADATA wsa = {};
    m_wsaStarted = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;

    SetRegistryKey(_T("WinMon"));

    CMFCVisualManagerOffice2007::SetStyle(CMFCVisualManagerOffice2007::Office2007_LunaBlue);
    CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerOffice2007));

    m_elevated = QueryElevation();
    if (m_elevated)
        EnableDebugPrivilege();   // lets OpenProcess reach services and other users' processes

    CWinMonDlg dlg;
    m_pMainWnd = &dlg;
    dlg.DoModal();

    // Dialog closed: return FALSE so the application exits instead of entering a message pump.
    return FALSE;
}

int CWinMonApp::ExitInstance()
{
    if (m_wsaStarted)
        WSACleanup();
    return CWinAppEx::ExitInstance();
}

bool CWinMonApp::QueryElevation()
{
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return false;
    TOKEN_ELEVATION elevation = {};
    DWORD cb = 0;
    const bool elevated = GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cb) && elevation.TokenIsElevated;
    CloseHandle(hToken);
    return elevated;
}

bool CWinMonApp::EnableDebugPrivilege()
{
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &tp.Privileges[0].Luid) &&
              AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr) &&
              GetLastError() == ERROR_SUCCESS;
    CloseHandle(hToken);
    return ok;
}
