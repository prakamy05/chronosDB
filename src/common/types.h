#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

constexpr size_t PAGE_SIZE = 4096;
constexpr size_t BUFFER_POOL_SIZE = 10;

using page_id_t = int32_t;
using lsn_t = uint64_t;
constexpr page_id_t INVALID_PAGE_ID = -1;

enum class PageType { DATA_PAGE, INDEX_PAGE };

struct RID {
    page_id_t page_id{INVALID_PAGE_ID};
    uint32_t slot_num{0};

    bool operator==(const RID &other) const {
        return page_id == other.page_id && slot_num == other.slot_num;
    }
};

namespace std {
    template<> struct hash<RID> {
        size_t operator()(const RID& rid) const {
            return hash<page_id_t>()(rid.page_id) ^ (hash<uint32_t>()(rid.slot_num) << 1);
        }
    };
}

enum class LogType { BEGIN, COMMIT, ABORT, UPDATE };
struct LogRecord {
    lsn_t lsn;
    uint32_t txn_id;
    LogType type;
    page_id_t page_id{INVALID_PAGE_ID};
    uint32_t slot_num{0};
    char before_image[128]{0}; 
    char after_image[128]{0};  
};

enum class DataType { INT, VARCHAR };
enum class TxnState { GROWING, SHRINKING, COMMITTED, ABORTED };
enum class LockMode { SHARED, EXCLUSIVE };

// Added for Expression, Sorting, and Aggregation execution processing
enum class ExpressionType { CONSTANT, COLUMN_REF, ADD, SUBSTRACT, MULTIPLY };
enum class AggregationType { COUNT, SUM, AVG, MIN, MAX };