/*
 * zap_protocol.h — ZAP binary protocol helpers for hanzo/kv modules.
 */
#ifndef ZAP_PROTOCOL_H
#define ZAP_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define ZAP_MAGIC       "ZAP\0"
#define ZAP_MAGIC_LEN   4
#define ZAP_VERSION     1
#define ZAP_HEADER_SIZE 16

#define ZAP_MSG_KV         301
#define ZAP_FIELD_PATH     4
#define ZAP_FIELD_BODY     12
#define ZAP_RESP_STATUS    0
#define ZAP_RESP_BODY      4

static inline uint16_t zap_read_u16(const uint8_t *buf) {
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static inline uint32_t zap_read_u32(const uint8_t *buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static inline void zap_write_u16(uint8_t *buf, uint16_t val) {
    buf[0] = val & 0xFF; buf[1] = (val >> 8) & 0xFF;
}

static inline void zap_write_u32(uint8_t *buf, uint32_t val) {
    buf[0] = val & 0xFF; buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF; buf[3] = (val >> 24) & 0xFF;
}

typedef struct {
    uint16_t version;
    uint16_t flags;
    uint32_t root_offset;
    uint32_t size;
    uint16_t msg_type;
} ZapHeader;

static inline bool zap_parse_header(const uint8_t *data, size_t len, ZapHeader *hdr) {
    if (len < ZAP_HEADER_SIZE) return false;
    if (memcmp(data, ZAP_MAGIC, ZAP_MAGIC_LEN) != 0) return false;
    hdr->version = zap_read_u16(data + 4);
    if (hdr->version != ZAP_VERSION) return false;
    hdr->flags = zap_read_u16(data + 6);
    hdr->root_offset = zap_read_u32(data + 8);
    hdr->size = zap_read_u32(data + 12);
    hdr->msg_type = hdr->flags;
    return hdr->size <= len;
}

static inline const char *zap_read_text(const uint8_t *data, size_t data_len,
                                         uint32_t obj_off, int field_off,
                                         uint32_t *out_len) {
    uint32_t pos = obj_off + field_off;
    if (pos + 8 > data_len) { *out_len = 0; return NULL; }
    int32_t rel = (int32_t)zap_read_u32(data + pos);
    if (rel == 0) { *out_len = 0; return NULL; }
    *out_len = zap_read_u32(data + pos + 4);
    uint32_t abs = pos + rel;
    if (abs + *out_len > data_len) { *out_len = 0; return NULL; }
    return (const char *)(data + abs);
}

static inline const uint8_t *zap_read_bytes(const uint8_t *data, size_t data_len,
                                              uint32_t obj_off, int field_off,
                                              uint32_t *out_len) {
    return (const uint8_t *)zap_read_text(data, data_len, obj_off, field_off, out_len);
}

static inline uint8_t *zap_build_response(uint32_t status, const uint8_t *body,
                                            uint32_t body_len, uint32_t *out_size) {
    uint32_t total = (ZAP_HEADER_SIZE + 20 + body_len + 7) & ~7;
    uint8_t *buf = (uint8_t *)calloc(1, total);
    if (!buf) { *out_size = 0; return NULL; }
    memcpy(buf, ZAP_MAGIC, ZAP_MAGIC_LEN);
    zap_write_u16(buf + 4, ZAP_VERSION);
    zap_write_u32(buf + 8, ZAP_HEADER_SIZE);
    zap_write_u32(buf + 12, total);
    uint32_t root = ZAP_HEADER_SIZE;
    zap_write_u32(buf + root + ZAP_RESP_STATUS, status);
    int32_t rel = (int32_t)(20 - ZAP_RESP_BODY);
    zap_write_u32(buf + root + ZAP_RESP_BODY, (uint32_t)rel);
    zap_write_u32(buf + root + ZAP_RESP_BODY + 4, body_len);
    if (body && body_len > 0) memcpy(buf + root + 20, body, body_len);
    *out_size = total;
    return buf;
}

#endif /* ZAP_PROTOCOL_H */
