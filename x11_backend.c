#include "x11_backend.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gif_loader.h"
#include "image_cache.h"
#include "stb_image.h"

typedef enum {
	ARGB_DEPTH_BITS = 32,
	STRING_BASE_DECIMAL = 10,
	DEFAULT_CELL_WIDTH = 8,
	DEFAULT_CELL_HEIGHT = 16,
	COLOR_CHANNEL_MAX = 255,
	BITS_PER_PIXEL_32 = 32
} X11BackendConfig;

static Display *display = NULL;
static Window win = 0;
static GC gc = NULL;
static Colormap cmap = 0;
static int screen;

static GifAnim *current_gif = NULL;

typedef struct {
	int pixel_x;
	int pixel_y;
	int pixel_width;
	int pixel_height;
	long mtime;
} ImageGeometry;

/* X11 error handler to prevent crashes on BadValue / BadWindow */
static int
x11_error_handler(Display *dpy, XErrorEvent *event)
{
	char err_msg[256];
	XGetErrorText(dpy, event->error_code, err_msg, sizeof(err_msg));
	fprintf(stderr, "X11 Error: %s (request code: %d)\n", err_msg,
		event->request_code);
	return 0;
}

int
x11_init(void)
{
	display = XOpenDisplay(NULL);
	if (!display)
		return 0;
	screen = DefaultScreen(display);
	XSetErrorHandler(x11_error_handler);
	return 1;
}

static Visual *
get_argb_visual(Display *dpy, int scr, int *out_depth)
{
	XVisualInfo vinfo_template;
	XVisualInfo *vinfo;
	int nitems;
	Visual *visual = NULL;

	vinfo_template.screen = scr;
	vinfo_template.depth = ARGB_DEPTH_BITS;
	vinfo_template.class = TrueColor;

	vinfo = XGetVisualInfo(
		dpy, VisualScreenMask | VisualDepthMask | VisualClassMask,
		&vinfo_template, &nitems);
	if (vinfo) {
		if (nitems > 0) {
			visual = vinfo[0].visual;
			if (out_depth)
				*out_depth = vinfo[0].depth;
		}
		XFree(vinfo);
	}

	if (!visual) {
		visual = DefaultVisual(dpy, scr);
		if (out_depth)
			*out_depth = DefaultDepth(dpy, scr);
	}

	return visual;
}

static Window
get_parent_window(void)
{
	char *window_id_str = getenv("WINDOWID");
	if (window_id_str && *window_id_str != '\0') {
		char *endptr = NULL;
		long wid = strtol(window_id_str, &endptr, STRING_BASE_DECIMAL);
		if (endptr && *endptr == '\0' && wid > 0) {
			return (Window)wid;
		}
	}

	if (!display)
		return None;

	Window parent = 0;
	int revert_to;
	XGetInputFocus(display, &parent, &revert_to);
	return (parent != 0 && parent != None && parent != PointerRoot) ?
		       parent :
		       DefaultRootWindow(display);
}

static void
ensure_window_created(Visual *visual, int depth, int pixel_x, int pixel_y,
		      int pixel_width, int pixel_height)
{
	if (win != 0 || pixel_width <= 0 || pixel_height <= 0)
		return;

	Window parent_window = get_parent_window();
	XSetWindowAttributes attr;
	memset(&attr, 0, sizeof(attr));

	if (depth == ARGB_DEPTH_BITS) {
		cmap = XCreateColormap(display, parent_window, visual,
				       AllocNone);
		if (cmap) {
			attr.colormap = cmap;
			attr.border_pixel = 0;
			attr.background_pixel = 0;

			win = XCreateWindow(display, parent_window, pixel_x,
					    pixel_y, pixel_width, pixel_height,
					    0, depth, InputOutput, visual,
					    CWColormap | CWBorderPixel |
						    CWBackPixel,
					    &attr);
		}
	}

	if (win == 0) {
		win = XCreateSimpleWindow(display, parent_window, pixel_x,
					  pixel_y, pixel_width, pixel_height, 0,
					  BlackPixel(display, screen),
					  BlackPixel(display, screen));
	}

	if (win != 0) {
		gc = XCreateGC(display, win, 0, NULL);
	}
}

static unsigned char *
convert_rgba_to_bgra(const unsigned char *rgba_buf, int pixel_width,
		     int pixel_height)
{
	if (pixel_width <= 0 || pixel_height <= 0)
		return NULL;

	size_t req_bytes = (size_t)pixel_width * pixel_height * 4;
	unsigned char *bgra_buf = malloc(req_bytes);
	if (!bgra_buf)
		return NULL;

	for (size_t i = 0; i < req_bytes; i += 4) {
		unsigned char red = rgba_buf[i + 0];
		unsigned char green = rgba_buf[i + 1];
		unsigned char blue = rgba_buf[i + 2];
		unsigned char alpha = rgba_buf[i + 3];

		if (alpha == 0) {
			bgra_buf[i + 0] = 0;
			bgra_buf[i + 1] = 0;
			bgra_buf[i + 2] = 0;
			bgra_buf[i + 3] = 0;
		} else if (alpha == COLOR_CHANNEL_MAX) {
			bgra_buf[i + 0] = blue;
			bgra_buf[i + 1] = green;
			bgra_buf[i + 2] = red;
			bgra_buf[i + 3] = alpha;
		} else {
			bgra_buf[i + 0] = (unsigned char)((blue * alpha) /
							  COLOR_CHANNEL_MAX);
			bgra_buf[i + 1] = (unsigned char)((green * alpha) /
							  COLOR_CHANNEL_MAX);
			bgra_buf[i + 2] = (unsigned char)((red * alpha) /
							  COLOR_CHANNEL_MAX);
			bgra_buf[i + 3] = alpha;
		}
	}

	return bgra_buf;
}

static void
get_terminal_cell_size(int *cell_w, int *cell_h)
{
	struct winsize winsz;
	*cell_w = DEFAULT_CELL_WIDTH;
	*cell_h = DEFAULT_CELL_HEIGHT;

	// Check multiple FDs for window size (stderr, stdin, stdout)
	int fds[] = { STDERR_FILENO, STDIN_FILENO, STDOUT_FILENO };
	for (int i = 0; i < 3; i++) {
		if (ioctl(fds[i], TIOCGWINSZ, &winsz) == 0) {
			if (winsz.ws_col > 0 && winsz.ws_row > 0 &&
			    winsz.ws_xpixel > 0 && winsz.ws_ypixel > 0) {
				*cell_w = winsz.ws_xpixel / winsz.ws_col;
				*cell_h = winsz.ws_ypixel / winsz.ws_row;
				return;
			}
		}
	}
}

static int
calculate_dimensions(const char *path, ImagePlacement placement,
		     ImageGeometry *geometry)
{
	struct stat file_st;
	double scale_w;
	double scale_h;
	double scale;
	int cell_w;
	int cell_h;
	int max_pw;
	int max_ph;
	int img_w;
	int img_h;
	int channels;

	get_terminal_cell_size(&cell_w, &cell_h);
	max_pw = placement.cell_width * cell_w;
	max_ph = placement.cell_height * cell_h;

	if (max_pw <= 0 || max_ph <= 0)
		return 0;

	geometry->mtime = 0;
	if (stat(path, &file_st) == 0)
		geometry->mtime = file_st.st_mtime;

	if (!stbi_info(path, &img_w, &img_h, &channels) || img_w <= 0 ||
	    img_h <= 0)
		return 0;

	scale_w = (double)max_pw / img_w;
	scale_h = (double)max_ph / img_h;
	scale = (scale_w < scale_h) ? scale_w : scale_h;

	geometry->pixel_width = (int)(img_w * scale);
	geometry->pixel_height = (int)(img_h * scale);

	if (geometry->pixel_width < 1)
		geometry->pixel_width = 1;
	if (geometry->pixel_height < 1)
		geometry->pixel_height = 1;

	geometry->pixel_x = (placement.cell_x * cell_w) +
			    ((max_pw - geometry->pixel_width) / 2);
	geometry->pixel_y = (placement.cell_y * cell_h) +
			    ((max_ph - geometry->pixel_height) / 2);

	return 1;
}

static void
render_frame_to_window(const unsigned char *rgba_buf, int pixel_width,
		       int pixel_height)
{
	if (!win || !rgba_buf || pixel_width <= 0 || pixel_height <= 0)
		return;

	unsigned char *bgra_buf =
		convert_rgba_to_bgra(rgba_buf, pixel_width, pixel_height);
	if (!bgra_buf)
		return;

	int depth;
	Visual *visual = get_argb_visual(display, screen, &depth);
	XImage *ximage = XCreateImage(display, visual, depth, ZPixmap, 0,
				      (char *)bgra_buf, pixel_width,
				      pixel_height, BITS_PER_PIXEL_32,
				      pixel_width * 4);
	if (ximage) {
		XInitImage(ximage);
		XPutImage(display, win, gc, ximage, 0, 0, 0, 0, pixel_width,
			  pixel_height);
		XSync(display, False);
		XDestroyImage(ximage);
	} else {
		free(bgra_buf);
	}
}

static const unsigned char *
prepare_image_payload(const char *path, long mtime, int pixel_width,
		      int pixel_height)
{
	if (current_gif) {
		gif_free(current_gif);
		current_gif = NULL;
	}

	current_gif = gif_load(path, pixel_width, pixel_height);
	if (current_gif) {
		return gif_get_current_frame(current_gif);
	}

	return load_or_render_image(path, mtime, pixel_width, pixel_height,
				    NULL);
}

void
x11_show_image(const char *path, ImagePlacement placement)
{
	ImageGeometry geom;

	if (!display || placement.cell_width <= 0 || placement.cell_height <= 0)
		return;

	if (!calculate_dimensions(path, placement, &geom))
		return;

	const unsigned char *frame_data = prepare_image_payload(
		path, geom.mtime, geom.pixel_width, geom.pixel_height);
	if (!frame_data)
		return;

	int depth;
	Visual *visual = get_argb_visual(display, screen, &depth);

	ensure_window_created(visual, depth, geom.pixel_x, geom.pixel_y,
			      geom.pixel_width, geom.pixel_height);

	if (win == 0)
		return;

	XMoveResizeWindow(display, win, geom.pixel_x, geom.pixel_y,
			  geom.pixel_width, geom.pixel_height);

	XMapRaised(display, win);
	render_frame_to_window(frame_data, geom.pixel_width, geom.pixel_height);
	XSync(display, False);
}

int
x11_get_next_frame_delay(void)
{
	return gif_get_next_delay(current_gif);
}

void
x11_step_gif_frame(void)
{
	if (!current_gif)
		return;
	gif_step_frame(current_gif);
	render_frame_to_window(gif_get_current_frame(current_gif),
			       current_gif->width, current_gif->height);
}

void
x11_hide_image(void)
{
	if (current_gif) {
		gif_free(current_gif);
		current_gif = NULL;
	}

	if (win != 0 && display) {
		XUnmapWindow(display, win);
		XSync(display, False);
	}
}

void
x11_cleanup(void)
{
	x11_hide_image();

	if (display) {
		if (gc != NULL) {
			XFreeGC(display, gc);
			gc = NULL;
		}
		if (win != 0) {
			XDestroyWindow(display, win);
			win = 0;
		}
		if (cmap != 0) {
			XFreeColormap(display, cmap);
			cmap = 0;
		}
		XCloseDisplay(display);
		display = NULL;
	}
	free_image_cache_buffer();
}
