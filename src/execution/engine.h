#pragma once
#include "../storage/disk_manager.h"
#include "../storage/buffer_pool_manager.h"
#include "../storage/recovery.h"
#include "../concurrency/lock_manager.h"
#include "../concurrency/transaction_manager.h"
#include "../compiler/parser.h"
#include "catalog.h"
#include "executors.h"
#include <map>
#include <memory>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

class DBEngine {
private:
    void PersistCatalogToDisk() {
        Page* cat_page = bpm.FetchPage(1);
        if (!cat_page) return;
        cat_page->Reset();
        cat_page->set_page_id(1);
        cat_page->set_page_type(PageType::DATA_PAGE);
        
        RID placeholder;
        cat_page->InsertRecord(catalog.SerializeCatalog(), placeholder);
        bpm.UnpinPage(1, true);
        bpm.FlushAllPages();
    }

    void LoadCatalogFromDisk() {
        if (dm.GetNumPages() == 0) {
            page_id_t p0, p1;
            Page* fp0 = bpm.NewPage(p0);
            if (fp0) bpm.UnpinPage(p0, true);
            Page* fp1 = bpm.NewPage(p1);
            if (fp1) bpm.UnpinPage(p1, true);
            bpm.FlushAllPages();
            return;
        }

        Page* cat_page = bpm.FetchPage(1);
        if (cat_page) {
            std::string raw_catalog;
            if (cat_page->GetRecord(0, raw_catalog)) {
                catalog.DeserializeCatalog(raw_catalog);
            }
            bpm.UnpinPage(1, false);
        }
    }

public:
    DiskManager dm;
    BufferPoolManager bpm;
    RecoveryManager rm; 
    LockManager lm;
    TransactionManager tm;
    Catalog catalog;

    DBEngine(std::string db_name) : dm(db_name), bpm(dm), rm(dm, bpm), tm(lm, dm) {
        rm.RunRecovery();
        LoadCatalogFromDisk(); 
    }

    ~DBEngine() {
        PersistCatalogToDisk();
        bpm.FlushAllPages();
    }

    std::string ExecuteStatement(std::string sql) {
        if(sql.empty()) return "";
        
        // Command preprocessing normalization checks for secondary index creation mappings
        std::stringstream command_check(sql);
        std::string first_tok, second_tok;
        command_check >> first_tok >> second_tok;
        std::transform(first_tok.begin(), first_tok.end(), first_tok.begin(), ::toupper);
        std::transform(second_tok.begin(), second_tok.end(), second_tok.begin(), ::toupper);

        if (first_tok == "CREATE" && second_tok == "INDEX") {
            std::string idx_name, on_tok, table_name, raw_col;
            command_check >> idx_name >> on_tok >> table_name >> raw_col;
            
            if (!catalog.Exists(table_name)) return "ERROR: Base table not found for index build.\n";
            if (catalog.IndexExists(idx_name)) return "ERROR: Index identifier already exists.\n";
            
            // Clean index target variables layout strings
            size_t p_open = raw_col.find('(');
            size_t p_close = raw_col.find(')');
            std::string target_col = raw_col.substr(p_open + 1, p_close - p_open - 1);
            
            TableMetadata &t_meta = catalog.GetTable(table_name);
            size_t col_idx = 0;
            bool found = false;
            for (size_t i = 0; i < t_meta.col_names.size(); ++i) {
                if (t_meta.col_names[i] == target_col) {
                    col_idx = i;
                    found = true;
                    break;
                }
            }
            if (!found) return "ERROR: Column target match failed inside structural metadata schema.\n";

            // Spawn index backing allocation node frame page layout
            page_id_t idx_root_pid;
            Page* idx_page = bpm.NewPage(idx_root_pid);
            if (!idx_page) return "ERROR: Secondary catalog structural allocations failed.\n";
            idx_page->Reset();
            idx_page->set_page_id(idx_root_pid);
            idx_page->set_page_type(PageType::INDEX_PAGE);

            // Populate index leaves mapping sequence from existing linear data pool using an explicit transaction
            Transaction* txn = tm.Begin();
            auto table_scan = std::make_unique<SeqScanExecutor>(bpm, t_meta, txn, lm);
            table_scan->Init();
            std::vector<std::string> tuple;
            RID data_rid;
            while (table_scan->Next(tuple, data_rid)) {
                std::vector<std::string> index_payload = { tuple[col_idx], std::to_string(data_rid.page_id), std::to_string(data_rid.slot_num) };
                std::string leaf_raw = TupleSerializer::Serialize(index_payload);
                RID dummy_rid;
                idx_page->InsertRecord(leaf_raw, dummy_rid);
            }
            tm.Commit(txn);
            
            bpm.UnpinPage(idx_root_pid, true);
            catalog.CreateIndex(idx_name, table_name, target_col, col_idx, idx_root_pid);
            PersistCatalogToDisk();
            return "SUCCESS: Secondary index '" + idx_name + "' synchronized efficiently on col [" + target_col + "].\n";
        }

        ASTStatement ast = Parser::Parse(sql);
        
        if (ast.type == "CREATE") {
            if (catalog.Exists(ast.table_name)) return "ERROR: Table already exists.\n";
            
            std::vector<std::string> c_names;
            std::vector<DataType> c_types;
            for(const auto& col : ast.columns) {
                c_names.push_back(col.name);
                c_types.push_back(col.type);
            }
            
            catalog.CreateTable(ast.table_name, c_names, c_types);
            PersistCatalogToDisk(); 
            return "SUCCESS: Table " + ast.table_name + " created with explicit typed schema layout.\n";
        }

        if (ast.type == "DROP") {
            if (!catalog.Exists(ast.table_name)) return "ERROR: Table not found.\n";
            catalog.DropTable(ast.table_name);
            PersistCatalogToDisk();
            return "SUCCESS: Table " + ast.table_name + " dropped from storage.\n";
        }

        if (ast.type == "DELETE") {
            if (!catalog.Exists(ast.table_name)) return "ERROR: Table not found.\n";
            TableMetadata &meta = catalog.GetTable(ast.table_name);
            
            size_t target_where_idx = 0;
            bool target_where_found = false;
            if (!ast.where_col.empty()) {
                for (size_t i = 0; i < meta.col_names.size(); ++i) {
                    if (meta.col_names[i] == ast.where_col) {
                        target_where_idx = i;
                        target_where_found = true;
                        break;
                    }
                }
                if (!target_where_found) return "ERROR: WHERE column target field mismatch inside active schema validation loop.\n";
            }

            Transaction* txn = tm.Begin();
            auto scan = std::make_unique<SeqScanExecutor>(bpm, meta, txn, lm);
            scan->Init();
            
            std::vector<std::string> tuple;
            RID rid;
            int deleted_count = 0;
            while (scan->Next(tuple, rid)) {
                if (ast.where_val.empty() || (target_where_idx < tuple.size() && tuple[target_where_idx] == ast.where_val)) {
                    lm.Acquire(txn->GetTxnId(), rid, LockMode::EXCLUSIVE);
                    txn->AddLock(rid);
                    
                    scan->DeleteCurrentRecord(txn->GetTxnId());
                    deleted_count++;

                    // Structural cascade cleanup sweep: Purge matching payload entries out of secondary index frames
                    for (auto const& [idx_name, idx_meta] : catalog.GetAllIndexes()) {
                        if (idx_meta.table_name == ast.table_name) {
                            Page* idx_p = bpm.FetchPage(idx_meta.root_page_id);
                            if (idx_p) {
                                std::string raw_idx_node;
                                uint32_t slot_idx = 0; 
                                while (idx_p->GetRecord(slot_idx, raw_idx_node)) {
                                    std::vector<std::string> payload = TupleSerializer::Deserialize(raw_idx_node);
                                    if (payload.size() >= 3) {
                                        page_id_t p_id = std::stoi(payload[1]);
                                        uint32_t s_num = std::stoul(payload[2]); 
                                        if (p_id == rid.page_id && s_num == rid.slot_num) {
                                            idx_p->DeleteRecord(slot_idx, txn->GetTxnId()); 
                                            break;
                                        }
                                    }
                                    slot_idx++;
                                }
                                bpm.UnpinPage(idx_meta.root_page_id, true);
                            }
                        }
                    }
                }
            }
            tm.Commit(txn);
            PersistCatalogToDisk();
            return "SUCCESS: Deleted " + std::to_string(deleted_count) + " matching rows from table.\n";
        }

        if (ast.type == "INSERT") {
            if (!catalog.Exists(ast.table_name)) return "ERROR: Table not found.\n";
            TableMetadata &meta = catalog.GetTable(ast.table_name);
            
            if (ast.values.size() != meta.col_names.size()) {
                return "ERROR: Column count mismatch. Expected " + std::to_string(meta.col_names.size()) + " fields.\n";
            }

            for (size_t i = 0; i < meta.col_types.size(); ++i) {
                if (meta.col_types[i] == DataType::INT) {
                    for (char const &ch : ast.values[i]) {
                        if (std::isdigit(ch) == 0 && ch != '-') {
                            return "ERROR: Type Mutation Fault. Column '" + meta.col_names[i] + "' expects an INTEGER.\n";
                        }
                    }
                }
            }

            Transaction* txn = tm.Begin();
            RID rid;
            std::string raw = TupleSerializer::Serialize(ast.values);
            page_id_t target_pid = meta.root_pages.empty() ? INVALID_PAGE_ID : meta.root_pages.back();
            Page* page = nullptr;

            if (target_pid == INVALID_PAGE_ID || !(page = bpm.FetchPage(target_pid))->InsertRecord(raw, rid)) {
                if (page) bpm.UnpinPage(target_pid, false);
                page = bpm.NewPage(target_pid);
                meta.root_pages.push_back(target_pid);
                page->InsertRecord(raw, rid);
            }

            lm.Acquire(txn->GetTxnId(), rid, LockMode::EXCLUSIVE);
            txn->AddLock(rid);
            bpm.UnpinPage(target_pid, true);
            tm.Commit(txn);

            // Auto-update any secondary indexes bound to this insertion layout context
            for (auto const& [idx_name, idx_meta] : catalog.GetAllIndexes()) {
                if (idx_meta.table_name == ast.table_name) {
                    Page* idx_p = bpm.FetchPage(idx_meta.root_page_id);
                    if (idx_p) {
                        std::vector<std::string> index_payload = { ast.values[idx_meta.col_idx], std::to_string(rid.page_id), std::to_string(rid.slot_num) };
                        std::string leaf_raw = TupleSerializer::Serialize(index_payload);
                        RID dummy_rid;
                        idx_p->InsertRecord(leaf_raw, dummy_rid);
                        bpm.UnpinPage(idx_meta.root_page_id, true);
                    }
                }
            }

            PersistCatalogToDisk(); 
            return "SUCCESS: Row inserted at Page " + std::to_string(rid.page_id) + " Slot " + std::to_string(rid.slot_num) + "\n";
        }

        if (ast.type == "SELECT") {
            bool is_join = !ast.where_col.empty() && ast.where_col.find('.') != std::string::npos;
            std::unique_ptr<AbstractExecutor> root_plan;
            std::vector<std::string> projection_headers;

            Transaction* txn = tm.Begin();

            if (!is_join) {
                if (!catalog.Exists(ast.table_name)) {
                    tm.Commit(txn);
                    return "ERROR: Table not found.\n";
                }
                TableMetadata &meta = catalog.GetTable(ast.table_name);
                
                // Optimizer scan routing check: intercept index-accelerated configurations
                bool index_scanned = false;
                if (!ast.where_val.empty() && !ast.where_col.empty()) {
                    for (auto const& [idx_name, idx_meta] : catalog.GetAllIndexes()) {
                        if (idx_meta.table_name == ast.table_name && idx_meta.column_name == ast.where_col) { 
                            root_plan = std::make_unique<IndexScanExecutor>(bpm, idx_meta, ast.where_val, txn, lm);
                            index_scanned = true;
                            break;
                        }
                    }
                }

                if (!index_scanned) {
                    auto seq_scan = std::make_unique<SeqScanExecutor>(bpm, meta, txn, lm);
                    if (!ast.where_val.empty()) {
                        size_t col_idx = 0;
                        for(size_t i = 0; i < meta.col_names.size(); ++i) {
                            if(meta.col_names[i] == ast.where_col) { col_idx = i; break; }
                        }
                        root_plan = std::make_unique<FilterExecutor>(std::move(seq_scan), ast.where_val, col_idx);
                    } else {
                        root_plan = std::move(seq_scan);
                    }
                }
                projection_headers = meta.col_names;

            } else {
                std::string left_t = ast.table_name;
                std::string right_t = ast.where_val; 
                
                if (!catalog.Exists(left_t) || !catalog.Exists(right_t)) {
                    tm.Commit(txn);
                    return "ERROR: Table mismatch in join parameters.\n";
                }
                
                TableMetadata &l_meta = catalog.GetTable(left_t);
                TableMetadata &r_meta = catalog.GetTable(right_t);
                
                auto left_scan = std::make_unique<SeqScanExecutor>(bpm, l_meta, txn, lm);
                auto right_scan = std::make_unique<SeqScanExecutor>(bpm, r_meta, txn, lm);
                
                root_plan = std::make_unique<NestedLoopJoinExecutor>(
                    std::move(left_scan), std::move(right_scan), 0, 0);

                for (const auto& cn : l_meta.col_names) projection_headers.push_back(left_t + "." + cn);
                for (const auto& cn : r_meta.col_names) projection_headers.push_back(right_t + "." + cn);
            }

            // Optional Pipeline Layer: Aggregation and Group By checks
            if (!ast.values.empty() && (ast.values[0] == "COUNT" || ast.values[0] == "SUM" || ast.values[0] == "AVG")) {
                auto type = AggregationType::COUNT;
                if (ast.values[0] == "SUM") type = AggregationType::SUM;
                if (ast.values[0] == "AVG") type = AggregationType::AVG;
                
                root_plan = std::make_unique<HashAggregationExecutor>(std::move(root_plan), 0, 0, type);
                projection_headers = { "GroupKey", ast.values[0] + "_Result" };
            }

            // Optional Pipeline Layer: In-Memory Sorting checks
            if (!ast.where_col.empty() && ast.where_col == "ORDER_BY") {
                root_plan = std::make_unique<SortExecutor>(std::move(root_plan), 0, true);
            }

            // Pipeline Output Core: Final Expression Projection Engine layer processing
            std::vector<AbstractExpression> exprs;
            for (size_t i = 0; i < projection_headers.size(); ++i) {
                exprs.push_back({ ExpressionType::COLUMN_REF, i, "", nullptr, nullptr });
            }

            auto projection_plan = std::make_unique<ProjectionExecutor>(std::move(root_plan), std::move(exprs));

            projection_plan->Init();
            std::vector<std::string> final_tuple;
            RID final_rid;
            std::string output = "--- Query Engine Results Vector ---\n";
            while (projection_plan->Next(final_tuple, final_rid)) {
                output += "[Record Tuple]: ";
                for (size_t i = 0; i < final_tuple.size(); ++i) {
                    output += projection_headers[i] + "=" + final_tuple[i] + (i + 1 < final_tuple.size() ? ", " : "");
                }
                output += "\n";
            }
            tm.Commit(txn); 
            return output;
        }
        return "ERROR: Parsing step unhandled or invalid statement structure.\n";
    }
};

class DatabaseClusterManager {
private:
    std::map<std::string, std::unique_ptr<DBEngine>> cluster_pool;
    std::string active_db;

    void SaveClusterMeta() {
        std::ofstream out("cluster_manifest.meta");
        for (auto const& [name, eng] : cluster_pool) {
            out << name << "\n";
        }
    }

    void LoadClusterMeta() {
        std::ifstream input_file("cluster_manifest.meta");
        if (!input_file.is_open()) {
            cluster_pool["mydb"] = std::make_unique<DBEngine>("mydb");
            active_db = "mydb";
            SaveClusterMeta();
            return;
        }
        std::string db_name;
        while (std::getline(input_file, db_name)) {
            if (!db_name.empty()) {
                cluster_pool[db_name] = std::make_unique<DBEngine>(db_name);
                if (active_db.empty()) active_db = db_name;
            }
        }
    }

public:
    DatabaseClusterManager() {
        LoadClusterMeta();
    }

    std::string GetActiveDBName() { return active_db; }
    const std::map<std::string, std::unique_ptr<DBEngine>>& GetClusterPool() const { return cluster_pool; }

    std::string ExecuteClusterWideQuery(std::string raw_sql) {
        while(!raw_sql.empty() && (raw_sql.back() == ' ' || raw_sql.back() == '\n' || raw_sql.back() == '\r')) raw_sql.pop_back();
        if(raw_sql.empty()) return "";

        std::stringstream ss(raw_sql);
        std::string command;
        ss >> command;
        std::transform(command.begin(), command.end(), command.begin(), ::toupper);

        if (command == "CREATE") {
            std::string type, target_name;
            ss >> type >> target_name;
            std::transform(type.begin(), type.end(), type.begin(), ::toupper);
            
            if (type == "DATABASE") {
                if (cluster_pool.count(target_name)) return "ERROR: Database already exists.\n";
                cluster_pool[target_name] = std::make_unique<DBEngine>(target_name);
                SaveClusterMeta();
                return "SUCCESS: Database cluster '" + target_name + "' spawned safely.\n";
            }
        }
        if (command == "USE") {
            std::string target_db;
            ss >> target_db;
            if (!cluster_pool.count(target_db)) return "ERROR: Database instance '" + target_db + "' not found.\n";
            active_db = target_db;
            return "SUCCESS: Switched active storage context to database '" + active_db + "'.\n";
        }

        return cluster_pool[active_db]->ExecuteStatement(raw_sql);
    }

    std::string ProcessDashboardRoute(std::string action, std::string payload) {
        std::string base_html = 
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
            "<!DOCTYPE html><html><head><title>MiniDB Server Control Studio</title>"
            "<style>"
            "body{font-family:'Segoe UI',sans-serif;margin:0;background:#f8f9fa;display:flex;height:100vh;}"
            ".sidebar{width:300px;background:#2c3e50;color:white;padding:20px;box-sizing:border-box;display:flex;flex-direction:column;border-right:3px solid #34495e;}"
            ".main-content{flex:1;padding:30px;box-sizing:border-box;overflow-y:auto;}"
            "h2{margin-top:0;font-size:16px;text-transform:uppercase;letter-spacing:1px;color:#bdc3c7;border-bottom:1px solid #34495e;padding-bottom:8px;margin-bottom:15px;}"
            ".db-block{background:#34495e;padding:12px;border-radius:6px;margin-bottom:15px;border-left:5px solid #7f8c8d;}"
            ".db-block.active{border-left-color:#2ecc71;background:#1e272e;}"
            ".db-title{font-weight:bold;font-size:15px;color:#fff;display:block;margin-bottom:5px;}"
            ".table-link{color:#ecf0f1;text-decoration:none;display:block;padding:4px 0;font-size:13px;margin-left:10px;font-family:monospace;}"
            ".sub-link{font-size:11px;color:#3498db;margin-left:8px;text-decoration:none;}"
            ".sub-link:hover{text-decoration:underline;}"
            ".card{background:white;padding:25px;border-radius:8px;box-shadow:0 4px 12px rgba(0,0,0,0.05);margin-bottom:25px;}"
            "textarea{width:100%;height:100px;padding:12px;border:1px solid #dcdde1;border-radius:6px;font-family:monospace;resize:none;box-sizing:border-box;}"
            "button{background:#2ecc71;color:white;border:none;padding:12px 25px;border-radius:6px;cursor:pointer;font-weight:bold;margin-top:10px;}"
            "button:hover{background:#27ae60;}"
            ".output-console{background:#1e272e;color:#2ecc71;padding:15px;border-radius:6px;font-family:monospace;white-space:pre-wrap;margin-top:15px;}"
            "table{width:100%;border-collapse:collapse;margin-top:15px;}th,td{padding:12px;text-align:left;border-bottom:1px solid #dcdde1;}th{background:#f1f2f6;color:#2c3e50;}"
            ".badge{background:#e74c3c;color:white;padding:2px 6px;border-radius:4px;font-size:11px;font-weight:bold;margin-left:5px;}"
            "</style></head><body>"
            "<div class='sidebar'><h2>🗄️ Database Clusters</h2>";

        for (auto const& [db_name, eng_ptr] : cluster_pool) {
            bool is_current = (db_name == active_db);
            base_html += "<div class='db-block " + std::string(is_current ? "active" : "") + "'>";
            base_html += "<span class='db-title'>🛢️ " + db_name + (is_current ? " (Active)" : "") + "</span>";
            
            auto tables = eng_ptr->catalog.GetAllTables();
            if(tables.empty()) {
                base_html += "<p style='color:#bdc3c7;font-size:11px;margin:5px 0 0 10px;font-style:italic;'>Empty cluster</p>";
            } else {
                for(auto const& [t_name, t_meta] : tables) {
                    base_html += "<div class='table-link'>📋 " + t_name;
                    base_html += " <br><a class='sub-link' href='/table?db=" + db_name + "&name=" + t_name + "'>🔍 View</a>";
                    base_html += "<a class='sub-link' href='/schema?db=" + db_name + "&name=" + t_name + "'>🛠️ Schema</a>";
                    base_html += "</div>";
                }
            }
            base_html += "</div>";
        }

        base_html += 
            "</div>"
            "<div class='main-content'>"
            "  <div class='card'>"
            "    <h2 style='color:#2c3e50;'>🚀 Cluster Workstation Engine [Active Context: <span style='color:#2ecc71;'>" + active_db + "</span>]</h2>"
            "    <p style='color:#7f8c8d;font-size:13px;'>Run queries on the active database, or use commands like <code>CREATE DATABASE name</code> or <code>USE name</code>.</p>"
            "    <form method='POST' action='/run'>"
            "      <textarea name='sql' placeholder='e.g., SELECT * FROM users'></textarea>"
            "      <button type='submit'>Execute Query Sequence</button>"
            "    </form>";

        if (action == "POST_RUN") {
            base_html += "<h3>Cluster Result Payload:</h3><div class='output-console'>" + payload + "</div>";
        }
        base_html += "</div>";

        if (action == "TABLE_DATA" || action == "TABLE_SCHEMA") {
            size_t db_token_pos = payload.find("|");
            if (db_token_pos != std::string::npos) {
                std::string target_db = payload.substr(0, db_token_pos);
                std::string target_table = payload.substr(db_token_pos + 1);

                if (cluster_pool.count(target_db)) {
                    DBEngine* target_eng = cluster_pool[target_db].get();
                    if (action == "TABLE_DATA") {
                        base_html += "<div class='card'><h2>Data Grid: <code>" + target_db + "." + target_table + "</code></h2>";
                        if (!target_eng->catalog.Exists(target_table)) {
                            base_html += "<p>Table not found.</p>";
                        } else {
                            TableMetadata &meta = target_eng->catalog.GetTable(target_table);
                            base_html += "<table><tr>";
                            for (const auto& col : meta.col_names) base_html += "<th>" + col + "</th>";
                            base_html += "</tr>";

                            Transaction* dashboard_txn = target_eng->tm.Begin();
                            auto scan = std::make_unique<SeqScanExecutor>(target_eng->bpm, meta, dashboard_txn, target_eng->lm);
                            scan->Init();
                            std::vector<std::string> row_vals;
                            RID rid;
                            while (scan->Next(row_vals, rid)) {
                                base_html += "<tr>";
                                for (const auto& cell : row_vals) base_html += "<td>" + cell + "</td>";
                                base_html += "</tr>";
                            }
                            target_eng->tm.Commit(dashboard_txn);
                            base_html += "</table>";
                        }
                        base_html += "</div>";
                    } else {
                        base_html += "<div class='card'><h2>Schema Matrix: <code>" + target_db + "." + target_table + "</code></h2>";
                        if (!target_eng->catalog.Exists(target_table)) {
                            base_html += "<p>Table structural log unavailable.</p>";
                        } else {
                            TableMetadata &meta = target_eng->catalog.GetTable(target_table);
                            base_html += "<table><tr><th>Column Target</th><th>Type</th></tr>";
                            for (size_t i = 0; i < meta.col_names.size(); ++i) {
                                std::string type_label = (meta.col_types[i] == DataType::INT) ? "INT / INTEGER" : "VARCHAR / STRING";
                                base_html += "<tr><td><code>" + meta.col_names[i] + "</code></td><td>" + type_label + "</td></tr>";
                            }
                            base_html += "</table>";
                        }
                        base_html += "</div>";
                    }
                }
            }
        }

        base_html += "</div></body></html>";
        return base_html;
    }
};