#pragma once
#include <string>
#include <algorithm>
#include <cctype>

enum class TokenType { KEYWORD, IDENTIFIER, NUMBER, SYMBOL, END };

struct Token {
    TokenType type;
    std::string text;
};

class Lexer {
private:
    std::string src;
    size_t pos{0};
public:
    Lexer(std::string sql) : src(sql) {}
    
    Token NextToken() {
        while (pos < src.size() && std::isspace(src[pos])) pos++;
        if (pos >= src.size()) return {TokenType::END, ""};

        if (std::isalpha(src[pos])) {
            std::string ident;
            while (pos < src.size() && (std::isalnum(src[pos]))) {
                ident += src[pos++];
            }
            std::string upper = ident;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            if (upper == "SELECT" || upper == "INSERT" || upper == "INTO" || upper == "VALUES" || upper == "CREATE" || upper == "TABLE" || upper == "WHERE") {
                return {TokenType::KEYWORD, upper};
            }
            return {TokenType::IDENTIFIER, ident};
        }
        if (std::isdigit(src[pos])) {
            std::string num;
            while (pos < src.size() && std::isdigit(src[pos])) num += src[pos++];
            return {TokenType::NUMBER, num};
        }
        if (src[pos] == '=' || src[pos] == ',' || src[pos] == '(' || src[pos] == ')' || src[pos] == '*') {
            std::string sym(1, src[pos++]);
            return {TokenType::SYMBOL, sym};
        }
        return {TokenType::END, ""};
    }
};