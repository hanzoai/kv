ARG KV_VERSION=9

# Hanzo KV: High-performance key-value store
FROM kv/kv:${KV_VERSION}-alpine AS base

FROM base

LABEL maintainer="dev@hanzo.ai"
LABEL org.opencontainers.image.source="https://github.com/hanzoai/kv"
LABEL org.opencontainers.image.description="Hanzo KV - High-performance key-value store"
LABEL org.opencontainers.image.vendor="Hanzo AI"

# Install Hanzo KV CLI tools
# Primary names are kv-* ; legacy kv-* names remain as symlinks
RUN cp /usr/local/bin/kv-server    /usr/local/bin/kv-server    \
 && cp /usr/local/bin/kv-cli       /usr/local/bin/kv-cli       \
 && ln -sf /usr/local/bin/kv-cli       /usr/local/bin/kv           \
 && cp /usr/local/bin/kv-sentinel  /usr/local/bin/kv-sentinel  2>/dev/null; \
    cp /usr/local/bin/kv-benchmark /usr/local/bin/kv-benchmark 2>/dev/null; \
    cp /usr/local/bin/kv-check-aof /usr/local/bin/kv-check-aof 2>/dev/null; \
    cp /usr/local/bin/kv-check-rdb /usr/local/bin/kv-check-rdb 2>/dev/null; \
    true

EXPOSE 6379

HEALTHCHECK --interval=15s --timeout=3s --start-period=10s --retries=3 \
    CMD kv ping | grep -q PONG || exit 1

ENTRYPOINT ["kv-server"]
CMD ["--bind", "0.0.0.0", "--dir", "/data", "--maxmemory-policy", "allkeys-lru", "--protected-mode", "no"]
