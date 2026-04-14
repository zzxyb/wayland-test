#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wayland-client.h>

#define WINDOW_WIDTH 480
#define WINDOW_HEIGHT 360
#define GRID_COLS 3
#define GRID_ROWS 3
#define FRAMES_PER_CELL 100
#define FRAMES_SHOW_FULL_GRID 150
#define CELL_WIDTH 160
#define CELL_HEIGHT 120
#define BUFFER_WIDTH (GRID_COLS * CELL_WIDTH)
#define BUFFER_HEIGHT (GRID_ROWS * CELL_HEIGHT)

struct viewporter_test_client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *xdg_wm_base;
	struct zxdg_decoration_manager_v1 *decoration_manager;
	struct wp_viewporter *viewporter;

	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct zxdg_toplevel_decoration_v1 *toplevel_decoration;
	struct wp_viewport *viewport;

	struct wl_buffer *buffer;
	struct wl_callback *frame_callback;
	void *shm_data;
	size_t shm_size;

	bool running;
	bool configured;
	uint32_t frame_index;
};

static void request_next_frame(struct viewporter_test_client *client);
static void render_frame(struct viewporter_test_client *client);

static int
create_shm_file(size_t size)
{
	static const char template[] = "/viewporter-test-XXXXXX";
	const char *path;
	char *name;
	int fd;
	int ret;

	path = getenv("XDG_RUNTIME_DIR");
	if (!path) {
		errno = ENOENT;
		return -1;
	}

	name = malloc(strlen(path) + sizeof(template));
	if (!name)
		return -1;

	strcpy(name, path);
	strcat(name, template);

	fd = mkstemp(name);
	if (fd >= 0) {
		unlink(name);
		fcntl(fd, F_SETFD, FD_CLOEXEC);
	}

	free(name);

	if (fd < 0)
		return -1;

	ret = ftruncate(fd, size);
	if (ret < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

static void
paint_nine_grid(struct viewporter_test_client *client)
{
	static const uint32_t colors[GRID_ROWS * GRID_COLS] = {
		0xFFFF3B30, /* red */
		0xFFFF9500, /* orange */
		0xFFFFCC00, /* yellow */
		0xFF34C759, /* green */
		0xFF00C7BE, /* cyan */
		0xFF007AFF, /* blue */
		0xFF5856D6, /* indigo */
		0xFFAF52DE, /* violet */
		0xFFFF2D55  /* pink */
	};
	uint32_t *pixels = client->shm_data;
	int x;
	int y;

	for (y = 0; y < BUFFER_HEIGHT; ++y) {
		for (x = 0; x < BUFFER_WIDTH; ++x) {
			int col = x / CELL_WIDTH;
			int row = y / CELL_HEIGHT;
			int idx = row * GRID_COLS + col;
			pixels[y * BUFFER_WIDTH + x] = colors[idx];
		}
	}
}

static struct wl_buffer *
create_buffer(struct viewporter_test_client *client)
{
	struct wl_shm_pool *pool;
	int stride = BUFFER_WIDTH * 4;
	int size = stride * BUFFER_HEIGHT;
	int fd;
	struct wl_buffer *buffer;

	fd = create_shm_file(size);
	if (fd < 0) {
		fprintf(stderr, "Creating shared memory file failed: %s\n", strerror(errno));
		return NULL;
	}

	client->shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (client->shm_data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %s\n", strerror(errno));
		close(fd);
		return NULL;
	}
	client->shm_size = size;

	pool = wl_shm_create_pool(client->shm, fd, size);
	buffer = wl_shm_pool_create_buffer(pool, 0, BUFFER_WIDTH, BUFFER_HEIGHT,
			stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	paint_nine_grid(client);
	return buffer;
}

static void
registry_handle_global(void *data, struct wl_registry *registry,
		uint32_t id, const char *interface, uint32_t version)
{
	struct viewporter_test_client *client = data;

	if (strcmp(interface, "wl_compositor") == 0) {
		client->compositor = wl_registry_bind(registry, id,
				&wl_compositor_interface, 4);
	} else if (strcmp(interface, "wl_shm") == 0) {
		client->shm = wl_registry_bind(registry, id,
				&wl_shm_interface, 1);
	} else if (strcmp(interface, "xdg_wm_base") == 0) {
		client->xdg_wm_base = wl_registry_bind(registry, id,
				&xdg_wm_base_interface, 1);
	} else if (strcmp(interface, "zxdg_decoration_manager_v1") == 0) {
		client->decoration_manager = wl_registry_bind(registry, id,
				&zxdg_decoration_manager_v1_interface,
				version > 1 ? 1 : version);
	} else if (strcmp(interface, "wp_viewporter") == 0) {
		client->viewporter = wl_registry_bind(registry, id,
				&wp_viewporter_interface, version > 1 ? 1 : version);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t id)
{
	(void)data;
	(void)registry;
	(void)id;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove,
};

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	.ping = xdg_wm_base_ping,
};

static void
decoration_configure(void *data,
		struct zxdg_toplevel_decoration_v1 *decoration, uint32_t mode)
{
	struct viewporter_test_client *client = data;

	(void)decoration;
	if (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE)
		zxdg_toplevel_decoration_v1_set_mode(client->toplevel_decoration,
				ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
	.configure = decoration_configure,
};

static void
frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
	struct viewporter_test_client *client = data;

	(void)time;
	wl_callback_destroy(callback);
	client->frame_callback = NULL;
	render_frame(client);
}

static const struct wl_callback_listener frame_listener = {
	.done = frame_done,
};

static void
render_frame(struct viewporter_test_client *client)
{
	if (client->frame_index < FRAMES_SHOW_FULL_GRID) {
		/* Stage 1: keep the full 3x3 grid visible for a while. */
		wp_viewport_set_source(client->viewport,
				wl_fixed_from_int(0),
				wl_fixed_from_int(0),
				wl_fixed_from_int(BUFFER_WIDTH),
				wl_fixed_from_int(BUFFER_HEIGHT));
		wp_viewport_set_destination(client->viewport, WINDOW_WIDTH, WINDOW_HEIGHT);
	} else {
		uint32_t idx = ((client->frame_index - FRAMES_SHOW_FULL_GRID) / FRAMES_PER_CELL)
			% (GRID_COLS * GRID_ROWS);
		int col = idx % GRID_COLS;
		int row = idx / GRID_COLS;
		wl_fixed_t sx = wl_fixed_from_int(col * CELL_WIDTH);
		wl_fixed_t sy = wl_fixed_from_int(row * CELL_HEIGHT);
		wl_fixed_t sw = wl_fixed_from_int(CELL_WIDTH);
		wl_fixed_t sh = wl_fixed_from_int(CELL_HEIGHT);

		wp_viewport_set_source(client->viewport, sx, sy, sw, sh);
		wp_viewport_set_destination(client->viewport, WINDOW_WIDTH, WINDOW_HEIGHT);
	}

	wl_surface_attach(client->surface, client->buffer, 0, 0);
	wl_surface_damage_buffer(client->surface, 0, 0, BUFFER_WIDTH, BUFFER_HEIGHT);
	request_next_frame(client);
	wl_surface_commit(client->surface);

	client->frame_index++;
}

static void
request_next_frame(struct viewporter_test_client *client)
{
	if (client->frame_callback) {
		wl_callback_destroy(client->frame_callback);
		client->frame_callback = NULL;
	}

	client->frame_callback = wl_surface_frame(client->surface);
	wl_callback_add_listener(client->frame_callback, &frame_listener, client);
}

static void
xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
	struct viewporter_test_client *client = data;

	xdg_surface_ack_configure(xdg_surface, serial);

	if (!client->configured) {
		client->configured = true;
		render_frame(client);
		printf("Window configured: show full grid for %d frames, then per-cell zoom animation\n",
				FRAMES_SHOW_FULL_GRID);
	}
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure,
};

static void
xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
		int32_t width, int32_t height, struct wl_array *states)
{
	(void)data;
	(void)xdg_toplevel;
	(void)width;
	(void)height;
	(void)states;
}

static void
xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	struct viewporter_test_client *client = data;

	(void)xdg_toplevel;
	client->running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_configure,
	.close = xdg_toplevel_close,
};

int
main(int argc, char *argv[])
{
	struct viewporter_test_client client = {0};

	(void)argc;
	(void)argv;

	printf("Viewporter test: full 3x3 grid for %d frames, then animated per-cell zoom\n",
			FRAMES_SHOW_FULL_GRID);

	client.display = wl_display_connect(NULL);
	if (!client.display) {
		fprintf(stderr, "Failed to connect to Wayland display\n");
		return 1;
	}

	client.registry = wl_display_get_registry(client.display);
	wl_registry_add_listener(client.registry, &registry_listener, &client);
	wl_display_roundtrip(client.display);

	if (!client.compositor || !client.shm || !client.xdg_wm_base || !client.viewporter) {
		fprintf(stderr, "Required globals missing (compositor/shm/xdg_wm_base/wp_viewporter)\n");
		goto cleanup;
	}

	xdg_wm_base_add_listener(client.xdg_wm_base, &xdg_wm_base_listener, &client);

	client.surface = wl_compositor_create_surface(client.compositor);
	if (!client.surface) {
		fprintf(stderr, "Failed to create wl_surface\n");
		goto cleanup;
	}

	client.viewport = wp_viewporter_get_viewport(client.viewporter, client.surface);
	if (!client.viewport) {
		fprintf(stderr, "Failed to create wp_viewport\n");
		goto cleanup;
	}

	client.xdg_surface = xdg_wm_base_get_xdg_surface(client.xdg_wm_base, client.surface);
	if (!client.xdg_surface) {
		fprintf(stderr, "Failed to create xdg_surface\n");
		goto cleanup;
	}
	xdg_surface_add_listener(client.xdg_surface, &xdg_surface_listener, &client);

	client.xdg_toplevel = xdg_surface_get_toplevel(client.xdg_surface);
	if (!client.xdg_toplevel) {
		fprintf(stderr, "Failed to create xdg_toplevel\n");
		goto cleanup;
	}
	xdg_toplevel_add_listener(client.xdg_toplevel, &xdg_toplevel_listener, &client);
	xdg_toplevel_set_title(client.xdg_toplevel, "Viewporter Nine Grid Test");
	xdg_toplevel_set_app_id(client.xdg_toplevel, "viewporter-test");

	if (client.decoration_manager) {
		client.toplevel_decoration =
			zxdg_decoration_manager_v1_get_toplevel_decoration(
					client.decoration_manager, client.xdg_toplevel);
		if (client.toplevel_decoration) {
			zxdg_toplevel_decoration_v1_add_listener(
					client.toplevel_decoration, &decoration_listener, &client);
			zxdg_toplevel_decoration_v1_set_mode(client.toplevel_decoration,
					ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
			printf("Requested server-side decorations\n");
		}
	}

	client.buffer = create_buffer(&client);
	if (!client.buffer) {
		fprintf(stderr, "Failed to create SHM buffer\n");
		goto cleanup;
	}

	wl_surface_commit(client.surface);
	client.running = true;

	while (client.running) {
		if (wl_display_dispatch(client.display) == -1)
			break;
	}

cleanup:
	if (client.frame_callback)
		wl_callback_destroy(client.frame_callback);
	if (client.toplevel_decoration)
		zxdg_toplevel_decoration_v1_destroy(client.toplevel_decoration);
	if (client.buffer)
		wl_buffer_destroy(client.buffer);
	if (client.shm_data)
		munmap(client.shm_data, client.shm_size);
	if (client.viewport)
		wp_viewport_destroy(client.viewport);
	if (client.xdg_toplevel)
		xdg_toplevel_destroy(client.xdg_toplevel);
	if (client.xdg_surface)
		xdg_surface_destroy(client.xdg_surface);
	if (client.surface)
		wl_surface_destroy(client.surface);
	if (client.xdg_wm_base)
		xdg_wm_base_destroy(client.xdg_wm_base);
	if (client.decoration_manager)
		zxdg_decoration_manager_v1_destroy(client.decoration_manager);
	if (client.viewporter)
		wp_viewporter_destroy(client.viewporter);
	if (client.shm)
		wl_shm_destroy(client.shm);
	if (client.compositor)
		wl_compositor_destroy(client.compositor);
	if (client.registry)
		wl_registry_destroy(client.registry);
	if (client.display)
		wl_display_disconnect(client.display);

	return 0;
}
