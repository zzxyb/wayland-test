#include "xdg-shell-client-protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <wayland-client.h>

#define PARENT_WIDTH 600
#define PARENT_HEIGHT 400
#define CHILD_WIDTH 300
#define CHILD_HEIGHT 200

struct window {
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct wl_buffer *buffer;
	struct wl_callback *frame_callback;
	void *shm_data;
	size_t shm_size;
	int width, height;
	bool configured;
	bool frame_done;
};

struct set_parent_client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *xdg_wm_base;

	struct window parent;
	struct window child;

	bool running;
	bool parent_child_set;
};

static void
registry_handle_global(void *data, struct wl_registry *registry,
                      uint32_t id, const char *interface, uint32_t version)
{
	struct set_parent_client *client = data;

	printf("Available interface: %s (version %u)\n", interface, version);

	if (strcmp(interface, "wl_compositor") == 0) {
		client->compositor = wl_registry_bind(registry, id,
		                                    &wl_compositor_interface, 4);
		printf("Bound to wl_compositor\n");
	} else if (strcmp(interface, "wl_shm") == 0) {
		client->shm = wl_registry_bind(registry, id,
		                             &wl_shm_interface, 1);
		printf("Bound to wl_shm\n");
	} else if (strcmp(interface, "xdg_wm_base") == 0) {
		client->xdg_wm_base = wl_registry_bind(registry, id,
		                                     &xdg_wm_base_interface, 1);
		printf("Bound to xdg_wm_base\n");
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
                             uint32_t id)
{
	printf("Global removed: %u\n", id);
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove,
};

static int
create_shm_file(size_t size)
{
	static const char template[] = "/set-parent-test-XXXXXX";
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

static struct wl_buffer *
create_buffer(struct set_parent_client *client, struct window *window,
              int width, int height, uint32_t color)
{
	struct wl_shm_pool *pool;
	int stride = width * 4; // 4 bytes per pixel (ARGB)
	int size = stride * height;
	int fd;
	struct wl_buffer *buffer;
	uint32_t *data;
	int i;

	fd = create_shm_file(size);
	if (fd < 0) {
		fprintf(stderr, "Creating a shared memory file failed: %s\n",
		        strerror(errno));
		return NULL;
	}

	window->shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (window->shm_data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %s\n", strerror(errno));
		close(fd);
		return NULL;
	}
	window->shm_size = size;

	pool = wl_shm_create_pool(client->shm, fd, size);
	buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
	                                  stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	// Fill the buffer with specified color
	data = window->shm_data;
	for (i = 0; i < width * height; ++i) {
		data[i] = color;
	}

	return buffer;
}

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
	xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	.ping = xdg_wm_base_ping,
};

static void
frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
	struct window *window = data;

	wl_callback_destroy(callback);
	window->frame_callback = NULL;
	window->frame_done = true;
}

static const struct wl_callback_listener frame_listener = {
	.done = frame_done,
};

static void
xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
	struct window *window = data;

	xdg_surface_ack_configure(xdg_surface, serial);

	if (!window->configured) {
		window->configured = true;

		wl_surface_attach(window->surface, window->buffer, 0, 0);
		wl_surface_damage(window->surface, 0, 0, window->width, window->height);

		window->frame_callback = wl_surface_frame(window->surface);
		wl_callback_add_listener(window->frame_callback, &frame_listener, window);

		wl_surface_commit(window->surface);

		printf("Window configured (%dx%d)\n", window->width, window->height);
	}
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure,
};

static void
xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                      int32_t width, int32_t height, struct wl_array *states)
{
	// Handle resize if needed
}

static void
xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	struct set_parent_client *client = data;
	client->running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_configure,
	.close = xdg_toplevel_close,
};

static int
create_window(struct set_parent_client *client, struct window *window,
              const char *title, const char *app_id, int width, int height, uint32_t color)
{
	window->width = width;
	window->height = height;
	window->configured = false;
	window->frame_done = false;

	window->surface = wl_compositor_create_surface(client->compositor);
	if (!window->surface) {
		fprintf(stderr, "Failed to create surface for %s\n", title);
		return -1;
	}

	window->xdg_surface = xdg_wm_base_get_xdg_surface(client->xdg_wm_base, window->surface);
	if (!window->xdg_surface) {
		fprintf(stderr, "Failed to create XDG surface for %s\n", title);
		return -1;
	}
	xdg_surface_add_listener(window->xdg_surface, &xdg_surface_listener, window);

	window->xdg_toplevel = xdg_surface_get_toplevel(window->xdg_surface);
	if (!window->xdg_toplevel) {
		fprintf(stderr, "Failed to create XDG toplevel for %s\n", title);
		return -1;
	}

	xdg_toplevel_add_listener(window->xdg_toplevel, &xdg_toplevel_listener, client);
	xdg_toplevel_set_title(window->xdg_toplevel, title);
	xdg_toplevel_set_app_id(window->xdg_toplevel, app_id);

	window->buffer = create_buffer(client, window, width, height, color);
	if (!window->buffer) {
		fprintf(stderr, "Failed to create buffer for %s\n", title);
		return -1;
	}

	wl_surface_commit(window->surface);

	printf("Created window: %s (%dx%d)\n", title, width, height);
	return 0;
}

int main(int argc, char *argv[])
{
	struct set_parent_client client = {0};

	printf("XDG toplevel set_parent test\n");

	client.display = wl_display_connect(NULL);
	if (!client.display) {
		fprintf(stderr, "Failed to connect to Wayland display\n");
		return 1;
	}

	printf("Connected to Wayland display\n");

	client.registry = wl_display_get_registry(client.display);
	wl_registry_add_listener(client.registry, &registry_listener, &client);

	wl_display_roundtrip(client.display);

	if (!client.compositor) {
		fprintf(stderr, "Compositor not available\n");
		goto cleanup;
	}

	if (!client.shm) {
		fprintf(stderr, "Shared memory not available\n");
		goto cleanup;
	}

	if (!client.xdg_wm_base) {
		fprintf(stderr, "XDG shell not available\n");
		goto cleanup;
	}

	xdg_wm_base_add_listener(client.xdg_wm_base, &xdg_wm_base_listener, &client);

	// Create parent window (blue)
	if (create_window(&client, &client.parent, "Parent Window", "set-parent-test-parent",
	                  PARENT_WIDTH, PARENT_HEIGHT, 0xFF0000FF) < 0) {
		fprintf(stderr, "Failed to create parent window\n");
		goto cleanup;
	}

	// Create child window (red)
	if (create_window(&client, &client.child, "Child Window", "set-parent-test-child",
	                  CHILD_WIDTH, CHILD_HEIGHT, 0xFFFF0000) < 0) {
		fprintf(stderr, "Failed to create child window\n");
		goto cleanup;
	}

	wl_display_roundtrip(client.display);

	client.running = true;
	printf("Waiting for windows to appear and parent frame_done...\n");

	while (client.running && (!client.parent.frame_done || !client.child.configured)) {
		if (wl_display_dispatch(client.display) == -1) {
			break;
		}
	}

	if (client.parent.frame_done && !client.parent_child_set) {
		printf("Parent frame_done received! Setting child parent...\n");
		xdg_toplevel_set_parent(client.child.xdg_toplevel, client.parent.xdg_toplevel);
		wl_surface_commit(client.child.surface);
		wl_display_roundtrip(client.display);
		client.parent_child_set = true;
		printf("Parent-child relationship set! Child window is now transient for parent.\n");
	}

	printf("Test completed! Windows will remain visible. Press Ctrl+C to exit.\n");

	while (client.running) {
		if (wl_display_dispatch(client.display) == -1) {
			break;
		}
	}

cleanup:
	if (client.child.frame_callback)
		wl_callback_destroy(client.child.frame_callback);
	if (client.child.buffer)
		wl_buffer_destroy(client.child.buffer);
	if (client.child.shm_data)
		munmap(client.child.shm_data, client.child.shm_size);
	if (client.child.xdg_toplevel)
		xdg_toplevel_destroy(client.child.xdg_toplevel);
	if (client.child.xdg_surface)
		xdg_surface_destroy(client.child.xdg_surface);
	if (client.child.surface)
		wl_surface_destroy(client.child.surface);

	if (client.parent.frame_callback)
		wl_callback_destroy(client.parent.frame_callback);
	if (client.parent.buffer)
		wl_buffer_destroy(client.parent.buffer);
	if (client.parent.shm_data)
		munmap(client.parent.shm_data, client.parent.shm_size);
	if (client.parent.xdg_toplevel)
		xdg_toplevel_destroy(client.parent.xdg_toplevel);
	if (client.parent.xdg_surface)
		xdg_surface_destroy(client.parent.xdg_surface);
	if (client.parent.surface)
		wl_surface_destroy(client.parent.surface);

	if (client.xdg_wm_base)
		xdg_wm_base_destroy(client.xdg_wm_base);
	if (client.shm)
		wl_shm_destroy(client.shm);
	if (client.compositor)
		wl_compositor_destroy(client.compositor);
	if (client.registry)
		wl_registry_destroy(client.registry);
	if (client.display)
		wl_display_disconnect(client.display);

	printf("Client cleanup completed\n");
	return 0;
}
