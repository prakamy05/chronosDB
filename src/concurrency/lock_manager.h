#pragma once
#include "../common/types.h"
#include <unordered_map>
#include <list>
#include <mutex>
#include <condition_variable>
#include <vector>

// Enum representing the transaction states under MVCC Snapshot Isolation
enum class TransactionState { ACTIVE, COMMITTED, ABORTED };

// Explicit structure tracking transaction snapshots for visible version evaluation
struct MVCCSnapshot {
    uint32_t read_ts;                    // The read timestamp (usually the transaction start sequence)
    std::vector<uint32_t> active_txns;   // In-flight transactions that must be invisible to this snapshot
};

struct LockRequest {
    uint32_t txn_id;
    LockMode mode; // SHARED or EXCLUSIVE
    bool granted;
};

class LockManager {
private:
    std::mutex latch;
    
    // Maps a Tuple Slot ID (RID) to its active lock queue (Write-Write conflict protection)
    std::unordered_map<RID, std::list<LockRequest>> lock_table;
    std::condition_variable cv;

    // Global Active Transaction Directory for MVCC state visibility determination
    std::unordered_map<uint32_t, TransactionState> txn_directory;
    uint32_t global_txn_counter{1};

public:
    // --- MVCC Transaction Lifecycle API ---

    uint32_t BeginTransaction() {
        std::lock_guard<std::mutex> lock(latch);
        uint32_t current_id = global_txn_counter++;
        txn_directory[current_id] = TransactionState::ACTIVE;
        return current_id;
    }

    MVCCSnapshot GetSnapshot(uint32_t txn_id) {
        std::lock_guard<std::mutex> lock(latch);
        MVCCSnapshot snap;
        snap.read_ts = global_txn_counter; // Capture the current logical timeline ticker state
        
        for (const auto& [id, state] : txn_directory) {
            if (state == TransactionState::ACTIVE && id != txn_id) {
                snap.active_txns.push_back(id);
            }
        }
        return snap;
    }

    void CommitTransaction(uint32_t txn_id) {
        std::lock_guard<std::mutex> lock(latch);
        txn_directory[txn_id] = TransactionState::COMMITTED;
        cv.notify_all();
    }

    void AbortTransaction(uint32_t txn_id) {
        std::lock_guard<std::mutex> lock(latch);
        txn_directory[txn_id] = TransactionState::ABORTED;
        cv.notify_all();
    }

    TransactionState GetTransactionState(uint32_t txn_id) {
        std::lock_guard<std::mutex> lock(latch);
        if (txn_directory.find(txn_id) == txn_directory.end()) {
            return TransactionState::COMMITTED; // Fallback implicit rule for historical records
        }
        return txn_directory[txn_id];
    }

    // --- Upgraded Locking Layer Declarations (Implementations moved to .cpp) ---
    bool Acquire(uint32_t txn_id, RID rid, LockMode mode);
    void Release(uint32_t txn_id, RID rid);
};