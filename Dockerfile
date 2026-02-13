ARG VALKEY_VERSION=8.1

# Hanzo KV: Valkey with Hanzo defaults
FROM valkey/valkey:${VALKEY_VERSION}-alpine

LABEL maintainer="dev@hanzo.ai"
LABEL org.opencontainers.image.source="https://github.com/hanzoai/kv"
LABEL org.opencontainers.image.description="Hanzo KV - High-performance key-value store"

# Apply Hanzo defaults
RUN mkdir -p /etc/kv && \
    cp /etc/valkey/valkey.conf /etc/kv/kv.conf && \
    sed -i \
        -e 's/^bind 127.0.0.1/bind 0.0.0.0/' \
        -e 's/^protected-mode yes/protected-mode no/' \
        -e 's|^dir \./|dir /data|' \
        -e 's/^# maxmemory-policy noeviction/maxmemory-policy allkeys-lru/' \
        /etc/kv/kv.conf

EXPOSE 6379

HEALTHCHECK --interval=15s --timeout=3s --start-period=10s --retries=3 \
    CMD valkey-cli ping | grep -q PONG || exit 1

ENTRYPOINT ["valkey-server"]
CMD ["/etc/kv/kv.conf"]
