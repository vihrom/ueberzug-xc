#ifndef X11_BACKEND_H
#define X11_BACKEND_H

/* Placement specification in terminal cells */
typedef struct {
    int cell_x;
    int cell_y;
    int cell_width;
    int cell_height;
} ImagePlacement;

/* Initialize X11 display connection and default screen setup */
int x11_init(void);

/* Calculate layout, load/scale image payload, and display window */
void x11_show_image(const char *path, ImagePlacement placement);

/* Hide active image window without destroying backend state */
void x11_hide_image(void);

/* Destroy active windows, free resources, and close X11 display connection */
void x11_cleanup(void);

/* GIF animation helpers */
int x11_get_next_frame_delay(void);
void x11_step_gif_frame(void);

#endif /* X11_BACKEND_H */
