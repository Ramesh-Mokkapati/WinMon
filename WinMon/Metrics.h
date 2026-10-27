// Metrics.h : plain data structures exchanged between the worker thread and the UI
#pragma once

struct CpuMetrics
{
    double   totalUsage = 0.0;        // % across all logical processors (PDH)
    DWORD    logicalProcessors = 0;
    CString  processorName;
};

struct MemoryMetrics
{
    DWORD     memoryLoad = 0;         // % physical memory in use
    ULONGLONG totalPhys = 0;
    ULONGLONG availPhys = 0;
    ULONGLONG totalPageFile = 0;      // commit limit
    ULONGLONG availPageFile = 0;
};

struct ProcessInfo
{
    DWORD     pid = 0;
    DWORD     parentPid = 0;
    CString   name;
    CString   imagePath;
    DWORD     threads = 0;
    DWORD     priorityClass = 0;      // 0 when it could not be queried
    LONG      basePriority = 0;
    double    cpuPercent = 0.0;
    SIZE_T    workingSet = 0;
    SIZE_T    privateBytes = 0;
    double    ioReadPerSec = 0.0;
    double    ioWritePerSec = 0.0;
    bool      accessDenied = false;
};

struct VolumeInfo
{
    CString   root;                   // "C:\"
    CString   label;
    CString   fileSystem;
    ULONGLONG totalBytes = 0;
    ULONGLONG freeBytes = 0;          // total free
    ULONGLONG availBytes = 0;         // free for the caller
};

struct DiskIoMetrics
{
    double readBytesPerSec = 0.0;     // \PhysicalDisk(_Total)
    double writeBytesPerSec = 0.0;
    bool   available = false;
};

struct AdapterInfo
{
    ULONG64   luid = 0;
    CString   name;
    CString   description;
    CString   type;
    CString   status;
    ULONG64   linkSpeedBps = 0;
    ULONG64   totalIn = 0;
    ULONG64   totalOut = 0;
    double    inPerSec = 0.0;
    double    outPerSec = 0.0;
    bool      isUp = false;
};

struct ConnectionInfo
{
    CString   protocol;               // TCP / TCPv6 / UDP / UDPv6
    CString   localAddr;
    USHORT    localPort = 0;
    CString   remoteAddr;
    USHORT    remotePort = 0;
    CString   state;
    DWORD     pid = 0;
    CString   processName;
};

struct SystemSummary
{
    DWORD     processCount = 0;
    DWORD     threadCount = 0;
    DWORD     handleCount = 0;
    ULONGLONG uptimeMs = 0;
};

// Complete snapshot published by the worker. The *Seq counters increase every
// time the matching section is refreshed so the UI only rebuilds what changed.
struct SystemSnapshot
{
    CpuMetrics                  cpu;
    MemoryMetrics               memory;
    DiskIoMetrics               diskIo;
    SystemSummary               summary;
    double                      netInPerSec = 0.0;
    double                      netOutPerSec = 0.0;

    std::vector<ProcessInfo>    processes;
    std::vector<VolumeInfo>     volumes;
    std::vector<AdapterInfo>    adapters;
    std::vector<ConnectionInfo> connections;

    ULONGLONG                   sampleSeq = 0;
    ULONGLONG                   processSeq = 0;
    ULONGLONG                   volumeSeq = 0;
    ULONGLONG                   networkSeq = 0;
    SYSTEMTIME                  timestamp = {};
};
