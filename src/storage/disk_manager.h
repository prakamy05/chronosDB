#pragma once
#include "../common/types.h"
#include <fstream>
#include <string>

class DiskManager {
private:
    std::fstream db_io;
    std::fstream wal_io;
    page_id_t num_pages{0};
    std::string current_db_name;
public:
    DiskManager(const std::string &db_name); // Dynamic constructor parameter added
    ~DiskManager();
    void WritePage(page_id_t pid, const char* page_data);
    void ReadPage(page_id_t pid, char* page_data);
    page_id_t AllocatePage();
    void WriteWAL(const std::string &log);
    page_id_t GetNumPages() const { return num_pages; }
    std::string GetDBName() const { return current_db_name; }
};