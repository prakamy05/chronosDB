#pragma once
#include "../common/types.h"
#include <vector>

class Transaction {
private:
    uint32_t txn_id;
    TxnState state{TxnState::GROWING};
    std::vector<RID> held_locks;
public:
    Transaction(uint32_t id) : txn_id(id) {}
    uint32_t GetTxnId() const { return txn_id; }
    void AddLock(RID rid) { held_locks.push_back(rid); }
    const std::vector<RID>& GetHeldLocks() const { return held_locks; }
    void SetState(TxnState s) { state = s; }
};