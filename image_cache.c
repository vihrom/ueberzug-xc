#include "image_cache.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "stb_image.h"
#include "stb_image_write.h"
#define STBIR_NO_SIMD
#include "stb_image_resize2.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef enum {
	DJB2_INIT_HASH = 5381,
	DJB2_SHIFTS = 5,
	CACHE_DIR_PERMS = 0700,
	CHANNELS_RGBA = 4
} ImageCacheConfig;

static unsigned char *gbuf = NULL;
static size_t gbuf_cap = 0;

/* DJB2 string hashing algorithm combined with file mtime and dimensions */
unsigned long
hash_config(const char *path, const CacheMeta *meta)
{
	unsigned long hash = DJB2_INIT_HASH;
	int c;

	if (path) {
		while ((c = (int)((unsigned char)*path++)))
			hash = ((hash << DJB2_SHIFTS) + hash) +
			       (unsigned long)c;
	}

	hash = ((hash << DJB2_SHIFTS) + hash) + (unsigned long)meta->mtime;
	hash = ((hash << DJB2_SHIFTS) + hash) + (unsigned long)meta->width;
	hash = ((hash << DJB2_SHIFTS) + hash) + (unsigned long)meta->height;
	return hash;
}

static int
ensure_buffer_capacity(size_t req)
{
	if (gbuf_cap >= req && gbuf != NULL)
		return 1;

	size_t cap = req + (req / 4);
	unsigned char *new_buf = realloc(gbuf, cap);
	if (!new_buf)
		return 0;

	gbuf = new_buf;
	gbuf_cap = cap;
	return 1;
}

static int
try_load_cached_image(const char *cache_path, int target_width,
		      int target_height, size_t req_bytes)
{
	struct stat cache_st;
	if (stat(cache_path, &cache_st) != 0) {
		return 0; // Cache miss
	}

	int cached_w = 0;
	int cached_h = 0;
	int cached_ch = 0;

	unsigned char *cache_data = stbi_load(cache_path, &cached_w, &cached_h,
					      &cached_ch, CHANNELS_RGBA);
	if (!cache_data) {
		return 0;
	}

	int dimensions_match =
		(cached_w == target_width && cached_h == target_height);
	if (dimensions_match) {
		memcpy(gbuf, cache_data, req_bytes);
	}

	stbi_image_free(cache_data);
	return dimensions_match;
}

static int
render_and_cache_image(const char *path, const char *cache_path,
		       int target_width, int target_height)
{
	int img_w = 0;
	int img_h = 0;
	int channels = 0;

	unsigned char *orig_data =
		stbi_load(path, &img_w, &img_h, &channels, CHANNELS_RGBA);
	if (!orig_data || img_w <= 0 || img_h <= 0) {
		if (orig_data) {
			stbi_image_free(orig_data);
		}
		return 0;
	}

	stbir_resize_uint8_linear(orig_data, img_w, img_h, 0, gbuf,
				  target_width, target_height, 0, STBIR_RGBA);
	stbi_image_free(orig_data);

	int saved = stbi_write_png(cache_path, target_width, target_height,
				   CHANNELS_RGBA, gbuf,
				   target_width * CHANNELS_RGBA);
	return saved;
}

unsigned char *
load_or_render_image(const char *path, long mtime, int target_width,
		     int target_height, size_t *out_cap)
{
	if (!path || target_width <= 0 || target_height <= 0)
		return NULL;

	const char *cache_dir = "/tmp/ueberzug_cache";
	if (mkdir(cache_dir, CACHE_DIR_PERMS) == -1 && errno != EEXIST)
		return NULL;

	CacheMeta meta = { .mtime = mtime,
			   .width = target_width,
			   .height = target_height };
	unsigned long cache_hash = hash_config(path, &meta);

	char cache_path[PATH_MAX];
	if (snprintf(cache_path, sizeof(cache_path), "%s/%lu.png", cache_dir,
		     cache_hash) >= (int)sizeof(cache_path)) {
		return NULL;
	}

	size_t req_bytes = (size_t)target_width * target_height * CHANNELS_RGBA;
	if (!ensure_buffer_capacity(req_bytes))
		return NULL;

	/* Try loading from cache */
	if (try_load_cached_image(cache_path, target_width, target_height,
				  req_bytes)) {
		if (out_cap)
			*out_cap = gbuf_cap;
		return gbuf;
	}

	/* Fallback to render, resize and cache on miss */
	if (!render_and_cache_image(path, cache_path, target_width,
				    target_height)) {
		return NULL;
	}

	if (out_cap)
		*out_cap = gbuf_cap;
	return gbuf;
}

void
free_image_cache_buffer(void)
{
	if (gbuf) {
		free(gbuf);
		gbuf = NULL;
		gbuf_cap = 0;
	}
}
