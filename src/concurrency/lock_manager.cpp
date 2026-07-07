#include "lock_manager.h"

bool LockManager::Acquire(uint32_t txn_id, RID rid, LockMode mode) {
    std::unique_lock<std::mutex> lock(latch);
    auto &req_list = lock_table[rid];
    
    // Add request to the lock table queue
    req_list.push_back({txn_id, mode, false});

    // Check compatibility: Grantable if it's our turn and no conflicting locks precede us
    auto check_granted = [&]() {
        for (const auto &req : req_list) {
            if (req.txn_id == txn_id) {
                return true; 
            }
            // Strict conflict rules: EXCLUSIVE blocks everything; any existing lock blocks an EXCLUSIVE request
            if (mode == LockMode::EXCLUSIVE || req.mode == LockMode::EXCLUSIVE) {
                return false;
            }
        }
        return true;
    };

    // Block the thread until lock compatibility requirements are met
    while (!check_granted()) {
        cv.wait(lock);
    }

    // Mark the request as granted
    for (auto &req : req_list) {
        if (req.txn_id == txn_id) { 
            req.granted = true; 
            break; 
        }
    }
    return true;
}

void LockManager::Release(uint32_t txn_id, RID rid) {
    std::lock_guard<std::mutex> lock(latch);
    if (lock_table.find(rid) == lock_table.end()) return;
    
    auto &req_list = lock_table[rid];
    req_list.remove_if([txn_id](const LockRequest &r) { return r.txn_id == txn_id; });
    
    if (req_list.empty()) {
        lock_table.erase(rid);
    }
    
    // Alert waiting transaction threads to re-evaluate their lock conflict status
    cv.notify_all();
}