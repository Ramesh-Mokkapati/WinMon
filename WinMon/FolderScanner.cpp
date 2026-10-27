// FolderScanner.cpp
#include "pch.h"
#include "FolderScanner.h"

namespace
{
    ULONGLONG ToU64(const FILETIME& ft)
    {
        return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }

    // Cloud placeholders (OneDrive "online-only") report their full size but occupy no disk space.
    constexpr DWORD kNotOnDisk = FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN;
}

std::wstring FolderNode::FullPath() const
{
    if (!parent)
        return name;
    std::wstring base = parent->FullPath();
    if (filesEntry)
        return base;
    if (!base.empty() && base.back() != L'\\')
        base += L'\\';
    return base + name;
}

CFolderScanner::~CFolderScanner()
{
    Cancel();
}

void CFolderScanner::Start(const std::wstring& root)
{
    m_rootPath = root;
    m_root = std::make_unique<FolderNode>();
    m_root->name = root;

    DWORD sectorsPerCluster = 0, bytesPerSector = 0, freeClusters = 0, totalClusters = 0;
    if (GetDiskFreeSpaceW(root.c_str(), &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters))
        m_clusterSize = sectorsPerCluster * bytesPerSector;

    m_startTick = GetTickCount64();
    m_coordinator = std::thread(&CFolderScanner::Coordinator, this);
}

void CFolderScanner::Cancel()
{
    m_cancel = true;
    m_queueCv.notify_all();
    if (m_coordinator.joinable())
        m_coordinator.join();
}

ULONGLONG CFolderScanner::ElapsedMs() const
{
    const ULONGLONG end = m_endTick ? m_endTick.load() : GetTickCount64();
    return end - m_startTick;
}

CFolderScanner::Progress CFolderScanner::GetProgress() const
{
    Progress p;
    p.files = m_files;
    p.folders = m_folders;
    p.allocated = m_allocated;
    std::lock_guard<std::mutex> lock(m_pathLock);
    p.currentPath = m_currentPath;
    return p;
}

void CFolderScanner::Coordinator()
{
    // "\\?\" lifts the MAX_PATH limit for deep trees (node_modules, WinSxS, ...).
    std::wstring path = L"\\\\?\\" + m_rootPath;
    while (!path.empty() && path.back() == L'\\')
        path.pop_back();

    {
        std::lock_guard<std::mutex> lock(m_queueLock);
        m_queue.push_back({ m_root.get(), path });
        m_pending = 1;
    }

    // Directory enumeration is latency-bound, so a few parallel workers help a lot on SSDs.
    const unsigned workers = max(2u, min(8u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (unsigned i = 0; i < workers; ++i)
        pool.emplace_back(&CFolderScanner::Worker, this);
    for (auto& t : pool)
        t.join();

    if (!m_cancel)
        Aggregate(*m_root);

    m_endTick = GetTickCount64();
    m_done = true;
}

void CFolderScanner::Worker()
{
    for (;;)
    {
        WorkItem item;
        {
            std::unique_lock<std::mutex> lock(m_queueLock);
            m_queueCv.wait(lock, [this] { return m_cancel || !m_queue.empty() || m_pending == 0; });
            if (m_cancel || (m_queue.empty() && m_pending == 0))
                return;
            item = std::move(m_queue.front());
            m_queue.pop_front();
        }

        ScanFolder(item);

        std::lock_guard<std::mutex> lock(m_queueLock);
        if (--m_pending == 0)
            m_queueCv.notify_all();   // everything scanned: release the idle workers
    }
}

void CFolderScanner::ScanFolder(const WorkItem& item)
{
    FolderNode& node = *item.node;

    if ((m_folders.fetch_add(1) & 0xFF) == 0)
    {
        std::lock_guard<std::mutex> lock(m_pathLock);
        m_currentPath = item.path.substr(4);   // drop "\\?\"
    }

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileExW((item.path + L"\\*").c_str(), FindExInfoBasic, &fd,
                                    FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        const DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES)
            node.accessDenied = true;
        return;
    }

    std::vector<WorkItem> subfolders;
    do
    {
        if (m_cancel)
            break;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if ((fd.cFileName[0] == L'.' && fd.cFileName[1] == 0) ||
                (fd.cFileName[0] == L'.' && fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))
                continue;
            // Junctions, symlinks and mount points point at data counted elsewhere (or loop).
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                continue;

            auto child = std::make_unique<FolderNode>();
            child->name = fd.cFileName;
            child->parent = &node;
            child->ownLastWrite = ToU64(fd.ftLastWriteTime);
            subfolders.push_back({ child.get(), item.path + L"\\" + fd.cFileName });
            node.children.push_back(std::move(child));
        }
        else
        {
            const ULONGLONG size = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            const ULONGLONG onDisk = AllocatedSize(item.path + L"\\" + fd.cFileName, fd);
            node.ownSize += size;
            node.ownAllocated += onDisk;
            ++node.ownFiles;
            node.ownLastWrite = max(node.ownLastWrite, ToU64(fd.ftLastWriteTime));
            m_files.fetch_add(1, std::memory_order_relaxed);
            m_allocated.fetch_add(onDisk, std::memory_order_relaxed);
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (!subfolders.empty() && !m_cancel)
    {
        std::lock_guard<std::mutex> lock(m_queueLock);
        m_pending += subfolders.size();
        for (auto& w : subfolders)
            m_queue.push_back(std::move(w));
        m_queueCv.notify_all();
    }
}

ULONGLONG CFolderScanner::AllocatedSize(const std::wstring& filePath, const WIN32_FIND_DATAW& fd) const
{
    if (fd.dwFileAttributes & kNotOnDisk)
        return 0;

    ULONGLONG size = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    if (fd.dwFileAttributes & (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_SPARSE_FILE))
    {
        DWORD high = 0;
        const DWORD low = GetCompressedFileSizeW(filePath.c_str(), &high);
        if (low != INVALID_FILE_SIZE || GetLastError() == NO_ERROR)
            size = (static_cast<ULONGLONG>(high) << 32) | low;
    }
    // Round up to whole clusters.
    return (size + m_clusterSize - 1) / m_clusterSize * m_clusterSize;
}

void CFolderScanner::Aggregate(FolderNode& node)
{
    node.size = node.ownSize;
    node.allocated = node.ownAllocated;
    node.files = node.ownFiles;
    node.folders = node.children.size();
    node.lastWrite = node.ownLastWrite;
    node.deniedFolders = node.accessDenied ? 1 : 0;

    for (auto& child : node.children)
    {
        Aggregate(*child);
        node.size += child->size;
        node.allocated += child->allocated;
        node.files += child->files;
        node.folders += child->folders;
        node.lastWrite = max(node.lastWrite, child->lastWrite);
        node.deniedFolders += child->deniedFolders;
    }

    // When a folder has both subfolders and loose files, show the files as their own row
    // so the children always add up to the parent.
    if (!node.children.empty() && node.ownFiles > 0)
    {
        auto files = std::make_unique<FolderNode>();
        files->name = L"<files in this folder>";
        files->parent = &node;
        files->filesEntry = true;
        files->size = node.ownSize;
        files->allocated = node.ownAllocated;
        files->files = node.ownFiles;
        files->lastWrite = node.ownLastWrite;
        node.children.push_back(std::move(files));
    }
}
