#include "buffer_pool_manager.h"

BufferPoolManager::BufferPoolManager(DiskManager &dm) : disk_manager(dm) {}

int BufferPoolManager::GetVictimFrame() {
    for (auto it = lru_list.rbegin(); it != lru_list.rend(); ++it) {
        if (pin_count[*it] == 0) {
            size_t frame = *it;
            lru_list.erase(std::next(it).base());
            return static_cast<int>(frame);
        }
    }
    return -1;
}

Page* BufferPoolManager::FetchPage(page_id_t pid) {
    std::lock_guard<std::mutex> lock(latch);
    if (page_table.count(pid)) {
        size_t frame = page_table[pid];
        pin_count[frame]++;
        lru_list.remove(frame);
        lru_list.push_front(frame);
        return &pool[frame];
    }
    int frame = -1;
    if (page_table.size() < BUFFER_POOL_SIZE) {
        frame = static_cast<int>(page_table.size());
    } else {
        frame = GetVictimFrame();
        if (frame == -1) return nullptr;
        page_id_t old_pid = pool[frame].get_page_id();
        if (is_dirty[frame]) {
            disk_manager.WritePage(old_pid, pool[frame].data);
        }
        page_table.erase(old_pid);
    }
    page_table[pid] = frame;
    pool[frame].Reset();
    disk_manager.ReadPage(pid, pool[frame].data);
    pool[frame].set_page_id(pid);
    pin_count[frame] = 1;
    is_dirty[frame] = false;
    lru_list.push_front(frame);
    return &pool[frame];
}

void BufferPoolManager::UnpinPage(page_id_t pid, bool dirty) {
    std::lock_guard<std::mutex> lock(latch);
    if (!page_table.count(pid)) return;
    size_t frame = page_table[pid];
    if (dirty) is_dirty[frame] = true;
    if (pin_count[frame] > 0) pin_count[frame]--;
}

Page* BufferPoolManager::NewPage(page_id_t &pid) {
    std::lock_guard<std::mutex> lock(latch);
    int frame = -1;
    if (page_table.size() < BUFFER_POOL_SIZE) {
        frame = static_cast<int>(page_table.size());
    } else {
        frame = GetVictimFrame();
        if (frame == -1) return nullptr;
        page_id_t old_pid = pool[frame].get_page_id();
        if (is_dirty[frame]) {
            disk_manager.WritePage(old_pid, pool[frame].data);
        }
        page_table.erase(old_pid);
    }
    pid = disk_manager.AllocatePage();
    page_table[pid] = frame;
    pool[frame].Reset();
    pool[frame].set_page_id(pid);
    pin_count[frame] = 1;
    is_dirty[frame] = true;
    lru_list.push_front(frame);
    return &pool[frame];
}

void BufferPoolManager::FlushAllPages() {
    std::lock_guard<std::mutex> lock(latch);
    for (auto const& [pid, frame] : page_table) {
        if (is_dirty[frame]) {
            disk_manager.WritePage(pid, pool[frame].data);
            is_dirty[frame] = false;
        }
    }
}