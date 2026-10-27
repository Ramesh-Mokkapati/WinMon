// GraphCtrl.cpp
#include "pch.h"
#include "GraphCtrl.h"
#include "Format.h"

namespace
{
    const COLORREF kBackground = RGB(0, 0, 0);
    const COLORREF kGrid       = RGB(0, 70, 35);
    const COLORREF kText       = RGB(220, 220, 220);
    const COLORREF kDimText    = RGB(140, 140, 140);
    constexpr int  kGridStep   = 12;   // pixels between grid lines

    COLORREF Blend(COLORREF c, COLORREF bg, double alpha)
    {
        return RGB(BYTE(GetRValue(c) * alpha + GetRValue(bg) * (1 - alpha)),
                   BYTE(GetGValue(c) * alpha + GetGValue(bg) * (1 - alpha)),
                   BYTE(GetBValue(c) * alpha + GetBValue(bg) * (1 - alpha)));
    }
}

BEGIN_MESSAGE_MAP(CGraphCtrl, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
END_MESSAGE_MAP()

CGraphCtrl::CGraphCtrl()
{
    m_series[0].color = RGB(0, 255, 0);
    m_series[1].color = RGB(255, 200, 0);
}

BOOL CGraphCtrl::Create(const RECT& rect, CWnd* pParent, UINT nID, LPCTSTR title, Scale scale)
{
    m_title = title;
    m_scale = scale;

    LOGFONT lf = {};
    if (CFont* pFont = pParent->GetFont())
        pFont->GetLogFont(&lf);
    else
        ::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
    m_font.CreateFontIndirect(&lf);

    LPCTSTR cls = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    return CWnd::Create(cls, title, WS_CHILD | WS_VISIBLE, rect, pParent, nID);
}

void CGraphCtrl::SetSeries(int index, LPCTSTR name, COLORREF color)
{
    ASSERT(index >= 0 && index < 2);
    m_series[index].name = name;
    m_series[index].color = color;
    m_series[index].used = true;
}

void CGraphCtrl::AddValues(double v0, double v1)
{
    const double v[2] = { v0, v1 };
    for (int i = 0; i < 2; ++i)
    {
        if (!m_series[i].used)
            continue;
        m_series[i].values.push_back(v[i]);
        while (m_series[i].values.size() > kHistory)
            m_series[i].values.pop_front();
    }
    ++m_samples;
    if (GetSafeHwnd())
        Invalidate(FALSE);
}

void CGraphCtrl::SetCaption(LPCTSTR caption)
{
    if (m_caption != caption)
    {
        m_caption = caption;
        if (GetSafeHwnd())
            Invalidate(FALSE);
    }
}

BOOL CGraphCtrl::OnEraseBkgnd(CDC*)
{
    return TRUE;   // everything is painted in OnPaint via the back buffer
}

void CGraphCtrl::OnSize(UINT nType, int cx, int cy)
{
    CWnd::OnSize(nType, cx, cy);
    Invalidate(FALSE);
}

void CGraphCtrl::OnPaint()
{
    CPaintDC dc(this);
    CRect rc;
    GetClientRect(&rc);
    if (rc.IsRectEmpty())
        return;

    // Double buffering: draw into a memory bitmap, then blit once.
    CDC memDC;
    memDC.CreateCompatibleDC(&dc);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
    CBitmap* pOldBmp = memDC.SelectObject(&bmp);

    Draw(memDC, rc);

    dc.BitBlt(0, 0, rc.Width(), rc.Height(), &memDC, 0, 0, SRCCOPY);
    memDC.SelectObject(pOldBmp);
    // bmp and memDC release their GDI handles in their destructors.
}

double CGraphCtrl::ScaleMax() const
{
    if (m_scale == Scale::Percent)
        return 100.0;

    double peak = 0.0;
    for (const auto& s : m_series)
        for (double v : s.values)
            peak = max(peak, v);

    // Round up to a 1-2-5 step so the axis label is readable.
    double top = 1024.0;   // never below 1 KB/s
    while (top < peak * 1.1)
    {
        const double mantissa = top / pow(10.0, floor(log10(top)));
        top *= (mantissa < 1.5) ? 2.0 : (mantissa < 3.0) ? 2.5 : 2.0;
    }
    return top;
}

void CGraphCtrl::Draw(CDC& dc, const CRect& rc)
{
    dc.FillSolidRect(&rc, kBackground);

    const double xStep = double(rc.Width() - 1) / double(kHistory - 1);

    // Grid (vertical lines scroll left with the data)
    CPen gridPen(PS_SOLID, 1, kGrid);
    CPen* pOldPen = dc.SelectObject(&gridPen);
    for (int y = rc.bottom - 1; y > rc.top; y -= kGridStep)
    {
        dc.MoveTo(rc.left, y);
        dc.LineTo(rc.right, y);
    }
    const int shift = int(fmod(double(m_samples) * xStep, double(kGridStep)));
    for (int x = rc.right - 1 - shift; x > rc.left; x -= kGridStep)
    {
        dc.MoveTo(x, rc.top);
        dc.LineTo(x, rc.bottom);
    }

    const double top = ScaleMax();
    auto toY = [&](double v) {
        const double clamped = min(max(v, 0.0), top);
        return rc.bottom - 1 - int(clamped / top * (rc.Height() - 2));
    };

    // Series (drawn back to front so series 0 stays on top)
    for (int i = 1; i >= 0; --i)
    {
        const Series& s = m_series[i];
        if (!s.used || s.values.size() < 2)
            continue;

        const size_t n = s.values.size();
        std::vector<POINT> pts;
        pts.reserve(n + 2);
        for (size_t k = 0; k < n; ++k)
        {
            const double x = rc.right - 1 - double(n - 1 - k) * xStep;
            pts.push_back({ int(x), toY(s.values[k]) });
        }

        if (i == 0)
        {
            // Translucent-looking fill under the primary series.
            std::vector<POINT> poly(pts);
            poly.push_back({ pts.back().x, rc.bottom });
            poly.push_back({ pts.front().x, rc.bottom });
            CBrush fill(Blend(s.color, kBackground, 0.25));
            CPen none(PS_NULL, 0, RGB(0, 0, 0));
            CBrush* pOldBrush = dc.SelectObject(&fill);
            CPen* pPrev = dc.SelectObject(&none);
            dc.Polygon(poly.data(), int(poly.size()));
            dc.SelectObject(pPrev);
            dc.SelectObject(pOldBrush);
        }

        CPen linePen(PS_SOLID, 2, s.color);
        CPen* pPrev = dc.SelectObject(&linePen);
        dc.Polyline(pts.data(), int(pts.size()));
        dc.SelectObject(pPrev);
    }
    dc.SelectObject(pOldPen);

    // Text overlay
    CFont* pOldFont = dc.SelectObject(&m_font);
    dc.SetBkMode(TRANSPARENT);
    const int pad = 6;
    const int lineH = dc.GetTextExtent(_T("Ag")).cy;

    dc.SetTextColor(kText);
    dc.TextOut(rc.left + pad, rc.top + pad, m_title);

    CRect capRc(rc.left + pad, rc.top + pad, rc.right - pad, rc.top + pad + lineH);
    dc.DrawText(m_caption, &capRc, DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS);

    // Legend
    int lx = rc.left + pad;
    const int ly = rc.top + pad + lineH + 2;
    for (const auto& s : m_series)
    {
        if (!s.used || s.name.IsEmpty())
            continue;
        dc.FillSolidRect(lx, ly + lineH / 2 - 2, 10, 4, s.color);
        dc.SetTextColor(kDimText);
        dc.TextOut(lx + 14, ly, s.name);
        lx += 14 + dc.GetTextExtent(s.name).cx + 12;
    }

    // Axis maximum
    dc.SetTextColor(kDimText);
    const CString axis = m_scale == Scale::Percent ? CString(_T("100 %")) : fmt::Rate(top);
    dc.TextOut(rc.left + pad, rc.bottom - pad - lineH, axis);

    dc.SelectObject(pOldFont);
}
