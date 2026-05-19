<<<<<<< HEAD
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
=======
[![codecov](https://codecov.io/gh/valkey-io/valkey/graph/badge.svg?token=KYYSJAYC5F)](https://codecov.io/gh/valkey-io/valkey)

This project was forked from the open source Redis project right before the transition to their new source available licenses.

This README is just a fast *quick start* document. More details can be found under [valkey.io](https://valkey.io/)

# What is Valkey?

Valkey is a high-performance data structure server that primarily serves key/value workloads.
It supports a wide range of native structures and an extensible plugin system for adding new data structures and access patterns.

# Building Valkey using `Makefile`

Valkey can be compiled and used on Linux, macOS, OpenBSD, NetBSD, FreeBSD.
We support big endian and little endian architectures, and both 32 bit
and 64 bit systems.

It may compile on Solaris derived systems (for instance SmartOS) but our
support for this platform is *best effort* and Valkey is not guaranteed to
work as well as in Linux, macOS, and \*BSD.

It is as simple as:

    % make

To build with TLS support, you'll need OpenSSL development libraries (e.g.
libssl-dev on Debian/Ubuntu).

To build TLS support as Valkey built-in:

    % make BUILD_TLS=yes

To build TLS as Valkey module:

    % make BUILD_TLS=module

Note that sentinel mode does not support TLS module.

To build with experimental RDMA support you'll need RDMA development libraries
(e.g. librdmacm-dev and libibverbs-dev on Debian/Ubuntu).

To build RDMA support as Valkey built-in:

    % make BUILD_RDMA=yes

To build RDMA as Valkey module:

    % make BUILD_RDMA=module

To build with systemd support, you'll need systemd development libraries (such
as libsystemd-dev on Debian/Ubuntu or systemd-devel on CentOS) and run:

    % make USE_SYSTEMD=yes

To append a suffix to Valkey program names, use:

    % make PROG_SUFFIX="-alt"

You can build a 32 bit Valkey binary using:

    % make 32bit

After building Valkey, it is a good idea to test it using:

    % make test

The above runs the main integration tests. Additional tests are started using:

    % make test-unit     # Unit tests
    % make test-modules  # Tests of the module API
    % make test-sentinel # Valkey Sentinel integration tests
    % make test-cluster  # Valkey Cluster integration tests

More about running the integration tests can be found in
[tests/README.md](tests/README.md) and for unit tests, see
[src/unit/README.md](src/unit/README.md).

## Fixing build problems with dependencies or cached build options

Valkey has some dependencies which are included in the `deps` directory.
`make` does not automatically rebuild dependencies even if something in
the source code of dependencies changes.

When you update the source code with `git pull` or when code inside the
dependencies tree is modified in any other way, make sure to use the following
command in order to really clean everything and rebuild from scratch:

    % make distclean

This will clean: jemalloc, lua, libvalkey, linenoise and other dependencies.

Also if you force certain build options like 32bit target, no C compiler
optimizations (for debugging purposes), and other similar build time options,
those options are cached indefinitely until you issue a `make distclean`
command.

## Fixing problems building 32 bit binaries

If after building Valkey with a 32 bit target you need to rebuild it
with a 64 bit target, or the other way around, you need to perform a
`make distclean` in the root directory of the Valkey distribution.

In case of build errors when trying to build a 32 bit binary of Valkey, try
the following steps:

* Install the package libc6-dev-i386 (also try g++-multilib).
* Try using the following command line instead of `make 32bit`:
  `make CFLAGS="-m32 -march=native" LDFLAGS="-m32"`

## Allocator

Selecting a non-default memory allocator when building Valkey is done by setting
the `MALLOC` environment variable. Valkey is compiled and linked against libc
malloc by default, with the exception of jemalloc being the default on Linux
systems. This default was picked because jemalloc has proven to have fewer
fragmentation problems than libc malloc.

To force compiling against libc malloc, use:

    % make MALLOC=libc

To compile against jemalloc on Mac OS X systems, use:

    % make MALLOC=jemalloc

## Monotonic clock

By default, Valkey will build using the POSIX clock_gettime function as the
monotonic clock source.  On most modern systems, the internal processor clock
can be used to improve performance.  Cautions can be found here:
    http://oliveryang.net/2015/09/pitfalls-of-TSC-usage/

To build with support for the processor's internal instruction clock, use:

    % make CFLAGS="-DUSE_PROCESSOR_CLOCK"

## Verbose build

Valkey will build with a user-friendly colorized output by default.
If you want to see a more verbose output, use the following:

    % make V=1

# Running Valkey

To run Valkey with the default configuration, just type:

    % cd src
    % ./valkey-server

If you want to provide your valkey.conf, you have to run it using an additional
parameter (the path of the configuration file):

    % cd src
    % ./valkey-server /path/to/valkey.conf

It is possible to alter the Valkey configuration by passing parameters directly
as options using the command line. Examples:

    % ./valkey-server --port 9999 --replicaof 127.0.0.1 6379
    % ./valkey-server /etc/valkey/6379.conf --loglevel debug

All the options in valkey.conf are also supported as options using the command
line, with exactly the same name.

# Running Valkey with TLS:

## Running manually

To manually run a Valkey server with TLS mode (assuming `./utils/gen-test-certs.sh`
was invoked so sample certificates/keys are available):

* TLS built-in mode:
    ```
    ./src/valkey-server --tls-port 6379 --port 0 \
        --tls-cert-file ./tests/tls/valkey.crt \
        --tls-key-file ./tests/tls/valkey.key \
        --tls-ca-cert-file ./tests/tls/ca.crt
    ```

* TLS module mode:
    ```
    ./src/valkey-server --tls-port 6379 --port 0 \
        --tls-cert-file ./tests/tls/valkey.crt \
        --tls-key-file ./tests/tls/valkey.key \
        --tls-ca-cert-file ./tests/tls/ca.crt \
        --loadmodule src/valkey-tls.so
    ```

Note that you can disable TCP by specifying `--port 0` explicitly.
It's also possible to have both TCP and TLS available at the same time,
but you'll have to assign different ports.

Use `valkey-cli` to connect to the Valkey server:
```
./src/valkey-cli --tls \
    --cert ./tests/tls/valkey.crt \
    --key ./tests/tls/valkey.key \
    --cacert ./tests/tls/ca.crt
```

Specifying `--tls-replication yes` makes a replica connect to the primary.

Using `--tls-cluster yes` makes Valkey Cluster use TLS across nodes.

# Running Valkey with RDMA:

Note that Valkey Over RDMA is an experimental feature.
It may be changed or removed in any minor or major version.
Currently, it is only supported on Linux.

* RDMA built-in mode:
    ```
    ./src/valkey-server --protected-mode no \
         --rdma-bind 192.168.122.100 --rdma-port 6379
    ```

* RDMA module mode:
    ```
    ./src/valkey-server --protected-mode no \
         --loadmodule src/valkey-rdma.so --rdma-bind 192.168.122.100 --rdma-port 6379
    ```

It's possible to change bind address/port of RDMA by runtime command:

    192.168.122.100:6379> CONFIG SET rdma-port 6380

It's also possible to have both RDMA and TCP available, and there is no
conflict of TCP(6379) and RDMA(6379), Ex:

    % ./src/valkey-server --protected-mode no \
         --loadmodule src/valkey-rdma.so --rdma-bind 192.168.122.100 --rdma-port 6379 \
         --port 6379

Note that the network card (192.168.122.100 of this example) should support
RDMA. To test a server supports RDMA or not:

    % rdma res show (a new version iproute2 package)
Or:

    % ibv_devices


# Playing with Valkey

You can use valkey-cli to play with Valkey. Start a valkey-server instance,
then in another terminal try the following:

    % cd src
    % ./valkey-cli
    valkey> ping
    PONG
    valkey> set foo bar
    OK
    valkey> get foo
    "bar"
    valkey> incr mycounter
    (integer) 1
    valkey> incr mycounter
    (integer) 2
    valkey>

# Installing Valkey

In order to install Valkey binaries into /usr/local/bin, just use:

    % make install

You can use `make PREFIX=/some/other/directory install` if you wish to use a
different destination.

_Note_: For compatibility with Redis, we create symlinks from the Redis names (`redis-server`, `redis-cli`, etc.) to the Valkey binaries installed by `make install`.
The symlinks are created in same directory as the Valkey binaries.
The symlinks are removed when using `make uninstall`.
The creation of the symlinks can be skipped by setting the makefile variable `USE_REDIS_SYMLINKS=no`.

`make install` will just install binaries in your system, but will not configure
init scripts and configuration files in the appropriate place. This is not
needed if you just want to play a bit with Valkey, but if you are installing
it the proper way for a production system, we have a script that does this
for Ubuntu and Debian systems:

    % cd utils
    % ./install_server.sh

_Note_: `install_server.sh` will not work on macOS; it is built for Linux only.

The script will ask you a few questions and will setup everything you need
to run Valkey properly as a background daemon that will start again on
system reboots.

You'll be able to stop and start Valkey using the script named
`/etc/init.d/valkey_<portnumber>`, for instance `/etc/init.d/valkey_6379`.

# Building using `CMake`

In addition to the traditional `Makefile` build, Valkey supports an alternative, **experimental**, build system using `CMake`.

To build and install `Valkey`, in `Release` mode (an optimized build), type this into your terminal:
>>>>>>> v9.0.4

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

<<<<<<< HEAD
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
=======
[1]: https://github.com/valkey-io/valkey/blob/unstable/COPYING
[2]: https://github.com/valkey-io/valkey/blob/unstable/CONTRIBUTING.md
[3]: https://github.com/valkey-io/valkey/blob/unstable/SECURITY.md
>>>>>>> v9.0.4
