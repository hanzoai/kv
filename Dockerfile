ARG ALPINE_VERSION=3.21

# Stage 1: Build Valkey from source
FROM alpine:${ALPINE_VERSION} AS builder

RUN apk add --no-cache \
        build-base \
        linux-headers \
        openssl-dev

COPY . /src
WORKDIR /src

RUN make -j$(nproc) BUILD_TLS=yes && \
    make install PREFIX=/opt/kv

# Stage 2: Runtime image
FROM alpine:${ALPINE_VERSION}

LABEL maintainer="dev@hanzo.ai"
LABEL org.opencontainers.image.source="https://github.com/hanzoai/kv"
LABEL org.opencontainers.image.description="Hanzo KV - High-performance key-value store"

RUN apk add --no-cache \
        openssl \
        tzdata && \
    addgroup -S -g 1000 kv && \
    adduser -S -G kv -u 1000 -h /data kv && \
    mkdir -p /data /etc/kv && \
    chown kv:kv /data

COPY --from=builder /opt/kv/bin/ /usr/local/bin/
COPY valkey.conf /etc/kv/kv.conf

# Apply Hanzo defaults
RUN sed -i \
        -e 's/^bind 127.0.0.1/bind 0.0.0.0/' \
        -e 's/^protected-mode yes/protected-mode no/' \
        -e 's|^dir \./|dir /data|' \
        -e 's/^# maxmemory-policy noeviction/maxmemory-policy allkeys-lru/' \
        /etc/kv/kv.conf

VOLUME /data
WORKDIR /data
USER kv

EXPOSE 6379 6380

HEALTHCHECK --interval=15s --timeout=3s --start-period=10s --retries=3 \
    CMD valkey-cli ping | grep -q PONG || exit 1

ENTRYPOINT ["valkey-server"]
CMD ["/etc/kv/kv.conf"]
