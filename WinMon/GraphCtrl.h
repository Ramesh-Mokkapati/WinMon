// GraphCtrl.h : owner-drawn, double-buffered real-time line graph
#pragma once

class CGraphCtrl : public CWnd
{
public:
    enum class Scale { Percent, AutoBytes };

    CGraphCtrl();

    BOOL Create(const RECT& rect, CWnd* pParent, UINT nID, LPCTSTR title, Scale scale);

    // Up to two series; series 0 is filled, series 1 is drawn as a plain line.
    void SetSeries(int index, LPCTSTR name, COLORREF color);
    void AddValues(double v0, double v1 = 0.0);
    void SetCaption(LPCTSTR caption);   // text shown in the top-right corner

    static constexpr size_t kHistory = 120;

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    DECLARE_MESSAGE_MAP()

private:
    void Draw(CDC& dc, const CRect& rc);
    double ScaleMax() const;

    struct Series
    {
        CString            name;
        COLORREF           color = RGB(0, 255, 0);
        std::deque<double> values;
        bool               used = false;
    };

    CString  m_title;
    CString  m_caption;
    Scale    m_scale = Scale::Percent;
    Series   m_series[2];
    ULONGLONG m_samples = 0;       // scrolls the vertical grid with the data
    CFont    m_font;
};
