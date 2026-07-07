#pragma once
#include "../common/types.h"
#include "../storage/buffer_pool_manager.h"
#include "../concurrency/lock_manager.h"
#include "../concurrency/transaction.h"
#include "catalog.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <stdexcept>

// Structural runtime expression model representation
struct AbstractExpression {
    ExpressionType type;
    size_t col_idx{0};              // Used for COLUMN_REF
    std::string val;                // Used for CONSTANT
    std::unique_ptr<AbstractExpression> left;  // Used for Binary operations
    std::unique_ptr<AbstractExpression> right; // Used for Binary operations

    // Custom move constructor to safely transfer unique_ptrs
    AbstractExpression(AbstractExpression&&) noexcept = default;
    AbstractExpression& operator=(AbstractExpression&&) noexcept = default;

    // Explicitly delete copy constructor to prevent implicit vector sizing traps
    AbstractExpression(const AbstractExpression&) = delete;
    AbstractExpression& operator=(const AbstractExpression&) = delete;

    std::string Evaluate(const std::vector<std::string>& tuple) const {
        if (type == ExpressionType::CONSTANT) return val;
        if (type == ExpressionType::COLUMN_REF) {
            if (col_idx < tuple.size()) return tuple[col_idx];
            return "";
        }
        
        // Binary math evaluations 
        if (left && right) {
            try {
                long long l_val = std::stoll(left->Evaluate(tuple));
                long long r_val = std::stoll(right->Evaluate(tuple));
                if (type == ExpressionType::ADD) return std::to_string(l_val + r_val);
                if (type == ExpressionType::SUBSTRACT) return std::to_string(l_val - r_val);
                if (type == ExpressionType::MULTIPLY) return std::to_string(l_val * r_val);
            } catch (...) {
                return "";
            }
        }
        return "";
    }
};

class AbstractExecutor {
public:
    virtual void Init() = 0;
    virtual bool Next(std::vector<std::string> &tuple, RID &rid) = 0;
    virtual ~AbstractExecutor() = default;
};

class SeqScanExecutor : public AbstractExecutor {
private:
    BufferPoolManager &bpm;
    TableMetadata &meta;
    size_t page_idx{0};
    uint32_t slot_idx{0};
    
    // Explicit tracking states to fix index-shifting during deletion cycles
    size_t last_page_idx{0};
    uint32_t last_slot_idx{0};
    bool has_current{false};
    
    Transaction* txn{nullptr};
    LockManager* lock_mgr{nullptr};
public:
    // Legacy Constructor for backward compatibility with engine.h
    SeqScanExecutor(BufferPoolManager &bpm, TableMetadata &meta) 
        : bpm(bpm), meta(meta), txn(nullptr), lock_mgr(nullptr) {}

    // Upgraded Transaction-Aware Constructor
    SeqScanExecutor(BufferPoolManager &bpm, TableMetadata &meta, Transaction* txn, LockManager &lm) 
        : bpm(bpm), meta(meta), txn(txn), lock_mgr(&lm) {}
    
    void Init() override { 
        page_idx = 0; 
        slot_idx = 0; 
        has_current = false;
    }
    
    bool Next(std::vector<std::string> &tuple, RID &rid) override {
        while (page_idx < meta.root_pages.size()) {
            page_id_t pid = meta.root_pages[page_idx];
            Page* page = bpm.FetchPage(pid);
            if (!page) return false;
            
            std::string raw_data;
            bool record_fetched = false;

            // Use lock manager and txn tracking if present
            if (lock_mgr != nullptr && txn != nullptr) {
                // If your Page class requires a transaction structure, pass it or its ID
                record_fetched = page->GetRecord(slot_idx, raw_data); 
            } else {
                record_fetched = page->GetRecord(slot_idx, raw_data); // Direct fallback read
            }

            if (record_fetched) {
                tuple = TupleSerializer::Deserialize(raw_data);
                rid = RID{pid, slot_idx};
                
                // Track where the returned tuple actually lives before updating index pointers
                last_page_idx = page_idx;
                last_slot_idx = slot_idx;
                has_current = true;

                slot_idx++;
                if (slot_idx >= page->get_slot_count()) {
                    slot_idx = 0;
                    page_idx++;
                }
                bpm.UnpinPage(pid, false);
                return true;
            }
            
            slot_idx++;
            if (slot_idx >= page->get_slot_count()) {
                slot_idx = 0;
                page_idx++;
            }
            bpm.UnpinPage(pid, false);
        }
        return false;
    }

    bool DeleteCurrentRecord(uint32_t current_txn_id = 0) {
        if (!has_current || last_page_idx >= meta.root_pages.size()) return false;
        page_id_t pid = meta.root_pages[last_page_idx];
        Page* page = bpm.FetchPage(pid);
        if (!page) return false;
        
        bool status = page->DeleteRecord(last_slot_idx, current_txn_id);
        bpm.UnpinPage(pid, true);
        return status;
    }
};

class IndexScanExecutor : public AbstractExecutor {
private:
    BufferPoolManager &bpm;
    const IndexMetadata &index_meta; 
    std::string target_key;
    bool executed{false};
    
    Transaction* txn{nullptr};
    LockManager* lock_mgr{nullptr};
public:
    // Legacy Constructor fallback for engine.h
    IndexScanExecutor(BufferPoolManager &bpm, const IndexMetadata &idx, std::string key)
        : bpm(bpm), index_meta(idx), target_key(key), txn(nullptr), lock_mgr(nullptr) {}

    // Upgraded Transaction-Aware Constructor
    IndexScanExecutor(BufferPoolManager &bpm, const IndexMetadata &idx, std::string key, Transaction* txn, LockManager &lm)
        : bpm(bpm), index_meta(idx), target_key(key), txn(txn), lock_mgr(&lm) {}

    void Init() override { executed = false; }

    bool Next(std::vector<std::string> &tuple, RID &rid) override {
        if (executed) return false;
        
        Page* root_page = bpm.FetchPage(index_meta.root_page_id);
        if (!root_page) return false;
        
        std::string serialized_records;
        for (uint32_t idx = 0; idx < root_page->get_slot_count(); ++idx) {
            if (root_page->GetRecord(idx, serialized_records)) {
                auto items = TupleSerializer::Deserialize(serialized_records);
                if (items.size() >= 3 && items[0] == target_key) {
                    rid.page_id = std::stoi(items[1]);
                    rid.slot_num = std::stoul(items[2]);
                    
                    Page* data_page = bpm.FetchPage(rid.page_id);
                    if (data_page) {
                        std::string raw_data;
                        bool record_fetched = false;

                        if (lock_mgr != nullptr && txn != nullptr) {
                            record_fetched = data_page->GetRecord(rid.slot_num, raw_data);
                        } else {
                            record_fetched = data_page->GetRecord(rid.slot_num, raw_data);
                        }

                        if (record_fetched) {
                            tuple = TupleSerializer::Deserialize(raw_data);
                            bpm.UnpinPage(rid.page_id, false);
                            bpm.UnpinPage(index_meta.root_page_id, false);
                            executed = true;
                            return true;
                        }
                        bpm.UnpinPage(rid.page_id, false);
                    }
                }
            }
        }
        bpm.UnpinPage(index_meta.root_page_id, false);
        return false;
    }
};

class FilterExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    std::string target_val;
    size_t target_col_idx; // FIXED: Keep track of target column mapping
public:
    FilterExecutor(std::unique_ptr<AbstractExecutor> c, std::string val, size_t col_idx = 0) 
        : child(std::move(c)), target_val(val), target_col_idx(col_idx) {}
        
    void Init() override { child->Init(); }
    
    bool Next(std::vector<std::string> &tuple, RID &rid) override {
        while (child->Next(tuple, rid)) {
            // FIXED: Dynamically match targeted variable attributes rather than index 0 explicitly
            if (target_val.empty() || (target_col_idx < tuple.size() && tuple[target_col_idx] == target_val)) {
                return true;
            }
        }
        return false;
    }
};

class ProjectionExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    std::vector<AbstractExpression> expressions;
public:
    ProjectionExecutor(std::unique_ptr<AbstractExecutor> c, std::vector<AbstractExpression> exprs)
        : child(std::move(c)), expressions(std::move(exprs)) {}

    void Init() override { child->Init(); }

    bool Next(std::vector<std::string> &tuple, RID &rid) override {
        std::vector<std::string> raw_tuple;
        if (child->Next(raw_tuple, rid)) {
            tuple.clear();
            for (const auto& expr : expressions) {
                tuple.push_back(expr.Evaluate(raw_tuple));
            }
            return true;
        }
        return false;
    }
};

class SortExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    size_t sort_col_idx;
    bool ascending;
    std::vector<std::pair<std::vector<std::string>, RID>> sorted_buffer;
    size_t cursor{0};
public:
    SortExecutor(std::unique_ptr<AbstractExecutor> c, size_t col_idx, bool asc = true)
        : child(std::move(c)), sort_col_idx(col_idx), ascending(asc) {}

    void Init() override {
        child->Init();
        sorted_buffer.clear();
        cursor = 0;

        std::vector<std::string> t;
        RID r;
        while (child->Next(t, r)) {
            sorted_buffer.push_back({t, r});
        }

        std::sort(sorted_buffer.begin(), sorted_buffer.end(), [this](const auto& a, const auto& b) {
            if (sort_col_idx >= a.first.size() || sort_col_idx >= b.first.size()) return false;
            if (a.first[sort_col_idx] == b.first[sort_col_idx]) return false;
            try {
                double a_num = std::stod(a.first[sort_col_idx]);
                double b_num = std::stod(b.first[sort_col_idx]);
                return ascending ? (a_num < b_num) : (a_num > b_num);
            } catch (...) {
                return ascending ? (a.first[sort_col_idx] < b.first[sort_col_idx]) 
                                 : (a.first[sort_col_idx] > b.first[sort_col_idx]);
            }
        });
    }

    bool Next(std::vector<std::string> &tuple, RID &rid) override {
        if (cursor < sorted_buffer.size()) {
            tuple = sorted_buffer[cursor].first;
            rid = sorted_buffer[cursor].second;
            cursor++;
            return true;
        }
        return false;
    }
};

class HashAggregationExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    size_t group_by_col_idx;
    size_t agg_col_idx;
    AggregationType agg_type;
    
    std::unordered_map<std::string, std::vector<double>> agg_map; 
    std::vector<std::pair<std::string, std::string>> final_results;
    size_t cursor{0};
public:
    HashAggregationExecutor(std::unique_ptr<AbstractExecutor> c, size_t grp_idx, size_t agg_idx, AggregationType type)
        : child(std::move(c)), group_by_col_idx(grp_idx), agg_col_idx(agg_idx), agg_type(type) {}

    void Init() override {
        child->Init();
        agg_map.clear();
        final_results.clear();
        cursor = 0;

        std::vector<std::string> t;
        RID r;
        while (child->Next(t, r)) {
            std::string group_key = (group_by_col_idx < t.size()) ? t[group_by_col_idx] : "GLOBAL_GROUP";
            double val = 0.0;
            if (agg_col_idx < t.size()) {
                try { val = std::stod(t[agg_col_idx]); } catch(...) { val = 0.0; }
            }
            agg_map[group_key].push_back(val);
        }

        for (auto const& [group, values] : agg_map) {
            if (values.empty()) continue;
            double result = 0.0;
            if (agg_type == AggregationType::COUNT) {
                result = values.size();
            } else if (agg_type == AggregationType::SUM || agg_type == AggregationType::AVG) {
                for (double v : values) result += v;
                if (agg_type == AggregationType::AVG) result /= values.size();
            } else if (agg_type == AggregationType::MIN) {
                result = *std::min_element(values.begin(), values.end());
            } else if (agg_type == AggregationType::MAX) {
                result = *std::max_element(values.begin(), values.end());
            }
            final_results.push_back({group, std::to_string(result)});
        }
    }

    bool Next(std::vector<std::string> &tuple, RID &rid) override {
        if (cursor < final_results.size()) {
            tuple = { final_results[cursor].first, final_results[cursor].second };
            rid = RID{INVALID_PAGE_ID, static_cast<uint32_t>(cursor)};
            cursor++;
            return true;
        }
        return false;
    }
};

class NestedLoopJoinExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> left_child;
    std::unique_ptr<AbstractExecutor> right_child;
    size_t left_col_idx;
    size_t right_col_idx;
    
    std::vector<std::string> left_tuple;
    RID left_rid;
    bool has_more_left;

public:
    NestedLoopJoinExecutor(
        std::unique_ptr<AbstractExecutor> left,
        std::unique_ptr<AbstractExecutor> right,
        size_t left_idx,
        size_t right_idx)
        : left_child(std::move(left)), right_child(std::move(right)),
          left_col_idx(left_idx), right_col_idx(right_idx), has_more_left(false) {}

    void Init() override {
        left_child->Init();
        right_child->Init();
        has_more_left = left_child->Next(left_tuple, left_rid);
    }

    bool Next(std::vector<std::string> &tuple, RID &rid) override {
        std::vector<std::string> right_tuple;
        RID right_rid;

        while (has_more_left) {
            while (right_child->Next(right_tuple, right_rid)) {
                if (left_tuple.size() > left_col_idx && right_tuple.size() > right_col_idx) {
                    if (left_tuple[left_col_idx] == right_tuple[right_col_idx]) {
                        tuple = left_tuple;
                        tuple.insert(tuple.end(), right_tuple.begin(), right_tuple.end());
                        rid = left_rid; 
                        return true;
                    }
                }
            }
            right_child->Init();
            has_more_left = left_child->Next(left_tuple, left_rid);
        }
        return false;
    }
};