#pragma once
#include "../common/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <algorithm>

struct IndexMetadata {
    std::string index_name;
    std::string table_name;
    std::string column_name;
    size_t col_idx;
    page_id_t root_page_id;
};

struct TableMetadata {
    std::string name;
    std::vector<std::string> col_names;
    std::vector<DataType> col_types;
    std::vector<page_id_t> root_pages;
};

class Catalog {
private:
    std::unordered_map<std::string, TableMetadata> tables;
    std::unordered_map<std::string, IndexMetadata> indexes; // Secondary indexes registry
public:
    void CreateTable(std::string name, std::vector<std::string> cols, std::vector<DataType> types) {
        tables[name] = {name, cols, types, {}};
    }
    
    void AddTableMetadata(const TableMetadata& meta) {
        tables[meta.name] = meta;
    }

    bool DropTable(const std::string &name) {
        if (tables.count(name) == 0) return false;
        tables.erase(name);
        
        // Cascade drop associated table indexes
        auto it = indexes.begin();
        while (it != indexes.end()) {
            if (it->second.table_name == name) {
                it = indexes.erase(it);
            } else {
                ++it;
            }
        }
        return true;
    }

    TableMetadata& GetTable(std::string name) { return tables[name]; }
    bool Exists(std::string name) { return tables.count(name) > 0; }
    const std::unordered_map<std::string, TableMetadata>& GetAllTables() const { return tables; }

    // Secondary Indexes Management
    void CreateIndex(std::string idx_name, std::string t_name, std::string col_name, size_t col_idx, page_id_t root_pid) {
        indexes[idx_name] = {idx_name, t_name, col_name, col_idx, root_pid};
    }

    bool IndexExists(std::string idx_name) { return indexes.count(idx_name) > 0; }
    IndexMetadata& GetIndex(std::string idx_name) { return indexes[idx_name]; }
    const std::unordered_map<std::string, IndexMetadata>& GetAllIndexes() const { return indexes; }

    // Converts active table layouts, types, root pages, and secondary indexes to a string stringstream log
    std::string SerializeCatalog() {
        std::stringstream ss;
        ss << tables.size() << "\n";
        for (auto const& [name, meta] : tables) {
            ss << name << "|" << meta.col_names.size() << "|";
            for (size_t i = 0; i < meta.col_names.size(); ++i) {
                ss << meta.col_names[i] << "," << static_cast<int>(meta.col_types[i]) << (i == meta.col_names.size() - 1 ? "" : ";");
            }
            ss << "|";
            for (size_t i = 0; i < meta.root_pages.size(); ++i) {
                ss << meta.root_pages[i] << (i == meta.root_pages.size() - 1 ? "" : ",");
            }
            ss << "\n";
        }
        
        // Append index payload metadata mapping rules
        ss << "INDEX_COUNT:" << indexes.size() << "\n";
        for (auto const& [idx_name, idx_meta] : indexes) {
            ss << idx_name << "|" << idx_meta.table_name << "|" << idx_meta.column_name << "|" 
               << idx_meta.col_idx << "|" << idx_meta.root_page_id << "\n";
        }
        return ss.str();
    }

    // Restores structural metadata and index parameters back from disk
    void DeserializeCatalog(const std::string& data) {
        if (data.empty()) return;
        tables.clear();
        indexes.clear();
        
        std::stringstream ss(data);
        std::string num_tables_str;
        if (!std::getline(ss, num_tables_str)) return;
        if (num_tables_str.empty()) return;
        
        int num_tables = 0;
        try { num_tables = std::stoi(num_tables_str); } catch (...) { return; }
        
        std::string line;
        for (int t = 0; t < num_tables; ++t) {
            if (!std::getline(ss, line) || line.empty()) continue;
            std::stringstream line_ss(line);
            std::string name, count_str, cols_raw, pages_str;
            
            std::getline(line_ss, name, '|');
            std::getline(line_ss, count_str, '|');
            std::getline(line_ss, cols_raw, '|');
            std::getline(line_ss, pages_str, '|');

            std::vector<std::string> cols;
            std::vector<DataType> types;
            std::stringstream c_ss(cols_raw);
            std::string pair;
            while (std::getline(c_ss, pair, ';')) {
                size_t comma = pair.find(',');
                if (comma != std::string::npos) {
                    cols.push_back(pair.substr(0, comma));
                    types.push_back(static_cast<DataType>(std::stoi(pair.substr(comma + 1))));
                }
            }

            std::vector<page_id_t> pages;
            std::stringstream p_ss(pages_str);
            std::string page_val;
            while (std::getline(p_ss, page_val, ',')) {
                if (!page_val.empty()) pages.push_back(std::stoi(page_val));
            }

            tables[name] = {name, cols, types, pages};
        }
        
        // Parse secondary indexes metadata blocks if present
        while (std::getline(ss, line)) {
            if (line.rfind("INDEX_COUNT:", 0) == 0) continue;
            if (line.empty()) continue;
            
            std::stringstream idx_ss(line);
            std::string idx_name, t_name, col_name, c_idx_str, root_pid_str;
            
            std::getline(idx_ss, idx_name, '|');
            std::getline(idx_ss, t_name, '|');
            std::getline(idx_ss, col_name, '|');
            std::getline(idx_ss, c_idx_str, '|');
            std::getline(idx_ss, root_pid_str, '|');
            
            if(!idx_name.empty() && !t_name.empty()) {
                size_t col_idx = std::stoul(c_idx_str);
                page_id_t root_pid = std::stoi(root_pid_str);
                indexes[idx_name] = {idx_name, t_name, col_name, col_idx, root_pid};
            }
        }
    }
};

class TupleSerializer {
private:
    static std::string Trim(const std::string &str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }
public:
    static std::string Serialize(const std::vector<std::string> &vals) {
        std::string res;
        for (size_t i = 0; i < vals.size(); ++i) {
            res += Trim(vals[i]) + (i == vals.size() - 1 ? "" : ",");
        }
        return res;
    }
    static std::vector<std::string> Deserialize(const std::string &data) {
        std::vector<std::string> res;
        std::stringstream ss(data);
        std::string item;
        while (std::getline(ss, item, ',')) {
            res.push_back(Trim(item));
        }
        return res;
    }
};