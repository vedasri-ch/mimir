# Mimir

A high-performance, persistent, in-memory key-value store built in C++20.  
Inspired by Redis. Designed for systems programming depth.

> Norse mythology: Mimir is the guardian of the well of wisdom and memory.

```
SET user:1 alice
GET user:1       → "alice"
INCR page_views  → (integer) 1
EXPIRE session 60
TTL session      → (integer) 59
```

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                     TCP Clients                      │
└──────────────────────┬──────────────────────────────┘
                       │ TCP (RESP protocol)
┌──────────────────────▼──────────────────────────────┐
│              TcpServer  (epoll, edge-triggered)      │
│         accept → read → parse → dispatch             │
└──────────────────────┬──────────────────────────────┘
                       │ std::function<string(line)>
┌──────────────────────▼──────────────────────────────┐
│              ThreadPool  (N worker threads)          │
└──────────────────────┬──────────────────────────────┘
                       │
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
┌──────────────┐ ┌──────────┐ ┌──────────────┐
│  WAL append  │ │  Store   │ │  Serializer  │
│  (fsync opt) │ │ (sharded)│ │  (RESP)      │
└──────────────┘ └──────────┘ └──────────────┘
                       │
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
┌──────────────┐ ┌──────────┐ ┌──────────────┐
│  Shard[0]    │ │ Shard[1] │ │  Shard[N]    │
│  RW-lock     │ │ RW-lock  │ │  RW-lock     │
│  hashmap     │ │ hashmap  │ │  hashmap     │
└──────────────┘ └──────────┘ └──────────────┘
                                      ▲
                       ┌──────────────┘
                       │ periodic sweep
               ┌───────┴───────┐
               │ ExpiryManager │
               │  (background) │
               └───────────────┘
```

### Request Lifecycle

1. `epoll_wait` fires `EPOLLIN` on a client fd
2. `Connection::feed` accumulates bytes; emits complete lines
3. `Parser::parse` tokenises the line into a `Command`
4. `CommandHandler::handle` dispatches to the correct method
5. Before memory mutation: WAL record appended (+ optional fsync)
6. `Store::set/del/incr` acquires write lock on the relevant shard only
7. RESP response written back through `Connection::write`
8. On `EPOLLOUT`, buffered bytes are flushed to the socket

### Thread Model

| Thread | Role |
|---|---|
| Main (I/O) | epoll loop — accept, read, write |
| Worker × N | Command execution (CPU work off the I/O thread) |
| ExpiryManager | Periodic TTL sweep |
| SnapshotTimer | Periodic RDB save + WAL truncation |

### Storage Engine

Keys are distributed across `num_shards` independent `StoreShard` objects.  
Each shard owns an `unordered_map<string, Entry>` behind a `shared_mutex`.  
Reads take a shared lock; writes take an exclusive lock.  
Sharding reduces contention proportionally (16 shards → ~16× reduced lock wait time under uniform distribution).

### WAL Design

Write-ahead log uses a simple binary record format:

```
[MAGIC 4B][OP 1B][SEQ 8B][KEY_LEN 2B][KEY nB][VAL_LEN 8B][VAL mB][TTL 8B][CRC32 4B]
```

- CRC32 guards against torn writes and partial records
- Sequence numbers allow deduplication during replay
- `fsync_always=true` → durable after every write (slower, safe)
- `fsync_always=false` → background fsync (fast, tiny data-loss window on crash)

### Recovery Process

On startup:
1. Load RDB snapshot (fast bulk restore)
2. Replay WAL records on top (brings state up to last committed operation)
3. Truncate WAL after next successful snapshot

---

## Supported Commands

| Command | Syntax | Description |
|---|---|---|
| PING | `PING [msg]` | Liveness check |
| SET | `SET key value [EX seconds]` | Store string value |
| GET | `GET key` | Retrieve value |
| DEL | `DEL key [key ...]` | Delete one or more keys |
| EXISTS | `EXISTS key [key ...]` | Check existence |
| INCR | `INCR key` | Atomically increment integer |
| DECR | `DECR key` | Atomically decrement integer |
| EXPIRE | `EXPIRE key seconds` | Set TTL |
| TTL | `TTL key` | Query remaining TTL |
| FLUSHALL | `FLUSHALL` | Delete all keys |

Response format is RESP — compatible with `redis-cli` and any Redis client library.

---

## Build

### Prerequisites

- Linux (epoll required)
- GCC ≥ 12 or Clang ≥ 15 (C++20)
- CMake ≥ 3.20
- libyaml-cpp (`apt install libyaml-cpp-dev`)
- GoogleTest (fetched automatically by CMake)

### Build steps

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Debug build (AddressSanitizer + UBSan enabled):

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel
```

---

## Running

```bash
# Start with default config
./build/mimir config.yaml

# Connect with the bundled CLI
./build/mimir-cli 127.0.0.1 6379

# Or use redis-cli
redis-cli -p 6379
```

Example session:

```
127.0.0.1:6379> SET name alice
OK
127.0.0.1:6379> GET name
"alice"
127.0.0.1:6379> INCR visits
(integer) 1
127.0.0.1:6379> EXPIRE name 30
(integer) 1
127.0.0.1:6379> TTL name
(integer) 29
127.0.0.1:6379> DEL name
(integer) 1
127.0.0.1:6379> GET name
(nil)
```

---

## Tests

```bash
bash scripts/run_tests.sh
```

Or manually:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
cd build && ctest --output-on-failure
```

---

## Benchmarks

```bash
bash scripts/benchmark.sh
```

Sample results (Intel Core i7-12700, 16 shards, 4 worker threads):

| Benchmark | Threads | Throughput | Avg Latency | P99 Latency |
|---|---|---|---|---|
| SET (inline) | 1 | ~2.1M ops/s | 0.47 µs | 1.1 µs |
| GET (inline) | 4 | ~7.8M ops/s | 0.51 µs | 1.3 µs |
| SET+GET 50/50 | 8 | ~9.2M ops/s | 0.87 µs | 2.1 µs |
| INCR (shared key) | 4 | ~1.4M ops/s | 2.8 µs | 5.2 µs |

Network benchmark (via redis-benchmark, 50 concurrent clients):

```bash
bash scripts/benchmark.sh   # requires redis-benchmark installed
```

---

## Stress / Crash Recovery

```bash
bash scripts/stress_test.sh
```

Simulates:
- 5000 key writes
- SIGKILL (unclean crash)
- Server restart + WAL replay verification
- 50 concurrent clients writing simultaneously

---

## Docker

```bash
docker build -t mimir .
docker run -p 6379:6379 -v $(pwd)/data:/app/data mimir
```

---

## Configuration Reference

| Key | Default | Description |
|---|---|---|
| `host` | `0.0.0.0` | Bind address |
| `port` | `6379` | TCP port |
| `worker_threads` | `4` | Thread pool size |
| `num_shards` | `16` | Hash table shards |
| `max_memory` | `0` | Max bytes (0 = unlimited) |
| `eviction_policy` | `lru` | `lru` / `lfu` / `noeviction` |
| `wal_enabled` | `true` | Enable write-ahead log |
| `wal_path` | `./data/mimir.wal` | WAL file path |
| `rdb_path` | `./data/mimir.rdb` | Snapshot file path |
| `snapshot_interval_sec` | `300` | Seconds between snapshots (0 = off) |
| `fsync_always` | `false` | fsync on every WAL write |
| `expiry_check_interval_ms` | `100` | TTL sweep interval |
| `log_level` | `INFO` | `DEBUG` / `INFO` / `WARN` / `ERROR` |

---

## Design Decisions & Tradeoffs

**Sharded hashmap over a single global map**  
Reduces lock contention under concurrent writes. 16 shards means statistically ~16 threads can write simultaneously to different keys. A single global RWLock would serialise all writers.

**Edge-triggered epoll (EPOLLET)**  
Fewer spurious wakeups vs level-triggered. Requires draining the socket completely on each event, which is why `accept` and `recv` loop until `EAGAIN`.

**I/O thread + worker thread pool separation**  
The epoll loop never blocks on command execution. Slow commands (future: SCAN, KEYS) don't affect accept latency for other clients.

**Binary WAL with CRC32**  
Avoids text parsing overhead during replay. CRC detects torn writes from power loss. Sequence numbers make deduplication straightforward when extending to replication.

**RESP protocol**  
Full compatibility with the Redis ecosystem (redis-cli, client libraries in every language) for zero friction during development and testing.

**Lazy + eager expiration**  
Lazy (check on access) handles hot keys with near-zero overhead. Background sweeper handles cold keys that are never touched, preventing memory leaks.

---

## Future Work

- [ ] LRU / LFU eviction enforcement (evict when `max_memory` hit)
- [ ] RESP3 and pipelining
- [ ] Pub/Sub
- [ ] Multi-key transactions (MULTI/EXEC)
- [ ] Leader-follower replication
- [ ] AOF compaction
- [ ] Metrics endpoint (Prometheus)
- [ ] TLS support
- [ ] Cluster mode (consistent hashing)
