#include "gif_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"
#define STBIR_NO_SIMD
#include "stb_image_resize2.h"

typedef enum {
	MAX_GIF_FRAMES = 60,
	DEFAULT_FRAME_DELAY_MS = 100,
	GIF_header_size = 6,
	GIF_MAX_FILE_SIZE = 50 * 1024 * 1024,
	CHANNELS_RGBA = 4
} GifLoaderConfig;

static unsigned char *
read_file_to_memory(const char *path, long *size_out)
{
	FILE *gif_file = fopen(path, "rb");
	if (!gif_file)
		return NULL;

	if (fseek(gif_file, 0, SEEK_END) != 0) {
		fclose(gif_file);
		return NULL;
	}
	long size = ftell(gif_file);
	if (size <= GIF_header_size || size > GIF_MAX_FILE_SIZE) {
		fclose(gif_file);
		return NULL;
	}
	if (fseek(gif_file, 0, SEEK_SET) != 0) {
		fclose(gif_file);
		return NULL;
	}

	unsigned char *file_buf = malloc((size_t)size);
	if (!file_buf) {
		fclose(gif_file);
		return NULL;
	}

	if (fread(file_buf, 1, (size_t)size, gif_file) != (size_t)size) {
		free(file_buf);
		fclose(gif_file);
		return NULL;
	}

	fclose(gif_file);
	*size_out = size;
	return file_buf;
}

GifAnim *
gif_load(const char *path, int target_w, int target_h)
{
	unsigned char *file_buf;
	unsigned char *raw_frames;
	int *delays = NULL;
	int img_w;
	int img_h;
	int num_frames;
	int comp;
	long size = 0;

	if (!path || target_w <= 0 || target_h <= 0)
		return NULL;

	file_buf = read_file_to_memory(path, &size);
	if (!file_buf)
		return NULL;

	/* Quick check for GIF bytes ("GIF87a" or "GIF89a") */
	if (memcmp(file_buf, "GIF8", 4) != 0) {
		free(file_buf);
		return NULL;
	}

	/* Decode raw GIF animation from memory buffer */
	raw_frames = stbi_load_gif_from_memory(file_buf, (int)size, &delays,
					       &img_w, &img_h, &num_frames,
					       &comp, CHANNELS_RGBA);
	free(file_buf);

	if (!raw_frames || num_frames <= 1 || img_w <= 0 || img_h <= 0) {
		if (raw_frames)
			stbi_image_free(raw_frames);
		if (delays)
			stbi_image_free(delays);
		return NULL;
	}

	/* Cap frame count to prevent excessive memory usage */
	if (num_frames > MAX_GIF_FRAMES) {
		num_frames = MAX_GIF_FRAMES;
	}

	GifAnim *anim = calloc(1, sizeof(GifAnim));
	if (!anim) {
		stbi_image_free(raw_frames);
		stbi_image_free(delays);
		return NULL;
	}

	anim->width = target_w;
	anim->height = target_h;
	anim->num_frames = num_frames;
	anim->cur_frame = 0;
	anim->delays = delays;

	anim->frames = (unsigned char **)malloc(sizeof(unsigned char *) *
						(size_t)num_frames);
	if (!anim->frames) {
		if (anim->delays)
			stbi_image_free(anim->delays);
		free(anim);
		stbi_image_free(raw_frames);
		return NULL;
	}

	size_t orig_frame_bytes = (size_t)img_w * img_h * CHANNELS_RGBA;
	size_t resized_frame_bytes =
		(size_t)target_w * target_h * CHANNELS_RGBA;

	/* Pre-scale each frame to target window size */
	for (int i = 0; i < num_frames; i++) {
		anim->frames[i] = malloc(resized_frame_bytes);
		if (!anim->frames[i]) {
			for (int j = 0; j < i; j++)
				free(anim->frames[j]);
			free((void *)anim->frames);
			if (anim->delays)
				stbi_image_free(anim->delays);
			free(anim);
			stbi_image_free(raw_frames);
			return NULL;
		}

		if (img_w == target_w && img_h == target_h) {
			memcpy(anim->frames[i],
			       raw_frames + ((size_t)i * orig_frame_bytes),
			       resized_frame_bytes);
		} else {
			stbir_resize_uint8_linear(
				raw_frames + ((size_t)i * orig_frame_bytes),
				img_w, img_h, 0, anim->frames[i], target_w,
				target_h, 0, STBIR_RGBA);
		}
	}

	stbi_image_free(raw_frames);
	return anim;
}

void
gif_free(GifAnim *anim)
{
	if (!anim)
		return;
	for (int i = 0; i < anim->num_frames; i++) {
		free(anim->frames[i]);
	}

	free((void *)anim->frames);

	if (anim->delays)
		stbi_image_free(anim->delays);
	free(anim);
}

int
gif_get_next_delay(const GifAnim *anim)
{
	if (!anim || anim->num_frames <= 1 || !anim->delays)
		return -1;
	int delay = anim->delays[anim->cur_frame];
	return (delay > 0) ? delay : DEFAULT_FRAME_DELAY_MS;
}

const unsigned char *
gif_get_current_frame(GifAnim *anim)
{
	if (!anim || !anim->frames)
		return NULL;
	return anim->frames[anim->cur_frame];
}

void
gif_step_frame(GifAnim *anim)
{
	if (!anim || anim->num_frames <= 1)
		return;
	anim->cur_frame = (anim->cur_frame + 1) % anim->num_frames;
}
