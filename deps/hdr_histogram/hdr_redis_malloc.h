#ifndef HDR_MALLOC_H__
#define HDR_MALLOC_H__

void *kv_malloc(size_t size);
void *zcalloc_num(size_t num, size_t size);
void *kv_realloc(void *ptr, size_t size);
void kv_free(void *ptr);

#define hdr_malloc kv_malloc
#define hdr_calloc zcalloc_num
#define hdr_realloc kv_realloc
#define hdr_free kv_free
#endif
