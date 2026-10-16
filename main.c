#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "json_parser.h"
#include "x11_backend.h"

typedef enum {
	INITIAL_BUF_SIZE = 4096,
	MAX_LINE_SIZE = 64 * 1024,
	READ_CHUNK_SIZE = 1024,
	POLL_TIMEOUT_INFINITE = -1,
	POLL_TIMEOUT_IMMEDIATE = 0
} MainConfig;

static int
append_to_buffer(char **buf_ptr, size_t *cap_ptr, size_t *len_ptr,
		 const char *chunk, ssize_t n)
{
	if (*len_ptr + (size_t)n >= *cap_ptr) {
		while (*len_ptr + (size_t)n >= *cap_ptr) {
			*cap_ptr *= 2;
		}
		if (*cap_ptr > MAX_LINE_SIZE) {
			*len_ptr = 0; // Prevent OOM on malformed input stream
			return 0;
		}
		char *new_buf = realloc(*buf_ptr, *cap_ptr);
		if (!new_buf) {
			return -1;
		}
		*buf_ptr = new_buf;
	}

	memcpy(*buf_ptr + *len_ptr, chunk, (size_t)n);
	*len_ptr += (size_t)n;
	(*buf_ptr)[*len_ptr] = '\0';
	return 1;
}

static void
execute_command(const Command *cmd)
{
	switch (cmd->action) {
	case ACTION_ADD:
		x11_show_image(cmd->path,
			       (ImagePlacement){ .cell_x = cmd->x,
						 .cell_y = cmd->y,
						 .cell_width = cmd->width,
						 .cell_height = cmd->height });
		break;
	case ACTION_REMOVE:
		x11_hide_image();
		break;
	default:
		break;
	}
}

static void
process_buffer_lines(char *line_buf, size_t *buf_len, struct pollfd *pfd)
{
	char *line_start = line_buf;
	char *newline;

	while ((newline = strchr(line_start, '\n')) != NULL) {
		*newline = '\0';
		char *current_line = line_start;
		line_start = newline + 1;

		int has_more = (poll(pfd, 1, POLL_TIMEOUT_IMMEDIATE) > 0 &&
				(pfd->revents & POLLIN));

		Command cmd;
		if (parse_json_line(current_line, &cmd)) {
			/* Fast-scroll debouncing: drop outdated ADD if more input is pending */
			if (cmd.action == ACTION_ADD && has_more) {
				free_command(&cmd);
				continue;
			}

			execute_command(&cmd);
			free_command(&cmd);
		}
	}

	/* Shift remaining incomplete line data to the beginning of the buffer */
	size_t processed_len = (size_t)(line_start - line_buf);
	if (processed_len > 0 && processed_len < *buf_len) {
		*buf_len -= processed_len;
		memmove(line_buf, line_start, *buf_len);
		line_buf[*buf_len] = '\0';
	} else if (processed_len >= *buf_len) {
		*buf_len = 0;
		line_buf[0] = '\0';
	}
}

int
main(void)
{
	if (!x11_init()) {
		fprintf(stderr, "Failed to initialize X11 display\n");
		return EXIT_FAILURE;
	}

	size_t buf_cap = INITIAL_BUF_SIZE;
	size_t buf_len = 0;
	char *line_buf = malloc(buf_cap);
	if (!line_buf) {
		x11_cleanup();
		return EXIT_FAILURE;
	}

	struct pollfd pfd;
	pfd.fd = STDIN_FILENO;
	pfd.events = POLLIN;

	int running = 1;
	while (running) {
		/* Use GIF frame delay as poll timeout if animation is active, else block infinitely */
		int gif_delay = x11_get_next_frame_delay();
		int poll_timeout = (gif_delay > 0) ? gif_delay :
						     POLL_TIMEOUT_INFINITE;

		int ret = poll(&pfd, 1, poll_timeout);
		if (ret < 0) {
			break; // Interrupted or error
		}

		if (ret == 0) {
			if (gif_delay > 0) {
				x11_step_gif_frame();
			}
			continue;
		}

		if (pfd.revents & (POLLHUP | POLLERR)) {
			running = 0;
		}

		if (pfd.revents & POLLIN) {
			char chunk[READ_CHUNK_SIZE];
			ssize_t n = read(STDIN_FILENO, chunk, sizeof(chunk));
			if (n <= 0) {
				break; // EOF or error
			}

			if (append_to_buffer(&line_buf, &buf_cap, &buf_len,
					     chunk, n) < 0) {
				break; // OOM on realloc
			}
		}

		process_buffer_lines(line_buf, &buf_len, &pfd);
	}

	free(line_buf);
	x11_cleanup();
	return EXIT_SUCCESS;
}
