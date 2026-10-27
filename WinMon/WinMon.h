// WinMon.h : application class
#pragma once

#ifndef __AFXWIN_H__
#error "include 'pch.h' before including this file for PCH"
#endif

#include "resource.h"

class CWinMonApp : public CWinAppEx
{
public:
    CWinMonApp();

    BOOL InitInstance() override;
    int  ExitInstance() override;

    bool IsElevated() const { return m_elevated; }

private:
    static bool QueryElevation();
    static bool EnableDebugPrivilege();

    bool m_elevated = false;
    bool m_wsaStarted = false;

    DECLARE_MESSAGE_MAP()
};

extern CWinMonApp theApp;
