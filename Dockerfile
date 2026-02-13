ARG VALKEY_VERSION=8.1

# Hanzo KV: Valkey with Hanzo defaults
FROM valkey/valkey:${VALKEY_VERSION}-alpine

LABEL maintainer="dev@hanzo.ai"
LABEL org.opencontainers.image.source="https://github.com/hanzoai/kv"
LABEL org.opencontainers.image.description="Hanzo KV - High-performance key-value store"

EXPOSE 6379

HEALTHCHECK --interval=15s --timeout=3s --start-period=10s --retries=3 \
    CMD valkey-cli ping | grep -q PONG || exit 1

ENTRYPOINT ["valkey-server"]
CMD ["--bind", "0.0.0.0", "--dir", "/data", "--maxmemory-policy", "allkeys-lru", "--protected-mode", "no"]
