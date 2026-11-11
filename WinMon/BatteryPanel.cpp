// BatteryPanel.cpp
#include "pch.h"
#include "BatteryPanel.h"
#include "Format.h"

#include <winioctl.h>   // CTL_CODE etc. (excluded by VC_EXTRALEAN)
#include <poclass.h>

namespace
{
    enum { IDC_BATTERY_LIST = 5001 };

    const COLORREF kSection = RGB(0, 70, 140);
    const COLORREF kAlert   = RGB(200, 0, 0);
    const COLORREF kMuted   = RGB(128, 128, 128);

    constexpr double kHotCelsius     = 45.0;   // same thresholds as BatteryConsole
    constexpr double kWornPercent    = 75.0;

    CString AcText(BYTE ac)
    {
        switch (ac)
        {
        case 0:  return _T("Battery");
        case 1:  return _T("AC power (plugged in)");
        default: return _T("Unknown");
        }
    }

    CString PowerStateText(const BatteryInfo& b)
    {
        if (!b.statusKnown)
            return _T("Unknown");
        CString s;
        if (b.powerState & BATTERY_CHARGING)
            s = _T("Charging");
        else if (b.powerState & BATTERY_DISCHARGING)
            s = _T("Discharging");
        else if (b.powerState & BATTERY_POWER_ON_LINE)
            s = b.chargePercent >= 99.5 ? _T("Fully charged") : _T("Plugged in, not charging");
        else
            s = _T("Idle");
        if (b.powerState & BATTERY_CRITICAL)
            s += _T(" (CRITICAL)");
        return s;
    }

    // mWh, or % of full charge for batteries that only report relative capacity.
    CString Capacity(const BatteryInfo& b, ULONG v)
    {
        if (v == BATTERY_UNKNOWN_CAPACITY || (v == 0 && !b.relativeCapacity))
            return _T("Unknown");
        CString s;
        if (b.relativeCapacity)
            s.Format(_T("%lu %% (relative)"), v);
        else
            s.Format(_T("%s mWh  (%.1f Wh)"), fmt::Grouped(v).GetString(), v / 1000.0);
        return s;
    }

    CString TimeText(ULONG seconds)
    {
        return seconds == BATTERY_UNKNOWN_TIME || seconds == 0 ? CString(_T("Unknown")) : fmt::HoursMinutes(seconds);
    }

    class RowBuilder
    {
    public:
        void Section(const CString& title)
        {
            Add(title, CString(), kSection);
        }
        void Add(LPCTSTR name, const CString& value, COLORREF color = CLR_DEFAULT)
        {
            ListRow r;
            r.key = ++m_order;
            r.text = { CString(_T("    ")) + name, value };
            r.value = { double(m_order), 0 };
            r.textColor = color;
            if (color == kSection)
                r.text[0] = name;
            m_rows.push_back(std::move(r));
        }
        std::vector<ListRow>&& Take() { return std::move(m_rows); }

    private:
        std::vector<ListRow> m_rows;
        ULONGLONG            m_order = 0;
    };
}

BOOL CBatteryPanel::CreateControls()
{
    if (!CreateLabel(m_summary, _T("Collecting power information..."), IDC_STATIC, SS_LEFT, true) ||
        !CreateLabel(m_chargeLabel, _T("Battery charge")) ||
        !m_chargeBar.Create(WS_CHILD | WS_VISIBLE | PBS_SMOOTH, CRect(0, 0, 10, 10), this, IDC_STATIC) ||
        !CreateLabel(m_chargeValue, _T("")) ||
        !CreateLabel(m_detailsLabel, _T("Power and battery details")) ||
        !m_details.CreateList(this, IDC_BATTERY_LIST, LVS_NOSORTHEADER))
        return FALSE;

    m_chargeBar.SetRange(0, 100);

    // Column 0 sorts by the row's insertion order (value[0]) so sections stay grouped.
    m_details.SetColumns({
        { _T("Property"), Dpi(260), true },
        { _T("Value"),    Dpi(520), false },
    });
    m_details.SetDefaultSort(0, true);
    return TRUE;
}

void CBatteryPanel::Layout(int cx, int cy)
{
    if (!m_boldFont.GetSafeHandle() && m_pFont)
    {
        LOGFONT lf = {};
        m_pFont->GetLogFont(&lf);
        lf.lfWeight = FW_BOLD;
        m_boldFont.CreateFontIndirect(&lf);
        m_detailsLabel.SetFont(&m_boldFont, FALSE);
    }

    const int m = Dpi(10), gap = Dpi(8), summaryH = Dpi(36), rowH = Dpi(22), labelW = Dpi(150), valueW = Dpi(260);

    int y = m;
    m_summary.MoveWindow(m, y, cx - 2 * m, summaryH);
    y += summaryH + gap;

    const int barW = max(cx - 2 * m - labelW - valueW - 2 * gap, Dpi(60));
    m_chargeLabel.MoveWindow(m, y + Dpi(3), labelW, rowH - Dpi(4));
    m_chargeBar.MoveWindow(m + labelW + gap, y + Dpi(2), barW, rowH - Dpi(4));
    m_chargeValue.MoveWindow(m + labelW + gap + barW + gap, y + Dpi(3), valueW, rowH - Dpi(4));
    y += rowH + gap;

    m_detailsLabel.MoveWindow(m, y, cx - 2 * m, rowH - Dpi(4));
    y += rowH;
    m_details.MoveWindow(m, y, cx - 2 * m, max(cy - y - m, 0));
}

void CBatteryPanel::UpdateMetrics(const SystemSnapshot& snap)
{
    if (snap.powerSeq == m_lastSeq)
        return;
    m_lastSeq = snap.powerSeq;

    const PowerInfo& p = snap.power;

    // Summary line and overall charge bar.
    CString summary;
    summary.Format(_T("Power source: %s   |   Power plan: %s   |   Battery saver: %s"),
                   AcText(p.acLineStatus).GetString(),
                   p.powerPlan.IsEmpty() ? _T("Unknown") : p.powerPlan.GetString(),
                   p.batterySaver ? _T("On") : _T("Off"));
    if (!p.hasBattery)
        summary += _T("\nNo system battery detected.");
    else if (p.lifePercent >= 0)
    {
        CString line;
        line.Format(_T("\n%u batter%s  |  Overall charge %d %%"), unsigned(p.batteries.size()),
                    p.batteries.size() == 1 ? _T("y") : _T("ies"), p.lifePercent);
        if (p.acLineStatus == 0 && p.lifeTimeSec != 0xFFFFFFFF)
            line += _T("  |  ") + fmt::HoursMinutes(p.lifeTimeSec) + _T(" remaining");
        summary += line;
    }
    CString old;
    m_summary.GetWindowText(old);
    if (old != summary)
        m_summary.SetWindowText(summary);

    const int pct = p.hasBattery ? max(p.lifePercent, 0) : 0;
    m_chargeBar.SetPos(pct);
    m_chargeBar.SendMessage(PBM_SETSTATE, !p.hasBattery ? PBST_PAUSED : pct <= 10 ? PBST_ERROR : pct <= 25 ? PBST_PAUSED : PBST_NORMAL);
    CString charge;
    if (!p.hasBattery)
        charge = _T("No battery");
    else if (p.lifePercent < 0)
        charge = _T("Unknown");
    else
        charge.Format(_T("%d %%%s"), p.lifePercent, p.acLineStatus == 1 ? _T("  (plugged in)") : _T("  (on battery)"));
    m_chargeValue.GetWindowText(old);
    if (old != charge)
        m_chargeValue.SetWindowText(charge);

    // Details list.
    RowBuilder rows;
    rows.Section(_T("Power source"));
    rows.Add(_T("AC line status"), AcText(p.acLineStatus));
    rows.Add(_T("Battery saver"), p.batterySaver ? CString(_T("On")) : CString(_T("Off")));
    rows.Add(_T("Active power plan"), p.powerPlan.IsEmpty() ? CString(_T("Unknown")) : p.powerPlan);
    if (p.hasBattery)
    {
        rows.Add(_T("Overall charge"), p.lifePercent >= 0 ? fmt::Percent(p.lifePercent) : CString(_T("Unknown")),
                 p.lifePercent >= 0 && p.lifePercent <= 10 ? kAlert : CLR_DEFAULT);
        rows.Add(_T("Estimated time remaining"),
                 p.acLineStatus == 1 ? CString(_T("Not applicable (plugged in)")) : TimeText(p.lifeTimeSec));
    }

    if (p.batteries.empty())
    {
        rows.Section(_T("Batteries"));
        rows.Add(_T("Status"), p.hasBattery ? CString(_T("Battery present, but its details could not be read"))
                                            : CString(_T("No battery detected: this system runs on AC power")), kMuted);
    }

    for (const auto& b : p.batteries)
    {
        CString title;
        title.Format(_T("Battery %d%s%s"), b.slot, b.deviceName.IsEmpty() ? _T("") : _T(": "), b.deviceName.GetString());
        rows.Section(title);

        // Identity
        rows.Add(_T("Manufacturer"), b.manufacturer.IsEmpty() ? CString(_T("Unknown")) : b.manufacturer);
        rows.Add(_T("Serial number"), b.serialNumber.IsEmpty() ? CString(_T("Unknown")) : b.serialNumber);
        rows.Add(_T("Unique ID"), b.uniqueId.IsEmpty() ? CString(_T("Unknown")) : b.uniqueId);
        CString chem = b.chemistry.IsEmpty() ? CString(_T("Unknown")) : b.chemistry;
        chem += b.rechargeable ? _T(" (rechargeable)") : _T(" (non-rechargeable)");
        rows.Add(_T("Chemistry"), chem);
        CString kind = b.shortTerm ? _T("UPS / short-term") : b.systemBattery ? _T("System battery") : _T("Device battery");
        rows.Add(_T("Type"), kind);
        CString date = _T("Not reported");
        if (b.manufactureYear)
            date.Format(_T("%04u-%02u-%02u"), b.manufactureYear, b.manufactureMonth, b.manufactureDay);
        rows.Add(_T("Manufacture date"), date);

        // Live status
        const bool critical = b.statusKnown && (b.powerState & BATTERY_CRITICAL);
        rows.Add(_T("State"), PowerStateText(b), critical ? kAlert : CLR_DEFAULT);
        rows.Add(_T("Charge"), b.chargePercent >= 0 ? fmt::Percent(b.chargePercent) : CString(_T("Unknown")),
                 critical || (b.chargePercent >= 0 && b.chargePercent <= 10) ? kAlert : CLR_DEFAULT);
        rows.Add(_T("Remaining capacity"), b.statusKnown ? Capacity(b, b.currentCapacity) : CString(_T("Unknown")));

        CString rate = _T("Unknown");
        if (b.rateKnown)
        {
            rate.Format(_T("%s mW  (%.2f W, %s)"), fmt::Grouped(b.rateMw).GetString(), b.rateMw / 1000.0,
                        b.rateMw > 0 ? _T("charging") : b.rateMw < 0 ? _T("discharging") : _T("idle"));
            if (b.relativeCapacity)
                rate.Format(_T("%ld (relative units)"), b.rateMw);
        }
        rows.Add(_T("Charge / discharge rate"), rate);

        CString volts = _T("Unknown");
        if (b.statusKnown && b.voltageMv != BATTERY_UNKNOWN_VOLTAGE && b.voltageMv != 0)
            volts.Format(_T("%s mV  (%.2f V)"), fmt::Grouped(b.voltageMv).GetString(), b.voltageMv / 1000.0);
        rows.Add(_T("Voltage"), volts);

        if (b.powerState & BATTERY_DISCHARGING)
            rows.Add(_T("Time to empty (driver estimate)"), TimeText(b.estimatedSeconds));
        if (b.powerState & BATTERY_CHARGING)
            rows.Add(_T("Time to full (at current rate)"), TimeText(b.secondsToFull));

        CString temp = _T("Not reported");
        if (b.temperatureKnown)
        {
            temp.Format(_T("%.1f \x00B0") _T("C"), b.temperatureC);
            if (b.temperatureC > kHotCelsius)
                temp += _T("  - HIGH TEMPERATURE");
        }
        rows.Add(_T("Temperature"), temp, b.temperatureKnown && b.temperatureC > kHotCelsius ? kAlert : CLR_DEFAULT);

        // Health
        rows.Add(_T("Full charge capacity"), Capacity(b, b.fullChargeCapacity));
        rows.Add(_T("Design capacity"), Capacity(b, b.designCapacity));
        CString health = _T("Unknown");
        const bool worn = b.healthPercent >= 0 && b.healthPercent < kWornPercent;
        if (b.healthPercent >= 0)
        {
            health.Format(_T("%.1f %%  (wear %.1f %%)"), b.healthPercent, max(0.0, 100.0 - b.healthPercent));
            if (worn)
                health += _T("  - consider replacing the battery");
        }
        rows.Add(_T("Health (full / design)"), health, worn ? kAlert : CLR_DEFAULT);
        rows.Add(_T("Cycle count"), b.cycleCount ? fmt::Grouped(b.cycleCount) : CString(_T("Not reported")));
        rows.Add(_T("Warning alert level"), b.defaultAlert2 ? Capacity(b, b.defaultAlert2) : CString(_T("Not reported")));
        rows.Add(_T("Low battery alert level"), b.defaultAlert1 ? Capacity(b, b.defaultAlert1) : CString(_T("Not reported")));
        if (b.criticalBias)
            rows.Add(_T("Critical bias"), Capacity(b, b.criticalBias));
    }

    m_details.SetRows(rows.Take());
}
