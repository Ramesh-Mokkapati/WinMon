// Format.h : display helpers for sizes, rates and durations
#pragma once

namespace fmt
{
    inline CString Bytes(double bytes)
    {
        static const TCHAR* units[] = { _T("B"), _T("KB"), _T("MB"), _T("GB"), _T("TB") };
        int u = 0;
        while (bytes >= 1024.0 && u < _countof(units) - 1)
        {
            bytes /= 1024.0;
            ++u;
        }
        CString s;
        if (u == 0)
            s.Format(_T("%.0f %s"), bytes, units[u]);
        else
            s.Format(bytes < 10.0 ? _T("%.2f %s") : bytes < 100.0 ? _T("%.1f %s") : _T("%.0f %s"), bytes, units[u]);
        return s;
    }

    inline CString Rate(double bytesPerSec)
    {
        return Bytes(bytesPerSec) + _T("/s");
    }

    inline CString BitRate(ULONG64 bps)
    {
        CString s;
        if (bps == 0)            return CString();
        if (bps >= 1000000000ULL) s.Format(_T("%.1f Gbps"), bps / 1e9);
        else if (bps >= 1000000) s.Format(_T("%.0f Mbps"), bps / 1e6);
        else                     s.Format(_T("%.0f Kbps"), bps / 1e3);
        return s;
    }

    inline CString Kilobytes(ULONGLONG bytes)
    {
        // Task-Manager-style "123,456 K" so the column sorts and reads consistently.
        CString digits;
        digits.Format(_T("%I64u"), (bytes + 1023) / 1024);
        CString out;
        int n = digits.GetLength();
        for (int i = 0; i < n; ++i)
        {
            out += digits[i];
            if ((n - i - 1) % 3 == 0 && i != n - 1)
                out += _T(',');
        }
        return out + _T(" K");
    }

    inline CString Duration(ULONGLONG ms)
    {
        ULONGLONG s = ms / 1000;
        CString out;
        out.Format(_T("%I64ud %02I64u:%02I64u:%02I64u"), s / 86400, (s / 3600) % 24, (s / 60) % 60, s % 60);
        return out;
    }

    inline CString Percent(double v)
    {
        CString s;
        s.Format(_T("%.1f %%"), v);
        return s;
    }

    inline CString Number(ULONGLONG v)
    {
        CString s;
        s.Format(_T("%I64u"), v);
        return s;
    }
}
