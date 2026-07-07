#pragma once
#include "transaction.h"
#include "lock_manager.h"
#include "../storage/disk_manager.h"
#include <string>

class TransactionManager {
private:
    uint32_t next_txn_id{1};
    LockManager &lock_manager;
    DiskManager &disk_manager;
public:
    TransactionManager(LockManager &lm, DiskManager &dm) : lock_manager(lm), disk_manager(dm) {}

    Transaction* Begin() {
        Transaction* txn = new Transaction(next_txn_id++);
        disk_manager.WriteWAL("BEGIN Txn:" + std::to_string(txn->GetTxnId()) + "\n");
        return txn;
    }

    void Commit(Transaction* txn) {
        txn->SetState(TxnState::SHRINKING);
        disk_manager.WriteWAL("COMMIT Txn:" + std::to_string(txn->GetTxnId()) + "\n");
        for (const auto &rid : txn->GetHeldLocks()) {
            lock_manager.Release(txn->GetTxnId(), rid);
        }
        txn->SetState(TxnState::COMMITTED);
        delete txn;
    }
};