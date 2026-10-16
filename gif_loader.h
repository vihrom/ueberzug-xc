#ifndef GIF_LOADER_H
#define GIF_LOADER_H

/* Structure representing an animated GIF loaded into memory */
typedef struct {
    int width;
    int height;
    int num_frames;
    int cur_frame;
    int *delays;
    unsigned char **frames;
} GifAnim;

/* Load and scale all frames of a GIF file to target dimensions */
GifAnim *gif_load(const char *path, int target_w, int target_h);

/* Free allocated memory for a loaded GIF animation */
void gif_free(GifAnim *anim);

/* Get delay in milliseconds for the current GIF frame */
int gif_get_next_delay(const GifAnim *anim);

/* Get raw RGBA buffer for the current GIF frame */
const unsigned char *gif_get_current_frame(GifAnim *anim);

/* Advance GIF animation to the next frame */
void gif_step_frame(GifAnim *anim);

#endif /* GIF_LOADER_H */
