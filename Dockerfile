# Hanzo KV — built from the source in this repo.
#
# There is no upstream base image. The tree here IS the server, and its Makefile
# already names the binaries kv-* (src/Makefile: ENGINE_NAME=kv), so compiling it
# produces exactly what we ship. Pulling a prebuilt image and renaming its files
# would mean the artifact came from somewhere we do not control.
ARG ALPINE_VERSION=3.21

FROM alpine:${ALPINE_VERSION} AS build

RUN apk add --no-cache \
      build-base \
      linux-headers \
      openssl-dev \
      pkgconfig \
      git

WORKDIR /src
COPY . .

# `make` descends into src/, which emits kv-server, kv-cli and kv-benchmark;
# kv-sentinel, kv-check-aof and kv-check-rdb install as symlinks to kv-server.
RUN make -j"$(nproc)" BUILD_TLS=yes && make install PREFIX=/usr/local

FROM alpine:${ALPINE_VERSION}

LABEL maintainer="dev@hanzo.ai"
LABEL org.opencontainers.image.source="https://github.com/hanzoai/kv"
LABEL org.opencontainers.image.description="Hanzo KV - High-performance key-value store"
LABEL org.opencontainers.image.vendor="Hanzo AI"

RUN apk add --no-cache libssl3 libcrypto3 \
 && addgroup -S hanzo && adduser -S -G hanzo hanzo \
 && mkdir -p /data && chown hanzo:hanzo /data

# The whole directory, so the symlinks to kv-server arrive as symlinks.
COPY --from=build /usr/local/bin/ /usr/local/bin/

# `kv` is the interactive client, so `docker exec -it <c> kv` opens a shell.
# `kvd` is the daemon, matching luxd / hanzod / zood — the fleet names a
# long-running server <thing>d, and kv-server was the one that did not. The
# original name stays as a symlink so the in-image symlinks that already point
# at it (kv-sentinel, kv-check-aof, kv-check-rdb) keep resolving.
RUN ln -sf /usr/local/bin/kv-cli /usr/local/bin/kv \
 && ln -sf /usr/local/bin/kv-server /usr/local/bin/kvd

USER hanzo
WORKDIR /data
VOLUME /data
EXPOSE 6379

HEALTHCHECK --interval=15s --timeout=3s --start-period=10s --retries=3 \
    CMD kv ping | grep -q PONG || exit 1

ENTRYPOINT ["kvd"]
CMD ["--bind", "0.0.0.0", "--dir", "/data", "--maxmemory-policy", "allkeys-lru", "--protected-mode", "no"]
