// BatteryMonitor.cpp
//
// Battery handling is based on BatteryConsole's BatteryDiagnosticEngine, with these changes:
//  - runs on the WinMon worker thread instead of its own thread + message-only window
//    (WinMonDlg forces a refresh on WM_POWERBROADCAST, so power changes still show at once);
//  - honours BATTERY_CAPACITY_RELATIVE (capacities are % rather than mWh on some batteries);
//  - keeps the sign of the charge/discharge rate and treats "unknown" values as unknown
//    instead of showing 0;
//  - reports the critical flag, combined power-state flags, the driver's time-to-empty
//    estimate, a derived time-to-full, warning/critical levels and the technology;
//  - validates the manufacture date and always NUL-terminates the string queries;
//  - falls back to read-only access when the battery cannot be opened for read/write;
//  - adds system-wide AC/battery status, battery saver and the active power plan.
#include "pch.h"
#include "BatteryMonitor.h"

#include <setupapi.h>
#include <winioctl.h>   // CTL_CODE etc. (excluded by VC_EXTRALEAN)
#include <poclass.h>
#include <powrprof.h>

#pragma comment(lib, "SetupAPI.lib")
#pragma comment(lib, "PowrProf.lib")

namespace
{
    // GUID_DEVICE_BATTERY from poclass.h, defined here so no INITGUID is needed.
    const GUID kBatteryInterface = { 0x72631e54, 0x78a4, 0x11d0, { 0xbc, 0xf7, 0x00, 0xaa, 0x00, 0xb7, 0xb3, 0x2a } };

    bool QueryInfo(HANDLE h, ULONG tag, BATTERY_QUERY_INFORMATION_LEVEL level, void* out, DWORD outSize, LONG atRate = 0)
    {
        BATTERY_QUERY_INFORMATION bqi = {};
        bqi.BatteryTag = tag;
        bqi.InformationLevel = level;
        bqi.AtRate = atRate;
        DWORD bytes = 0;
        return DeviceIoControl(h, IOCTL_BATTERY_QUERY_INFORMATION, &bqi, sizeof(bqi), out, outSize, &bytes, nullptr) != FALSE;
    }

    CString QueryString(HANDLE h, ULONG tag, BATTERY_QUERY_INFORMATION_LEVEL level)
    {
        WCHAR buf[256] = {};
        if (!QueryInfo(h, tag, level, buf, sizeof(buf) - sizeof(WCHAR)))
            return CString();
        CString s(buf);
        s.Trim();
        return s;
    }
}

void CBatteryMonitor::Collect(PowerInfo& out)
{
    PowerInfo p;

    SYSTEM_POWER_STATUS sps = {};
    if (GetSystemPowerStatus(&sps))
    {
        p.acLineStatus = sps.ACLineStatus;
        p.batteryFlag = sps.BatteryFlag;
        p.lifePercent = sps.BatteryLifePercent <= 100 ? int(sps.BatteryLifePercent) : -1;
        p.lifeTimeSec = sps.BatteryLifeTime;
        p.batterySaver = sps.SystemStatusFlag == 1;
    }

    CollectBatteries(p.batteries);
    p.hasBattery = !p.batteries.empty() || (p.batteryFlag != 255 && !(p.batteryFlag & 128));
    p.powerPlan = ActivePowerPlan();

    out = std::move(p);
}

void CBatteryMonitor::CollectBatteries(std::vector<BatteryInfo>& out)
{
    out.clear();
    HDEVINFO hDevInfo = SetupDiGetClassDevsW(&kBatteryInterface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE)
        return;

    SP_DEVICE_INTERFACE_DATA did = { sizeof(did) };
    // Windows supports at most a handful of batteries; the cap guards against a broken enumerator.
    for (DWORD index = 0; index < 32 && SetupDiEnumDeviceInterfaces(hDevInfo, nullptr, &kBatteryInterface, index, &did); ++index)
    {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(hDevInfo, &did, nullptr, 0, &required, nullptr);
        if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
            continue;

        std::vector<BYTE> buffer(required);
        auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(hDevInfo, &did, detail, required, nullptr, nullptr))
            continue;

        HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            h = CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;

        BatteryInfo b;
        if (QueryBattery(h, int(out.size()) + 1, b))
            out.push_back(std::move(b));
        CloseHandle(h);
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
}

bool CBatteryMonitor::QueryBattery(HANDLE h, int slot, BatteryInfo& b)
{
    ULONG wait = 0, tag = 0;
    DWORD bytes = 0;
    if (!DeviceIoControl(h, IOCTL_BATTERY_QUERY_TAG, &wait, sizeof(wait), &tag, sizeof(tag), &bytes, nullptr) ||
        tag == BATTERY_TAG_INVALID)
        return false;   // slot is empty

    b.slot = slot;
    b.deviceName   = QueryString(h, tag, BatteryDeviceName);
    b.manufacturer = QueryString(h, tag, BatteryManufactureName);
    b.serialNumber = QueryString(h, tag, BatterySerialNumber);
    b.uniqueId     = QueryString(h, tag, BatteryUniqueID);

    BATTERY_MANUFACTURE_DATE date = {};
    if (QueryInfo(h, tag, BatteryManufactureDate, &date, sizeof(date)) &&
        date.Year >= 1990 && date.Year <= 2100 && date.Month >= 1 && date.Month <= 12 && date.Day >= 1 && date.Day <= 31)
    {
        b.manufactureYear = date.Year;
        b.manufactureMonth = date.Month;
        b.manufactureDay = date.Day;
    }

    ULONG tenthsKelvin = 0;
    if (QueryInfo(h, tag, BatteryTemperature, &tenthsKelvin, sizeof(tenthsKelvin)) && tenthsKelvin > 0)
    {
        b.temperatureC = tenthsKelvin / 10.0 - 273.15;
        b.temperatureKnown = b.temperatureC > -40.0 && b.temperatureC < 150.0;
    }

    BATTERY_INFORMATION bi = {};
    const bool haveInfo = QueryInfo(h, tag, BatteryInformation, &bi, sizeof(bi));
    if (haveInfo)
    {
        char chem[5] = {};
        memcpy(chem, bi.Chemistry, 4);
        b.chemistry = CString(chem).Trim();
        b.rechargeable = bi.Technology == 1;
        b.systemBattery = (bi.Capabilities & BATTERY_SYSTEM_BATTERY) != 0;
        b.relativeCapacity = (bi.Capabilities & BATTERY_CAPACITY_RELATIVE) != 0;
        b.shortTerm = (bi.Capabilities & BATTERY_IS_SHORT_TERM) != 0;
        b.designCapacity = bi.DesignedCapacity;
        b.fullChargeCapacity = bi.FullChargedCapacity;
        b.defaultAlert1 = bi.DefaultAlert1;
        b.defaultAlert2 = bi.DefaultAlert2;
        b.criticalBias = bi.CriticalBias;
        b.cycleCount = bi.CycleCount;

        if (bi.DesignedCapacity > 0 && bi.DesignedCapacity != BATTERY_UNKNOWN_CAPACITY &&
            bi.FullChargedCapacity != BATTERY_UNKNOWN_CAPACITY && !b.relativeCapacity)
            b.healthPercent = 100.0 * double(bi.FullChargedCapacity) / double(bi.DesignedCapacity);
    }
    else
    {
        b.designCapacity = b.fullChargeCapacity = BATTERY_UNKNOWN_CAPACITY;
    }

    BATTERY_WAIT_STATUS bws = {};
    bws.BatteryTag = tag;
    BATTERY_STATUS bs = {};
    if (DeviceIoControl(h, IOCTL_BATTERY_QUERY_STATUS, &bws, sizeof(bws), &bs, sizeof(bs), &bytes, nullptr))
    {
        b.statusKnown = true;
        b.powerState = bs.PowerState;
        b.currentCapacity = bs.Capacity;
        b.voltageMv = bs.Voltage;
        b.rateKnown = bs.Rate != BATTERY_UNKNOWN_RATE;
        b.rateMw = b.rateKnown ? LONG(bs.Rate) : 0;

        if (bs.Capacity != BATTERY_UNKNOWN_CAPACITY)
        {
            const ULONG full = b.relativeCapacity ? 100 : b.fullChargeCapacity;
            if (full > 0 && full != BATTERY_UNKNOWN_CAPACITY)
                b.chargePercent = min(100.0, 100.0 * double(bs.Capacity) / double(full));
        }

        if ((bs.PowerState & BATTERY_CHARGING) && b.rateKnown && b.rateMw > 0 && !b.relativeCapacity &&
            bs.Capacity != BATTERY_UNKNOWN_CAPACITY && b.fullChargeCapacity != BATTERY_UNKNOWN_CAPACITY &&
            b.fullChargeCapacity > bs.Capacity)
            b.secondsToFull = ULONG(ULONGLONG(b.fullChargeCapacity - bs.Capacity) * 3600ULL / ULONGLONG(b.rateMw));
    }

    ULONG seconds = BATTERY_UNKNOWN_TIME;
    if (QueryInfo(h, tag, BatteryEstimatedTime, &seconds, sizeof(seconds)))
        b.estimatedSeconds = seconds;

    return true;
}

CString CBatteryMonitor::ActivePowerPlan()
{
    GUID* scheme = nullptr;
    if (PowerGetActiveScheme(nullptr, &scheme) != ERROR_SUCCESS || !scheme)
        return CString();

    CString name;
    DWORD size = 0;
    if (PowerReadFriendlyName(nullptr, scheme, nullptr, nullptr, nullptr, &size) == ERROR_SUCCESS && size > 0)
    {
        std::vector<BYTE> buf(size + sizeof(WCHAR), 0);
        if (PowerReadFriendlyName(nullptr, scheme, nullptr, nullptr, buf.data(), &size) == ERROR_SUCCESS)
            name = reinterpret_cast<LPCWSTR>(buf.data());
    }
    LocalFree(scheme);
    return name;
}
