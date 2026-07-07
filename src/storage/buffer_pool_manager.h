#pragma once
#include "disk_manager.h"
#include "page.h"
#include <unordered_map>
#include <list>
#include <mutex>

class BufferPoolManager {
private:
    DiskManager &disk_manager;
    Page pool[BUFFER_POOL_SIZE];
    std::unordered_map<page_id_t, size_t> page_table;
    std::list<size_t> lru_list;
    bool is_dirty[BUFFER_POOL_SIZE]{false};
    int pin_count[BUFFER_POOL_SIZE]{0};
    std::mutex latch;

    int GetVictimFrame();
public:
    BufferPoolManager(DiskManager &dm);
    Page* FetchPage(page_id_t pid);
    void UnpinPage(page_id_t pid, bool dirty);
    Page* NewPage(page_id_t &pid);
    void FlushAllPages(); // Flushes all dirty buffer blocks straight to the disk manager
};