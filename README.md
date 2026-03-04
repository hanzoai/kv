<p align="center">
  <strong>Hanzo KV</strong>
</p>

<p align="center">
  High-performance key-value store for the Hanzo ecosystem.<br/>
  In-memory data store used as database, cache, streaming engine, and message broker.
</p>

<p align="center">
  <a href="https://github.com/hanzoai/kv/actions"><img src="https://github.com/hanzoai/kv/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/hanzoai/kv/releases"><img src="https://img.shields.io/github/v/release/hanzoai/kv" alt="Release"></a>
  <a href="https://github.com/hanzoai/kv/blob/main/LICENSE"><img src="https://img.shields.io/github/license/hanzoai/kv" alt="License"></a>
</p>

---

## Features

- **In-memory key-value store** -- sub-millisecond reads and writes
- **Redis-compatible protocol** -- drop-in replacement for existing Redis clients
- **Persistence** -- RDB snapshots and AOF (append-only file) for durability
- **Replication** -- primary-replica with automatic failover via Sentinel
- **Lua scripting** -- server-side scripting for atomic operations
- **Pub/Sub** -- publish and subscribe messaging
- **Streams** -- append-only log data structure for event sourcing
- **Cluster mode** -- horizontal scaling with automatic sharding
- **Modules** -- extensible plugin system for custom data structures
- **ZAP native** -- built-in [ZAP binary protocol](https://github.com/luxfi/zap) on port 9653 (17x faster than JSON-RPC)
- **Multi-arch** -- linux/amd64 and linux/arm64

## Quick Start

### Docker

```bash
docker run -d --name hanzo-kv -p 6379:6379 ghcr.io/hanzoai/kv
```

### Connect

```bash
docker exec -it hanzo-kv kv

127.0.0.1:6379> SET hello world
OK
127.0.0.1:6379> GET hello
"world"
```

Any Redis-compatible CLI also works out of the box.

### Build from Source

```bash
make
make test
make install
```

## CLI Tools

| Command | Description |
|---------|-------------|
| `kv` | Interactive CLI (default) |
| `kv-server` | Start KV server |
| `kv-cli` | Command-line client |
| `kv-sentinel` | High-availability sentinel |
| `kv-benchmark` | Performance benchmarking |
| `kv-check-aof` | AOF file integrity check |
| `kv-check-rdb` | RDB file integrity check |

## Configuration

Pass a config file at startup:

```bash
kv-server /etc/kv/kv.conf
```

Or set options via command line:

```bash
kv-server --port 6379 --maxmemory 256mb --appendonly yes
```

## ZAP Binary Protocol

Hanzo KV speaks [ZAP](https://github.com/luxfi/zap) natively on port **9653** — no sidecar needed.

ZAP is a zero-copy binary protocol that's 17x faster than JSON-RPC with 11x less memory usage.

### Enable ZAP

ZAP is enabled by default. Load the module:

```bash
kv-server --loadmodule /path/to/zap.so
# or with custom port:
kv-server --loadmodule /path/to/zap.so PORT 9653
```

### ZAP Operations

| Path | Body | Description |
|------|------|-------------|
| `/get` | `{"key":"mykey"}` | GET a key |
| `/set` | `{"key":"mykey","value":"myval"}` | SET a key |
| `/del` | `{"key":"mykey"}` | DEL a key |
| `/cmd` | `{"cmd":"PING","args":[]}` | Execute any command |

### Module API

Develop custom modules using the KV Module API:

```c
#include "kvmodule.h"

int KVModule_OnLoad(KVModuleCtx *ctx, KVModuleString **argv, int argc) {
    if (KVModule_Init(ctx, "mymod", 1, KVMODULE_APIVER_1) == KVMODULE_ERR)
        return KVMODULE_ERR;
    // register commands...
    return KVMODULE_OK;
}
```

## Client SDKs

| Language | Package | Install |
|----------|---------|---------|
| Python | [hanzo-kv](https://pypi.org/project/hanzo-kv) | `pip install hanzo-kv` |
| Go | [hanzo/kv-go](https://github.com/hanzoai/kv-go) | `go get github.com/hanzoai/kv-go` |
| Node.js | [@hanzo/kv](https://github.com/hanzoai/kv-client) | `npm install @hanzo/kv` |

Any Redis-compatible client library will also work.

## Documentation

Full documentation is available at [docs.hanzo.ai](https://docs.hanzo.ai).

## License

BSD-3-Clause

Copyright (c) 2024-2026 Hanzo AI Inc. All rights reserved.
