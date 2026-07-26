ARG KV_VERSION=9

# Hanzo KV: High-performance key-value store.
#
# Hanzo KV is Valkey-based. This FROM is an ADDRESS on Docker Hub, not a brand
# we ship — it has to name the image that actually exists. A rebrand pass once
# rewrote it to `kv/kv:9-alpine`, which does not exist ("object not found"), so
# the image could not build at all. What we ship is branded on the surface: the
# kv-* commands installed below, and the vendor labels.
FROM valkey/valkey:${KV_VERSION}-alpine AS base

FROM base

LABEL maintainer="dev@hanzo.ai"
LABEL org.opencontainers.image.source="https://github.com/hanzoai/kv"
LABEL org.opencontainers.image.description="Hanzo KV - High-performance key-value store"
LABEL org.opencontainers.image.vendor="Hanzo AI"

# Install the Hanzo KV commands. kv-* is the first-party name and the one to
# use; the base image's own names stay as it shipped them, which is what keeps
# stock clients and tooling working.
#
# The SOURCE path of each cp names a file in the base image. A rebrand pass once
# rewrote both sides to kv-*, making every line `cp X X` — a silent no-op that
# produced an image with no kv-* commands at all. Source stays as the base
# image names it; only the destination is ours.
RUN cp /usr/local/bin/valkey-server    /usr/local/bin/kv-server    \
 && cp /usr/local/bin/valkey-cli       /usr/local/bin/kv-cli       \
 && ln -sf /usr/local/bin/kv-cli       /usr/local/bin/kv           \
 && cp /usr/local/bin/valkey-sentinel  /usr/local/bin/kv-sentinel  2>/dev/null; \
    cp /usr/local/bin/valkey-benchmark /usr/local/bin/kv-benchmark 2>/dev/null; \
    cp /usr/local/bin/valkey-check-aof /usr/local/bin/kv-check-aof 2>/dev/null; \
    cp /usr/local/bin/valkey-check-rdb /usr/local/bin/kv-check-rdb 2>/dev/null; \
    true

EXPOSE 6379

HEALTHCHECK --interval=15s --timeout=3s --start-period=10s --retries=3 \
    CMD kv ping | grep -q PONG || exit 1

ENTRYPOINT ["kv-server"]
CMD ["--bind", "0.0.0.0", "--dir", "/data", "--maxmemory-policy", "allkeys-lru", "--protected-mode", "no"]

