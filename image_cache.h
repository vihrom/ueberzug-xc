#ifndef IMAGE_CACHE_H
#define IMAGE_CACHE_H

#include <stddef.h>

typedef struct {
	long mtime;
	int width;
	int height;
} CacheMeta;

/* Generate unique hash key for cached image configurations */
unsigned long hash_config(const char *path, const CacheMeta *meta);

/* Load cached image or render and cache new scaled frame */
unsigned char *load_or_render_image(const char *path, long mtime, int target_width, int target_height, size_t *out_cap);

/* Free globally reused image cache buffer */
void free_image_cache_buffer(void);

#endif /* IMAGE_CACHE_H */
