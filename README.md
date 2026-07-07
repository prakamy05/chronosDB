# ChronosDB

> **A transactional relational database management system (RDBMS) built from scratch in C++17.**
>
> ChronosDB implements a modular database engine architecture featuring **Slotted-Page Storage**, **LRU Buffer Pool Management**, **Multi-Version Concurrency Control (MVCC)**, and a **Volcano (Iterator Model) Query Execution Engine**. The system also provides client accessibility through a **multi-threaded TCP server**, an **interactive CLI**, and an **HTTP monitoring dashboard**.

---

## Table of Contents

- [Project Overview](#project-overview)
- [Installation](#installation)
- [Running ChronosDB](#running-chronosdb)
- [Ways to Use ChronosDB](#ways-to-use-chronosdb)
- [Supported SQL Commands](#supported-sql-commands)
- [Features](#features)
- [Architecture Overview](#architecture-overview)
- [Project Structure](#project-structure)
- [Architecture Details](#architecture-details)
- [Technologies Used](#technologies-used)
- [Current Project Scope](#current-project-scope)
- [Future Scope](#future-scope)

---

# Project Overview

ChronosDB is an educational relational database engine developed to explore the internal architecture of modern database management systems.

The project combines multiple core database subsystems into a layered architecture, including:

- SQL compilation
- Transaction management
- Concurrency control
- Storage management
- Buffer management
- Query execution
- Database virtualization
- Networking
- Performance monitoring

The system stores data using a **slotted-page storage engine**, executes queries through a **Volcano iterator execution pipeline**, manages concurrent transactions using **MVCC and Strict Two-Phase Locking (SS2PL)**, and caches disk pages using a fixed-size **LRU Buffer Pool**.

---

# Installation

## Requirements

- C++17 compatible compiler
- POSIX threads (`pthread`)
- Make

---

## Build

```bash
make
```

---

## Run

```bash
./minidb
```

---

## Clean

```bash
make clean
```

---

# Running ChronosDB

On startup, ChronosDB initializes:

- Interactive CLI
- TCP Server (Port **5433**)
- HTTP Dashboard (Port **8080**)

---

# Ways to Use ChronosDB

## 1. Interactive CLI

Run SQL statements directly inside the terminal.

Example:

```sql
CREATE DATABASE school;
USE school;

CREATE TABLE students (
    id INT,
    name VARCHAR
);

INSERT INTO students VALUES (1, Alice);

SELECT * FROM students;
```

---

## 2. TCP Server

ChronosDB starts a multi-threaded TCP server on:

```
Port 5433
```

Applications can send raw SQL query strings and receive tabular query results.

---

## 3. HTTP Dashboard

The integrated dashboard runs on:

```
http://localhost:8080
```

It provides:

- Engine telemetry
- Performance statistics
- Buffer cache statistics
- Execution metrics
- Database schema information

---

# Supported SQL Commands

## Database Commands

```sql
CREATE DATABASE <name>;
```

Creates a new isolated database.

---

```sql
USE <name>;
```

Switches the active database.

---

## Table Commands

```sql
CREATE TABLE <name>(
    column type,
    ...
);
```

Supported types:

- INT
- VARCHAR

---

```sql
DROP TABLE <name>;
```

Removes a table and associated secondary indexes.

---

## Data Manipulation

```sql
INSERT INTO table VALUES (...);
```

Performs:

- Type validation
- Tuple serialization
- Secondary index updates
- MVCC metadata creation

---

```sql
DELETE FROM table
WHERE column=value;
```

Performs:

- Exclusive locking
- MVCC delete marking
- Secondary index removal

---

## Query Processing

```sql
SELECT ...
FROM ...
```

Supported clauses include:

- WHERE
- JOIN
- GROUP BY
- ORDER BY

The execution engine dynamically selects:

- Sequential Scan
- Index Scan

depending on index availability.

---

# Features

### Storage Engine

- Slotted-page storage layout
- Variable-length row storage
- Binary page serialization
- Fixed page size of **4096 bytes**

### Query Processing

- SQL Lexer
- SQL Parser
- AST generation
- Volcano iterator execution engine
- Sequential scans
- Index scans
- Projection
- Filtering
- Sorting
- Hash aggregation
- Nested loop joins

### Transaction Processing

- Multi-Version Concurrency Control (MVCC)
- Snapshot Isolation
- Strict Two-Phase Locking (SS2PL)
- Transaction lifecycle management
- Write-Ahead Logging (WAL)

### Buffer Management

- LRU Buffer Pool
- Fixed-size cache (**10 page frames**)
- Dirty page flushing
- Page pinning
- Disk abstraction

### Storage

- Binary database files
- Append-only WAL files
- Slotted page management
- MVCC tuple version metadata

### Database Management

- Multi-database support
- Database virtualization
- Catalog management
- Schema serialization

### Networking

- Multi-threaded TCP Server
- Interactive CLI
- HTTP Monitoring Dashboard

### Monitoring

The HTTP dashboard tracks:

- Total requests
- Network payload sizes
- Execution faults
- Buffer cache hits
- Rolling execution latency

---

# Architecture Overview

```
                     Client Applications
              ┌──────────────┬──────────────┐
              │              │              │
              ▼              ▼              ▼
          CLI Shell      TCP Server    HTTP Dashboard
                 \          |          /
                  \         |         /
                   ▼        ▼        ▼
              DatabaseClusterManager
                        │
                        ▼
                     DBEngine
                        │
                        ▼
          ┌───────────────────────────┐
          │       Compiler Layer      │
          │ Lexer → Parser → AST      │
          └───────────────────────────┘
                        │
                        ▼
          ┌───────────────────────────┐
          │     Execution Engine      │
          │ Volcano Iterator Model    │
          └───────────────────────────┘
                        │
                        ▼
          ┌───────────────────────────┐
          │   Concurrency Layer       │
          │ MVCC + Lock Manager       │
          └───────────────────────────┘
                        │
                        ▼
          ┌───────────────────────────┐
          │ Buffer Pool Manager (LRU) │
          └───────────────────────────┘
                        │
                        ▼
          ┌───────────────────────────┐
          │     Persistent Storage    │
          │ Disk Manager + WAL        │
          └───────────────────────────┘
```

---

# Project Structure

```text
ChronosDB
│
├── Makefile
├── main.cpp
│
└── src
    │
    ├── common
    │   └── types.h
    │
    ├── compiler
    │   ├── ast.h
    │   ├── lexer.h
    │   └── parser.h
    │
    ├── concurrency
    │   ├── lock_manager.h
    │   ├── lock_manager.cpp
    │   ├── transaction.h
    │   └── transaction_manager.h
    │
    ├── execution
    │   ├── catalog.h
    │   ├── executors.h
    │   └── engine.h
    │
    ├── network
    │   ├── tcp_server.h
    │   └── http_dashboard.h
    │
    └── storage
        ├── page.h
        ├── disk_manager.h
        ├── disk_manager.cpp
        ├── buffer_pool_manager.h
        ├── buffer_pool_manager.cpp
        └── recovery.h
```

---

# Architecture Details

## Network Layer

Provides client access through:

- Interactive CLI
- TCP Server
- HTTP Dashboard

Responsible for receiving SQL queries and forwarding them to the database engine.

---

## Virtualization Layer

Implemented by:

- DatabaseClusterManager
- DBEngine

Responsibilities:

- Database isolation
- Active database selection
- Catalog loading
- Query routing

---

## Compiler Layer

### Lexer

- Tokenizes SQL
- Normalizes keywords

### Parser

Builds structured query metadata supporting:

- projections
- WHERE clauses
- grouping
- ordering
- aggregations

### AST

Stores parsed SQL command structures.

---

## Execution Layer

Implements the Volcano iterator execution model.

Operators include:

- SeqScanExecutor
- IndexScanExecutor
- FilterExecutor
- ProjectionExecutor
- SortExecutor
- HashAggregationExecutor
- NestedLoopJoinExecutor

---

## Concurrency Layer

Provides:

- MVCC Snapshot management
- LockManager
- Transaction lifecycle management
- RID locking
- Write-Ahead Log integration

---

## Memory Layer

Implemented using BufferPoolManager.

Features:

- 10 frame cache
- LRU replacement
- Dirty page flushing
- Page pinning

---

## Storage Layer

Implemented using:

- DiskManager
- Page
- Recovery

Responsibilities include:

- Binary page storage
- Slotted-page layout
- WAL management
- ARIES recovery framework

---

# Technologies Used

## Language

- C++17

## Database Concepts

- Relational Database Systems
- Slotted Page Storage
- MVCC
- Snapshot Isolation
- Strict Two-Phase Locking
- Volcano Iterator Model
- Buffer Pool Management
- LRU Cache
- Write-Ahead Logging
- ARIES Recovery Framework

## Systems

- Multi-threading
- TCP Networking
- HTTP Server
- Binary File Storage

---

# Current Project Scope

ChronosDB currently implements:

- Transactional relational database engine
- SQL compilation pipeline
- Slotted-page storage
- Binary persistence
- Buffer pool management
- MVCC concurrency control
- Strict Two-Phase Locking
- Volcano iterator query execution
- Secondary index acceleration
- Multi-database virtualization
- TCP client server
- Interactive CLI
- HTTP monitoring dashboard
- Performance telemetry

---

# Future Scope

The current codebase already provides the framework for ARIES recovery through `recovery.h`.

Additional extensions can be built on top of the existing architecture, including:

- Completing the ARIES crash recovery implementation
- Expanding SQL language support
- Additional query optimization strategies
- Additional executor implementations
- More index structures
- Extended monitoring and administrative capabilities

---

# Technologies Used

## Language

- C++17

## Database Concepts

- Relational Database Systems
- Slotted Page Storage
- MVCC
- Snapshot Isolation
- Strict Two-Phase Locking
- Volcano Iterator Model
- Buffer Pool Management
- LRU Cache
- Write-Ahead Logging
- ARIES Recovery Framework

## Systems

- Multi-threading
- TCP Networking
- HTTP Server
- Binary File Storage

---

# Keywords

**Database Systems • RDBMS • Storage Engine • Database Engine • Systems Programming • C++17 • MVCC • Snapshot Isolation • SS2PL • Volcano Iterator Model • Query Processing • Query Execution Engine • Buffer Pool Manager • LRU Cache • Slotted Pages • Write-Ahead Logging • WAL • Binary Storage • Multi-threading • TCP Networking • HTTP Server • Backend Engineering • Operating Systems • Low-Level Systems • Software Architecture**
```
