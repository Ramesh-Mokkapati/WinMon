// BatteryMonitor.h : power source and battery telemetry (GetSystemPowerStatus, battery IOCTLs)
#pragma once
#include "Metrics.h"

class CBatteryMonitor
{
public:
    // Fills the system-wide power status and one entry per battery device.
    void Collect(PowerInfo& out);

private:
    static void CollectBatteries(std::vector<BatteryInfo>& out);
    static bool QueryBattery(HANDLE hBattery, int slot, BatteryInfo& b);
    static CString ActivePowerPlan();
};
