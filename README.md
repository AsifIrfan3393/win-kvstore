# High-Performance Distributed In-Memory Key-Value Store (C++20)

This project is a production-oriented, low-latency distributed in-memory key-value store designed for high concurrency and operational realism. It ships with a sharded storage engine, bounded worker pool, TTL and eviction, snapshot + WAL persistence, primary/replica replication, dynamic shard rebalancing, observability, fault injection, and benchmarking utilities.

## Architecture Overview

```
┌──────────────────────────┐
│         CLI/Config        │
└────────────┬─────────────┘
             │
┌────────────▼─────────────┐
│     TCP KV Service        │
│ (GET/PUT/DEL/BATCH/etc.) │
└────────────┬─────────────┘
             │
┌────────────▼─────────────┐
│   ThreadPool + Queue      │
└────────────┬─────────────┘
             │
┌────────────▼─────────────┐
│    Sharded In-Memory      │
│    Store (LRU + TTL)      │
└────────────┬─────────────┘
             │
┌────────────▼─────────────┐
│ Snapshot + WAL Persistence│
└────────────┬─────────────┘
             │
┌────────────▼─────────────┐
│  Replication Broadcaster  │
└──────────────────────────┘

┌──────────────────────────┐
│     Metrics HTTP Server   │
└──────────────────────────┘
```

### Modules

- **Networking:** TCP service for client requests; line-based protocol.
- **Storage:** Sharded hash table with fine-grained locks, TTL expiration, and LRU eviction.
- **Concurrency:** Bounded thread pool to provide back-pressure.
- **Persistence:** Periodic snapshots and optional WAL with corruption detection.
- **Replication:** Leader streaming log entries to replicas.
- **Rebalancing:** Online shard count changes to redistribute keys.
- **Observability:** JSON metrics endpoint for throughput, latency, memory, eviction, snapshot duration, WAL size, replication lag.
- **Fault Injection:** Configurable WAL/snapshot/replication delays and failure probability.
- **Benchmarking:** Multi-threaded load generator with hotspot and read/write ratios.

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

### Windows (PowerShell)

```powershell
cmake -S . -B build
cmake --build build --config Release
```

> Note: Windows builds link against `ws2_32` for Winsock networking support.

## Run a Leader

```bash
./build/kvstore --port 9090 --metrics-port 9100 --replication-port 9091 --role leader \
  --shards 32 --workers 8 --memory-budget 536870912
```

### Windows (PowerShell)

```powershell
./build/Release/kvstore.exe --port 9090 --metrics-port 9100 --replication-port 9091 --role leader `
  --shards 32 --workers 8 --memory-budget 536870912
```

## Run a Replica

```bash
./build/kvstore --port 9092 --metrics-port 9102 --role replica --replica-of 127.0.0.1:9091
```

### Windows (PowerShell)

```powershell
./build/Release/kvstore.exe --port 9092 --metrics-port 9102 --role replica --replica-of 127.0.0.1:9091
```

## Protocol Examples

```
PUT key1 value1 30
GET key1
DEL key1
BATCH 2
PUT key2 value2
DEL key3
REBALANCE 64
PING
```

## Metrics

```bash
curl http://localhost:9100/metrics
```

## Benchmark

```bash
./build/kvbench --bind 127.0.0.1 --port 9090 --bench-threads 16 --bench-requests 50000 \
  --bench-read-ratio 0.7 --bench-hotspot 0.2 --bench-output bench.json
```

### Windows (PowerShell)

```powershell
./build/Release/kvbench.exe --bind 127.0.0.1 --port 9090 --bench-threads 16 --bench-requests 50000 `
  --bench-read-ratio 0.7 --bench-hotspot 0.2 --bench-output bench.json
```

## Operational Notes

- Snapshots are written to `data/snapshot.dat`.
- WAL is written to `data/wal.log` and replayed on startup with CRC validation.
- Replication lag is tracked by the broadcaster as a best-effort metric.
- TTL expiration runs in a background thread.

## Fault Injection Flags

You can simulate failures and slowness at runtime using CLI flags:

- `--wal-delay <ms>`: add latency to WAL appends.
- `--wal-fail-prob <float>`: probability of WAL write failure (0.0-1.0).
- `--snapshot-delay <ms>`: inject delay into snapshot writes.
- `--replication-delay <ms>`: add delay when streaming to replicas.

## Why This Is Enough With Only Two Projects

**Project 1 (this KV store)** demonstrates systems programming, concurrency, networking, replication, recovery, performance engineering, and platform design.

**Project 2 (online banking backend)** demonstrates transactions, real APIs, a real database, idempotency, auditing, integration testing, and domain correctness.

Together they cover the breadth expected by product companies, infrastructure teams, financial systems firms, logistics/platform engineering, and performance-focused organizations.
