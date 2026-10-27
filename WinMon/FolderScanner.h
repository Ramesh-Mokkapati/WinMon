// FolderScanner.h : multi-threaded recursive folder size scan of one volume
#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

struct FolderNode
{
    std::wstring name;                 // folder name; the drive root holds "C:\"
    FolderNode*  parent = nullptr;
    std::vector<std::unique_ptr<FolderNode>> children;

    // Recursive totals (filled in once the scan completes).
    ULONGLONG size = 0;                // logical file sizes
    ULONGLONG allocated = 0;           // estimated size on disk
    ULONGLONG files = 0;
    ULONGLONG folders = 0;
    ULONGLONG lastWrite = 0;           // newest FILETIME found below this folder
    ULONGLONG deniedFolders = 0;       // folders below (or this one) that could not be listed

    // Files directly inside this folder (written by the worker that lists it).
    ULONGLONG ownSize = 0;
    ULONGLONG ownAllocated = 0;
    ULONGLONG ownFiles = 0;
    ULONGLONG ownLastWrite = 0;

    bool accessDenied = false;
    bool filesEntry = false;           // pseudo row "<files in this folder>"

    // UI state, touched only on the UI thread after the scan has finished.
    bool expanded = false;
    int  sortStamp = -1;

    bool HasChildren() const { return !children.empty(); }
    std::wstring FullPath() const;
};

class CFolderScanner
{
public:
    struct Progress
    {
        ULONGLONG    files = 0;
        ULONGLONG    folders = 0;
        ULONGLONG    allocated = 0;
        std::wstring currentPath;
    };

    CFolderScanner() = default;
    ~CFolderScanner();
    CFolderScanner(const CFolderScanner&) = delete;
    CFolderScanner& operator=(const CFolderScanner&) = delete;

    // root must be a drive root such as "C:\".
    void Start(const std::wstring& root);
    void Cancel();

    bool IsDone() const      { return m_done; }
    bool IsCancelled() const { return m_cancel; }
    ULONGLONG ElapsedMs() const;
    Progress GetProgress() const;

    // Valid once IsDone() and not cancelled; transfers ownership.
    std::unique_ptr<FolderNode> TakeResult() { return std::move(m_root); }

private:
    struct WorkItem
    {
        FolderNode*  node;
        std::wstring path;   // "\\?\C:\Windows" style, no trailing backslash
    };

    void Coordinator();
    void Worker();
    void ScanFolder(const WorkItem& item);
    ULONGLONG AllocatedSize(const std::wstring& filePath, const WIN32_FIND_DATAW& fd) const;
    static void Aggregate(FolderNode& node);

    std::wstring                m_rootPath;
    std::unique_ptr<FolderNode> m_root;
    DWORD                       m_clusterSize = 4096;

    std::thread                 m_coordinator;
    std::mutex                  m_queueLock;
    std::condition_variable     m_queueCv;
    std::deque<WorkItem>        m_queue;
    size_t                      m_pending = 0;      // queued + being scanned

    std::atomic<bool>           m_cancel { false };
    std::atomic<bool>           m_done { false };
    std::atomic<ULONGLONG>      m_files { 0 };
    std::atomic<ULONGLONG>      m_folders { 0 };
    std::atomic<ULONGLONG>      m_allocated { 0 };
    std::atomic<ULONGLONG>      m_startTick { 0 };
    std::atomic<ULONGLONG>      m_endTick { 0 };

    mutable std::mutex          m_pathLock;
    std::wstring                m_currentPath;
};
