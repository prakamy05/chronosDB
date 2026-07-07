#pragma once
#include "../common/types.h"
#include "../concurrency/lock_manager.h" 
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

// Upgraded to handle Multi-Version Concurrency Control Metadata
struct MVCCSlot {
    uint16_t offset;
    uint16_t length;
    uint32_t xmin;  // Transaction ID that created/inserted this row version
    uint32_t xmax;  // Transaction ID that deleted/superseded this row version (0 if active)
};

struct IndexHeader {
    bool is_leaf;
    uint32_t size;
    uint32_t max_size;
    page_id_t next_page_id;
};

struct IndexEntry {
    int32_t key;
    RID rid; 
};

class Page {
public:
    char data[PAGE_SIZE];

    Page() { Reset(); }
    
    void Reset() { 
        std::memset(data, 0, PAGE_SIZE); 
        set_page_id(INVALID_PAGE_ID); 
        set_page_type(PageType::DATA_PAGE);
        set_free_ptr(PAGE_SIZE); 
        set_slot_count(0);
    }

    page_id_t get_page_id() const { return *reinterpret_cast<const page_id_t*>(data); }
    void set_page_id(page_id_t pid) { *reinterpret_cast<page_id_t*>(data) = pid; }

    PageType get_page_type() const { return *reinterpret_cast<const PageType*>(data + 4); }
    void set_page_type(PageType type) { *reinterpret_cast<PageType*>(data + 4) = type; }

    lsn_t get_lsn() const { return *reinterpret_cast<const lsn_t*>(data + 8); }
    void set_lsn(lsn_t lsn) { *reinterpret_cast<lsn_t*>(data + 8) = lsn; }

    uint16_t get_slot_count() const { return *reinterpret_cast<const uint16_t*>(data + 16); }
    void set_slot_count(uint16_t cnt) { *reinterpret_cast<uint16_t*>(data + 16) = cnt; }

    uint16_t get_free_ptr() const { return *reinterpret_cast<const uint16_t*>(data + 18); }
    void set_free_ptr(uint16_t ptr) { *reinterpret_cast<uint16_t*>(data + 18) = ptr; }

    MVCCSlot* get_slots() { return reinterpret_cast<MVCCSlot*>(data + 20); }

    // Overloaded Fallback: System-level insertions (Catalog/Index) default to Transaction 0
    bool InsertRecord(const std::string &serialized, RID &rid) {
        return InsertRecord(serialized, rid, 0);
    }

    // Main MVCC Insertion API
    bool InsertRecord(const std::string &serialized, RID &rid, uint32_t txn_id) {
        if (get_page_type() != PageType::DATA_PAGE) return false;
        uint16_t space_needed = sizeof(MVCCSlot) + serialized.size();
        uint16_t current_slots = get_slot_count();
        uint16_t current_free = get_free_ptr();
        uint16_t current_slots_end = 20 + (current_slots * sizeof(MVCCSlot));

        if (current_free < current_slots_end || (current_free - current_slots_end) < space_needed) {
            return false;
        }

        uint16_t new_free = current_free - serialized.size();
        std::memcpy(data + new_free, serialized.data(), serialized.size());

        MVCCSlot* slots = get_slots();
        slots[current_slots].offset = new_free;
        slots[current_slots].length = serialized.size();
        slots[current_slots].xmin = txn_id; 
        slots[current_slots].xmax = 0;       

        rid.page_id = get_page_id();
        rid.slot_num = current_slots;

        set_free_ptr(new_free);
        set_slot_count(current_slots + 1);
        return true;
    }

    bool DeleteRecord(uint32_t slot_num, uint32_t txn_id) {
        if (get_page_type() != PageType::DATA_PAGE) return false;
        if (slot_num >= get_slot_count()) return false;
        
        MVCCSlot& slot = get_slots()[slot_num];
        if (slot.length == 0 || slot.xmax != 0) return false; 
        
        slot.xmax = txn_id; 
        return true;
    }

    bool IsVersionVisible(uint32_t slot_num, const MVCCSnapshot &snapshot, LockManager &lock_mgr) {
        if (slot_num >= get_slot_count()) return false;
        MVCCSlot slot = get_slots()[slot_num];
        if (slot.length == 0) return false;

        // System entries written by transaction 0 are always universally visible
        if (slot.xmin == 0) return true;

        TransactionState xmin_state = lock_mgr.GetTransactionState(slot.xmin);
        if (xmin_state == TransactionState::ABORTED) return false;
        if (xmin_state == TransactionState::ACTIVE) {
            if (slot.xmin != snapshot.read_ts - 1) return false; 
        } else if (xmin_state == TransactionState::COMMITTED) {
            auto it = std::find(snapshot.active_txns.begin(), snapshot.active_txns.end(), slot.xmin);
            if (it != snapshot.active_txns.end()) return false; 
        }

        if (slot.xmax == 0) return true; 

        TransactionState xmax_state = lock_mgr.GetTransactionState(slot.xmax);
        if (xmax_state == TransactionState::ABORTED) return true; 
        if (xmax_state == TransactionState::ACTIVE) {
            return (slot.xmax != snapshot.read_ts - 1); 
        } else if (xmax_state == TransactionState::COMMITTED) {
            auto it = std::find(snapshot.active_txns.begin(), snapshot.active_txns.end(), slot.xmax);
            if (it != snapshot.active_txns.end()) return true; 
            return false; 
        }

        return true;
    }

    // Overloaded Fallback: Direct physical access ignoring snapshot check (For Catalog/Index maintenance)
    bool GetRecord(uint32_t slot_num, std::string &tuple) {
        if (get_page_type() != PageType::DATA_PAGE) return false;
        if (slot_num >= get_slot_count()) return false;
        MVCCSlot slot = get_slots()[slot_num];
        if (slot.length == 0) return false;
        tuple.assign(data + slot.offset, slot.length);
        return true;
    }

    // Main MVCC Visibility Selection API
    bool GetRecord(uint32_t slot_num, std::string &tuple, const MVCCSnapshot &snapshot, LockManager &lock_mgr) {
        if (get_page_type() != PageType::DATA_PAGE) return false;
        if (!IsVersionVisible(slot_num, snapshot, lock_mgr)) return false;
        
        MVCCSlot slot = get_slots()[slot_num];
        tuple.assign(data + slot.offset, slot.length);
        return true;
    }

    IndexHeader* GetIndexHeader() { return reinterpret_cast<IndexHeader*>(data + 20); }
    IndexEntry* GetIndexEntries() { return reinterpret_cast<IndexEntry*>(data + 20 + sizeof(IndexHeader)); }
};