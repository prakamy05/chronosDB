#pragma once
#include <string>
#include <vector>

struct ASTStatement {
    std::string type; 
    std::string table_name;
    std::vector<std::string> columns;
    std::vector<std::string> values;
    std::string where_col;
    std::string where_val;
};