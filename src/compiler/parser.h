#pragma once
#include "../common/types.h"
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iostream>

struct ColumnDefinition {
    std::string name;
    DataType type;
    bool is_primary_key{false};
    bool is_foreign_key{false};
    std::string references_table;
    std::string references_column;
};

struct ASTStatement {
    std::string type;                      // "CREATE", "INSERT", "SELECT", "DELETE", "DROP"
    std::string table_name;
    std::vector<ColumnDefinition> columns; // Used for CREATE
    std::vector<std::string> values;       // Used for INSERT
    std::string where_col;                 // Used for FILTER/DELETE WHERE / Left Join Column
    std::string where_val;                 // Used for FILTER/DELETE WHERE / Right Table Name / Right Join Column
    
    std::vector<std::string> projection_fields; 
    std::string aggregate_function;        // "COUNT", "SUM", "AVG", or empty
    std::string aggregate_target;          // Target column for aggregation function
    std::string group_by_col;              // Target column for GROUP BY clause
    std::string order_by_col;              // Target column for ORDER BY clause
    bool is_descending{false};             // Tracking for sorting direction
};

class Parser {
private:
    static std::string CleanString(std::string str) {
        while (!str.empty() && (str.front() == ' ' || str.front() == '\t' || str.front() == '\r' || str.front() == '\n')) {
            str.erase(str.begin());
        }
        while (!str.empty() && (str.back() == ' ' || str.back() == '\t' || str.back() == '\r' || str.back() == '\n' || str.back() == ';')) {
            str.pop_back();
        }
        return str;
    }

    static std::string ToUpper(std::string str) {
        std::transform(str.begin(), str.end(), str.begin(), ::toupper);
        return str;
    }

public:
    static ASTStatement Parse(std::string sql) {
        ASTStatement ast;
        sql = CleanString(sql);
        if (sql.empty()) return ast;

        std::stringstream ss(sql);
        std::string command;
        ss >> command;
        command = ToUpper(command);

        if (command == "CREATE") {
            std::string object_type;
            ss >> object_type;
            object_type = ToUpper(object_type);
            
            if (object_type == "TABLE") {
                ast.type = "CREATE";
                std::string table_and_cols;
                std::getline(ss, table_and_cols);
                
                size_t paren_open = table_and_cols.find('(');
                if (paren_open != std::string::npos) {
                    ast.table_name = CleanString(table_and_cols.substr(0, paren_open));
                    
                    size_t paren_close = table_and_cols.rfind(')');
                    std::string cols_block = table_and_cols.substr(paren_open + 1, paren_close - paren_open - 1);
                    std::stringstream col_ss(cols_block);
                    std::string single_col;
                    
                    while (std::getline(col_ss, single_col, ',')) {
                        single_col = CleanString(single_col);
                        if (single_col.empty()) continue;

                        std::stringstream token_ss(single_col);
                        std::string c_name, c_type;
                        token_ss >> c_name >> c_type;
                        
                        if (ToUpper(c_name) == "FOREIGN" || ToUpper(c_name) == "PRIMARY") continue;

                        c_type = ToUpper(c_type);
                        DataType dt = (c_type == "INT" || c_type == "INTEGER") ? DataType::INT : DataType::VARCHAR;
                        ColumnDefinition col_def{c_name, dt, false, false, "", ""};

                        std::string modifier;
                        while (token_ss >> modifier) {
                            modifier = ToUpper(modifier);
                            if (modifier == "PRIMARY") {
                                std::string next_tok;
                                if (token_ss >> next_tok) col_def.is_primary_key = true;
                            }
                            else if (modifier == "REFERENCES") {
                                std::string ref_target;
                                token_ss >> ref_target;
                                size_t target_paren = ref_target.find('(');
                                if (target_paren != std::string::npos) {
                                    col_def.is_foreign_key = true;
                                    col_def.references_table = ref_target.substr(0, target_paren);
                                    col_def.references_column = ref_target.substr(target_paren + 1, ref_target.find(')') - target_paren - 1);
                                }
                            }
                        }
                        ast.columns.push_back(col_def);
                    }
                } else {
                    std::stringstream space_ss(table_and_cols);
                    space_ss >> ast.table_name;
                    ast.table_name = CleanString(ast.table_name);
                    
                    std::string c_name, c_type;
                    while (space_ss >> c_name >> c_type) {
                        DataType dt = (ToUpper(c_type) == "INT" || ToUpper(c_type) == "INTEGER") ? DataType::INT : DataType::VARCHAR;
                        ast.columns.push_back({c_name, dt, false, false, "", ""});
                    }
                }
            }
        } 
        else if (command == "DROP") {
            std::string object_type;
            ss >> object_type;
            if (ToUpper(object_type) == "TABLE") {
                ast.type = "DROP";
                ss >> ast.table_name;
                ast.table_name = CleanString(ast.table_name);
            }
        }
        else if (command == "DELETE") {
            std::string from_token;
            ss >> from_token >> ast.table_name; 
            ast.table_name = CleanString(ast.table_name);
            ast.type = "DELETE";
            
            std::string remainder;
            std::getline(ss, remainder);
            remainder = CleanString(remainder);
            std::string upper_rem = ToUpper(remainder);
            
            size_t where_pos = upper_rem.find("WHERE");
            if (where_pos != std::string::npos) {
                std::string condition = CleanString(remainder.substr(where_pos + 5));
                size_t eq_pos = condition.find('=');
                if (eq_pos != std::string::npos) {
                    ast.where_col = CleanString(condition.substr(0, eq_pos));
                    ast.where_val = CleanString(condition.substr(eq_pos + 1));
                }
            }
        }
        else if (command == "INSERT") {
            std::string into_tok;
            ss >> into_tok >> ast.table_name;
            ast.table_name = CleanString(ast.table_name);
            ast.type = "INSERT";
            
            std::string values_block;
            std::getline(ss, values_block);
            size_t val_open = ToUpper(values_block).find("VALUES");
            
            if (val_open != std::string::npos) {
                size_t p_open = values_block.find('(', val_open);
                size_t p_close = values_block.rfind(')');
                if (p_open != std::string::npos && p_close != std::string::npos) {
                    std::string raw_vals = values_block.substr(p_open + 1, p_close - p_open - 1);
                    std::stringstream val_ss(raw_vals);
                    std::string v;
                    while (std::getline(val_ss, v, ',')) {
                        ast.values.push_back(CleanString(v));
                    }
                }
            } else {
                std::stringstream val_ss(values_block);
                std::string v;
                while (val_ss >> v) {
                    ast.values.push_back(CleanString(v));
                }
            }
        } 
        else if (command == "SELECT") {
            ast.type = "SELECT";
            std::string remaining;
            std::getline(ss, remaining);
            remaining = CleanString(remaining);
            
            size_t from_pos = ToUpper(remaining).find("FROM");
            if (from_pos == std::string::npos) return ast;
            
            std::string proj_block = CleanString(remaining.substr(0, from_pos));
            std::string clause_block = CleanString(remaining.substr(from_pos + 4));
            
            // Parse Projections / Aggregates
            std::stringstream proj_ss(proj_block);
            std::string field;
            while (std::getline(proj_ss, field, ',')) {
                field = CleanString(field);
                std::string field_upper = ToUpper(field);
                
                if (field_upper.find("COUNT(") == 0 || field_upper.find("SUM(") == 0 || field_upper.find("AVG(") == 0) {
                    size_t op = field.find('(');
                    size_t cl = field.find(')');
                    ast.aggregate_function = field_upper.substr(0, op);
                    ast.aggregate_target = CleanString(field.substr(op + 1, cl - op - 1));
                    ast.projection_fields.push_back(ast.aggregate_target); 
                } else {
                    ast.projection_fields.push_back(field);
                }
            }
            
            // Isolate table boundaries accurately using generic keyword positions
            std::string upper_clauses = ToUpper(clause_block);
            size_t join_pos = upper_clauses.find("JOIN");
            size_t where_pos = upper_clauses.find("WHERE");
            size_t group_pos = upper_clauses.find("GROUP");
            size_t order_pos = upper_clauses.find("ORDER");
            
            size_t first_keyword = clause_block.size();
            if (join_pos != std::string::npos)  first_keyword = std::min(first_keyword, join_pos);
            if (where_pos != std::string::npos) first_keyword = std::min(first_keyword, where_pos);
            if (group_pos != std::string::npos) first_keyword = std::min(first_keyword, group_pos);
            if (order_pos != std::string::npos) first_keyword = std::min(first_keyword, order_pos);
            
            ast.table_name = CleanString(clause_block.substr(0, first_keyword));
            
            // Extract JOIN details
            if (join_pos != std::string::npos) {
                size_t next_keyword = clause_block.size();
                if (where_pos != std::string::npos) next_keyword = std::min(next_keyword, where_pos);
                if (group_pos != std::string::npos) next_keyword = std::min(next_keyword, group_pos);
                if (order_pos != std::string::npos) next_keyword = std::min(next_keyword, order_pos);
                
                std::string join_segment = clause_block.substr(join_pos + 4, next_keyword - (join_pos + 4));
                std::stringstream join_ss(join_segment);
                std::string right_table, on_tok, condition;
                join_ss >> right_table >> on_tok;
                std::getline(join_ss, condition);
                
                size_t eq_pos = condition.find('=');
                if (eq_pos != std::string::npos) {
                    ast.where_col = CleanString(condition.substr(0, eq_pos));   
                    ast.where_val = CleanString(condition.substr(eq_pos + 1)); 
                }
            }
            
            // Extract WHERE Filter details
            if (where_pos != std::string::npos) {
                size_t next_keyword = clause_block.size();
                if (group_pos != std::string::npos) next_keyword = std::min(next_keyword, group_pos);
                if (order_pos != std::string::npos) next_keyword = std::min(next_keyword, order_pos);
                
                std::string filter_cond = CleanString(clause_block.substr(where_pos + 5, next_keyword - (where_pos + 5)));
                size_t eq_pos = filter_cond.find('=');
                if (eq_pos != std::string::npos) {
                    ast.where_col = CleanString(filter_cond.substr(0, eq_pos));
                    if (join_pos == std::string::npos) {
                        ast.where_val = CleanString(filter_cond.substr(eq_pos + 1));
                    }
                }
            }
            
            // Extract GROUP BY details
            if (group_pos != std::string::npos) {
                size_t next_keyword = clause_block.size();
                if (order_pos != std::string::npos) next_keyword = std::min(next_keyword, order_pos);
                
                std::string group_segment = clause_block.substr(group_pos + 5, next_keyword - (group_pos + 5));
                size_t by_pos = ToUpper(group_segment).find("BY");
                if (by_pos != std::string::npos) {
                    ast.group_by_col = CleanString(group_segment.substr(by_pos + 2));
                }
            }
            
            // Extract ORDER BY details
            if (order_pos != std::string::npos) {
                std::string order_segment = clause_block.substr(order_pos + 5);
                size_t by_pos = ToUpper(order_segment).find("BY");
                if (by_pos != std::string::npos) {
                    std::stringstream order_ss(order_segment.substr(by_pos + 2));
                    std::string col, dir;
                    if (order_ss >> col) {
                        ast.order_by_col = CleanString(col);
                        if (order_ss >> dir) {
                            if (ToUpper(dir) == "DESC") ast.is_descending = true;
                        }
                    }
                }
            }
        }
        return ast;
    }
};