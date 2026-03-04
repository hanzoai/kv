# Set the directory to find KV binaries for tests. Historically we've been
# using make to build binaries under the src/ directory. Since we start supporting
# CMake as well, we allow changing base dir by passing ENV variable `KV_BIN_DIR`,
# which could be either absolute or relative path (e.g. cmake-build-debug/bin).
if {[info exists ::env(KV_BIN_DIR)]} {
    set ::KV_BIN_DIR [file normalize $::env(KV_BIN_DIR)]
} else {
    set ::KV_BIN_DIR "[pwd]/src"
}

# Optional program suffix (e.g. `make PROG_SUFFIX=-alt` will create binary kv-server-alt).
# Passed from `make test` as environment variable KV_PROG_SUFFIX.
set ::KV_PROG_SUFFIX [expr {
    [info exists ::env(KV_PROG_SUFFIX)] ? $::env(KV_PROG_SUFFIX) : ""
}]

# Helper to build absolute paths
proc kv_bin_absolute_path {name} {
    set full_name "${name}${::KV_PROG_SUFFIX}"
    return [file join $::KV_BIN_DIR $full_name]
}

set ::KV_SERVER_BIN    [kv_bin_absolute_path "kv-server"]
set ::KV_CLI_BIN       [kv_bin_absolute_path "kv-cli"]
set ::KV_BENCHMARK_BIN [kv_bin_absolute_path "kv-benchmark"]
set ::KV_CHECK_AOF_BIN [kv_bin_absolute_path "kv-check-aof"]
set ::KV_CHECK_RDB_BIN [kv_bin_absolute_path "kv-check-rdb"]
set ::KV_SENTINEL_BIN  [kv_bin_absolute_path "kv-sentinel"]

# TLS module path: in CMake builds it's in lib/, in Make builds it's in src/
if {[info exists ::env(KV_BIN_DIR)]} {
    # CMake build: lib/ is sibling to bin/
    set ::KV_TLS_MODULE [file join [file dirname $::KV_BIN_DIR] "lib" "kv-tls${::KV_PROG_SUFFIX}.so"]
} else {
    set ::KV_TLS_MODULE "[pwd]/src/kv-tls${::KV_PROG_SUFFIX}.so"
}

if {![file executable $::KV_SERVER_BIN]} {
    error "Binary not found or not executable: $::KV_SERVER_BIN"
}