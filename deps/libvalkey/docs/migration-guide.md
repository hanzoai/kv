# Migration guide

Libkv can replace both libraries `hiredis` and `hiredis-cluster`.
This guide highlights which APIs that have changed and what you need to do when migrating to libkv.

The general actions needed are:

* Replace the prefix `redis` with `kv` in API usages.
* Replace the term `SSL` with `TLS` in API usages for secure communication.
* Update include paths depending on your previous installation.
  All `libkv` headers are now found under `include/kv/`.
* Update used build options, e.g. `USE_TLS` replaces `USE_SSL`.

## Migrating from `hiredis` v1.2.0

The type `sds` is removed from the public API.

### Renamed API functions

* `redisAsyncSetConnectCallbackNC` is renamed to `kvAsyncSetConnectCallback`.

### Removed API functions

* `redisFormatSdsCommandArgv` removed from API. Can be replaced with `kvFormatCommandArgv`.
* `redisFreeSdsCommand` removed since the `sds` type is for internal use only.
* `redisAsyncSetConnectCallback` is removed, but can be replaced with `kvAsyncSetConnectCallback` which accepts the non-const callback function prototype.

### Renamed API defines

* `HIREDIS_MAJOR` is renamed to `LIBKV_VERSION_MAJOR`.
* `HIREDIS_MINOR` is renamed to `LIBKV_VERSION_MINOR`.
* `HIREDIS_PATCH` is renamed to `LIBKV_VERSION_PATCH`.

### Removed API defines

* `HIREDIS_SONAME` removed.

## Migrating from `hiredis-cluster` 0.14.0

* The cluster client initiation procedure is changed and `kvClusterOptions`
  should be used to specify options when creating a context.
  See documentation for configuration examples when using the
  [Synchronous API](cluster.md#synchronous-api) or the
  [Asynchronous API](cluster.md#asynchronous-api).
  The [examples](../examples/) directory also contains some common client
  initiation examples that might be helpful.
* The default command to update the internal slot map is changed to `CLUSTER SLOTS`.
  `CLUSTER NODES` can be re-enabled through options using `KV_OPT_USE_CLUSTER_NODES`.
* A `kvClusterAsyncContext` now embeds a `kvClusterContext` instead of
  holding a pointer to it. Replace any use of `acc->cc` with `&acc->cc` or similar.

### Renamed API functions

* `ctx_get_by_node` is renamed to `kvClusterGetKVContext`.
* `actx_get_by_node` is renamed to `kvClusterGetKVAsyncContext`.

### Renamed API defines

* `REDIS_ROLE_NULL` is renamed to `KV_ROLE_UNKNOWN`.
* `REDIS_ROLE_MASTER` is renamed to `KV_ROLE_PRIMARY`.
* `REDIS_ROLE_SLAVE` is renamed to `KV_ROLE_REPLICA`.

### Removed API functions

* `redisClusterConnect2` removed, use `kvClusterConnectWithOptions`.
* `redisClusterContextInit` removed, use `kvClusterConnectWithOptions`.
* `redisClusterSetConnectCallback` removed, use `kvClusterOptions.connect_callback`.
* `redisClusterSetEventCallback` removed, use `kvClusterOptions.event_callback`.
* `redisClusterSetMaxRedirect` removed, use `kvClusterOptions.max_retry`.
* `redisClusterSetOptionAddNode` removed, use `kvClusterOptions.initial_nodes`.
* `redisClusterSetOptionAddNodes` removed, use `kvClusterOptions.initial_nodes`.
* `redisClusterSetOptionConnectBlock` removed since it was deprecated.
* `redisClusterSetOptionConnectNonBlock` removed since it was deprecated.
* `redisClusterSetOptionConnectTimeout` removed, use `kvClusterOptions.connect_timeout`.
* `redisClusterSetOptionMaxRetry` removed, use `kvClusterOptions.max_retry`.
* `redisClusterSetOptionParseSlaves` removed, use `kvClusterOptions.options` and `KV_OPT_USE_REPLICAS`.
* `redisClusterSetOptionPassword` removed, use `kvClusterOptions.password`.
* `redisClusterSetOptionRouteUseSlots` removed, `CLUSTER SLOTS` is used by default.
* `redisClusterSetOptionUsername` removed, use `kvClusterOptions.username`.
* `redisClusterAsyncConnect` removed, use `kvClusterAsyncConnectWithOptions` with options flag `KV_OPT_BLOCKING_INITIAL_UPDATE`.
* `redisClusterAsyncConnect2` removed, use `kvClusterAsyncConnectWithOptions`.
* `redisClusterAsyncContextInit` removed, `kvClusterAsyncConnectWithOptions` will initiate the context.
* `redisClusterAsyncSetConnectCallback` removed, but `kvClusterOptions.async_connect_callback` can be used which accepts a non-const callback function prototype.
* `redisClusterAsyncSetConnectCallbackNC` removed, use `kvClusterOptions.async_connect_callback`.
* `redisClusterAsyncSetDisconnectCallback` removed, use `kvClusterOptions.async_disconnect_callback`.
* `parse_cluster_nodes` removed from API, for internal use only.
* `parse_cluster_slots` removed from API, for internal use only.

### Removed API defines

* `HIRCLUSTER_FLAG_NULL` removed.
* `HIRCLUSTER_FLAG_ADD_SLAVE` removed, flag can be replaced with an option, see `KV_OPT_USE_REPLICAS`.
* `HIRCLUSTER_FLAG_ROUTE_USE_SLOTS` removed, the use of `CLUSTER SLOTS` is enabled by default.

### Removed support for splitting multi-key commands per slot

Since old days (from `hiredis-vip`) there has been support for sending some commands with multiple keys that covers multiple slots.
The client would split the command into multiple commands and send to each node handling each slot.
This was unnecessary complex and broke any expectations of atomicity.
Commands affected are `DEL`, `EXISTS`, `MGET` and `MSET`.

_Proposed action:_

Partition the keys by slot using `kvClusterGetSlotByKey` before sending affected commands.
Construct new commands when needed and send them using multiple calls to `kvClusterCommand` or equivalent.
