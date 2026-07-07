# ChronosDB

> **A transactional, ACID-compliant relational database management system (RDBMS) built from scratch in C++17.**
>
> ChronosDB implements a modular database engine architecture featuring **Slotted-Page Storage**, **LRU Buffer Pool Management**, **Multi-Version Concurrency Control (MVCC)**, and a **Volcano (Iterator Model) Query Execution Engine**. The system exposes three interfaces: an **interactive CLI**, a **multi-threaded TCP server**, and an **HTTP monitoring dashboard**.

---

## Table of Contents

- Project Overview
- Installation
- Running ChronosDB
- Ways to Use ChronosDB
- Supported SQL Commands
- Features
- Architecture Overview
- Project Structure
- Architecture Details
- Technologies Used
- Current Project Scope
- Future Scope

---

# Project Overview

ChronosDB is an educational relational database engine designed to explore the internals of modern database systems. It combines SQL compilation, query execution, concurrency control, storage management, networking, and monitoring into a layered architecture.

Core subsystems include:

- SQL compilation
- Volcano query execution
- MVCC + Strict Two-Phase Locking (SS2PL)
- Slotted-page storage
- LRU buffer pool
- Write-Ahead Logging (WAL)
- Multi-database virtualization
- TCP networking
- HTTP monitoring

---

# Installation

## Requirements

- **g++** (or another C++17-compatible compiler)
- **GNU Make**
- **POSIX Threads (`pthread`)**

The supplied Makefile is configured for **g++** with **C++17** and **-pthread**.

## Build

```bash
make
```

## Run

```bash
./chronosdb
```

## Clean

```bash
make clean
```

---

# Running ChronosDB

Starting the executable launches:

- Interactive CLI
- TCP Server (Port **5433**)
- HTTP Dashboard (Port **8080**)

---

# Ways to Use ChronosDB

### Interactive CLI

Run SQL commands directly from the terminal.

### TCP Server

Connect remotely on **5433** and send SQL statements over TCP.

### HTTP Dashboard

Open:

```text
http://localhost:8080
```

to inspect telemetry, cache statistics and execution metrics.

---

# Supported SQL Commands

## Database

```sql
CREATE DATABASE school;
USE school;
```

## Tables

```sql
CREATE TABLE students(
    id INT,
    name VARCHAR
);

DROP TABLE students;
```

Supported types:

- INT
- VARCHAR

## Data Manipulation

```sql
INSERT INTO students VALUES (1, Alice);

DELETE FROM students
WHERE id=1;
```

## Queries

Supports:

- SELECT
- WHERE
- JOIN
- GROUP BY
- ORDER BY

Automatically chooses sequential or index scans depending on index availability.

---

# Features

## Storage

- Slotted-page storage
- Variable-length tuples
- 4096-byte pages
- Binary serialization

## Query Engine

- SQL Lexer
- SQL Parser
- AST generation
- Volcano iterator execution
- Sequential scans
- Index scans
- Projection
- Filtering
- Sorting
- Hash aggregation
- Nested-loop joins

## Transactions

- MVCC
- Snapshot Isolation
- Strict 2PL
- WAL

## Buffer Pool

- 10-frame LRU cache
- Dirty page flushing
- Page pinning

## Networking

- CLI
- TCP Server
- HTTP Dashboard

---

# Architecture Overview

```text
Client
   │
   ├── CLI
   ├── TCP
   └── HTTP Dashboard
          │
          ▼
 DatabaseClusterManager
          │
       DBEngine
          │
  Lexer → Parser → AST
          │
          ▼
 Volcano Execution Engine
          │
          ▼
 MVCC + Lock Manager
          │
          ▼
 Buffer Pool (LRU)
          │
          ▼
 Disk Manager + WAL
```

### Volcano Execution Pipeline

```text
SQL
 │
 ▼
Lexer
 │
 ▼
Parser
 │
 ▼
AST
 │
 ▼
Executor::Init()
 │
 ▼
Next() → Next() → Next() → EOF
```

Each executor behaves as an iterator that produces one tuple at a time, enabling composable execution pipelines.

---

# Project Structure

```text
ChronosDB
├── Makefile
├── main.cpp
└── src/
    ├── common/
    ├── compiler/
    ├── concurrency/
    ├── execution/
    ├── network/
    └── storage/
```

---

# Architecture Details

- **Network Layer** — CLI, TCP server and dashboard.
- **Virtualization Layer** — DatabaseClusterManager and DBEngine manage catalogs and active databases.
- **Compiler Layer** — SQL tokenization, parsing and AST generation.
- **Execution Layer** — Volcano iterator model with scan, filter, projection, join, sort and aggregation executors.
- **Concurrency Layer** — MVCC snapshots, lock management and transaction lifecycle.
- **Memory Layer** — LRU buffer pool with dirty-page flushing and page pinning.
- **Storage Layer** — Slotted pages, binary persistence and WAL infrastructure.

---

# Technologies Used

## Language

- C++17

## Database Concepts

- Relational Database Systems
- Slotted Pages
- MVCC
- Snapshot Isolation
- Strict 2PL
- Volcano Iterator Model
- Buffer Pool Management
- LRU Cache
- Write-Ahead Logging (WAL)
- ARIES recovery framework

## Systems

- Multi-threading
- POSIX Threads
- TCP Networking
- HTTP Server
- Binary File Storage

---

# Current Project Scope

ChronosDB currently provides:

- Transactional relational database engine
- SQL compilation pipeline
- Slotted-page storage
- Binary persistence
- Buffer pool management
- MVCC concurrency
- Volcano execution engine
- Multi-database support
- Interactive CLI
- TCP server
- HTTP monitoring dashboard

---

# Future Scope

- Complete ARIES crash recovery
- Cost-based query optimization
- Additional SQL support
- Additional executor implementations
- More index structures (e.g. B+ Trees)
- Extended monitoring and administration

---

# Keywords

**Database Systems • RDBMS • C++17 • MVCC • Snapshot Isolation • SS2PL • Volcano Iterator Model • Slotted Pages • Buffer Pool • LRU • WAL • Query Processing • Systems Programming • TCP Networking • HTTP Dashboard**
