#include "disk_manager.h"
#include <iostream>

DiskManager::DiskManager(const std::string &db_name) : current_db_name(db_name) {
    std::string db_filename = db_name + ".db";
    std::string log_filename = db_name + ".log";

    db_io.open(db_filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!db_io.is_open()) {
        db_io.clear();
        db_io.open(db_filename, std::ios::out | std::ios::binary | std::ios::trunc);
        db_io.close();
        db_io.open(db_filename, std::ios::in | std::ios::out | std::ios::binary);
    }
    
    wal_io.open(log_filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!wal_io.is_open()) {
        wal_io.clear();
        wal_io.open(log_filename, std::ios::out | std::ios::binary | std::ios::trunc);
        wal_io.close();
        wal_io.open(log_filename, std::ios::in | std::ios::out | std::ios::binary);
    }

    db_io.seekg(0, std::ios::end);
    std::streampos file_size = db_io.tellg();
    if (file_size > 0) {
        num_pages = static_cast<page_id_t>(file_size / PAGE_SIZE);
    } else {
        num_pages = 0;
    }
}

DiskManager::~DiskManager() {
    if (db_io.is_open()) db_io.close();
    if (wal_io.is_open()) wal_io.close();
}

void DiskManager::WritePage(page_id_t pid, const char* page_data) {
    db_io.seekp(static_cast<std::streamoff>(pid) * PAGE_SIZE);
    db_io.write(page_data, PAGE_SIZE);
    db_io.flush();
}

void DiskManager::ReadPage(page_id_t pid, char* page_data) {
    db_io.seekg(static_cast<std::streamoff>(pid) * PAGE_SIZE);
    db_io.read(page_data, PAGE_SIZE);
}

page_id_t DiskManager::AllocatePage() {
    return num_pages++;
}

void DiskManager::WriteWAL(const std::string &log) {
    if (wal_io.is_open()) {
        wal_io.seekp(0, std::ios::end);
        wal_io.write(log.data(), log.size());
        wal_io.flush();
    }
}