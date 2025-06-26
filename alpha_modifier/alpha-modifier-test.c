#include "alpha-modifier-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"

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

#define WINDOW_WIDTH 400
#define WINDOW_HEIGHT 300

struct alpha_test_client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *xdg_wm_base;
	struct zxdg_decoration_manager_v1 *decoration_manager;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct zxdg_toplevel_decoration_v1 *toplevel_decoration;
	struct wl_buffer *buffer;
	struct wp_alpha_modifier_v1 *alpha_manager;
	struct wp_alpha_modifier_surface_v1 *alpha_surface;
	void *shm_data;
	size_t shm_size;
	bool running;
	bool configured;
	bool alpha_applied;
	int32_t width, height;
};

static void
registry_handle_global(void *data, struct wl_registry *registry,
                      uint32_t id, const char *interface, uint32_t version)
{
	struct alpha_test_client *client = data;

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
	} else if (strcmp(interface, "zxdg_decoration_manager_v1") == 0) {
		client->decoration_manager = wl_registry_bind(registry, id,
		                                            &zxdg_decoration_manager_v1_interface, 1);
		printf("Bound to zxdg_decoration_manager_v1\n");
	} else if (strcmp(interface, "wp_alpha_modifier_v1") == 0) {
		client->alpha_manager = wl_registry_bind(registry, id,
		                                       &wp_alpha_modifier_v1_interface, 1);
		printf("Bound to wp_alpha_modifier_v1\n");
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
	static const char template[] = "/alpha-test-XXXXXX";
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
create_buffer(struct alpha_test_client *client)
{
	struct wl_shm_pool *pool;
	int stride = WINDOW_WIDTH * 4; // 4 bytes per pixel (ARGB)
	int size = stride * WINDOW_HEIGHT;
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

	client->shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (client->shm_data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %s\n", strerror(errno));
		close(fd);
		return NULL;
	}
	client->shm_size = size;

	pool = wl_shm_create_pool(client->shm, fd, size);
	buffer = wl_shm_pool_create_buffer(pool, 0, WINDOW_WIDTH, WINDOW_HEIGHT,
	                                  stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	// Fill the buffer with blue color (ARGB: 0xFF0000FF)
	data = client->shm_data;
	for (i = 0; i < WINDOW_WIDTH * WINDOW_HEIGHT; ++i) {
		data[i] = 0xFF0000FF; // Blue with full alpha
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
xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
	struct alpha_test_client *client = data;

	xdg_surface_ack_configure(xdg_surface, serial);

	if (!client->configured) {
		client->configured = true;

		// Attach buffer to surface after configure
		wl_surface_attach(client->surface, client->buffer, 0, 0);
		wl_surface_damage(client->surface, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
		wl_surface_commit(client->surface);

		printf("Window configured and buffer attached - you should see a blue window\n");
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
	struct alpha_test_client *client = data;
	client->running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_configure,
	.close = xdg_toplevel_close,
};

static void
decoration_configure(void *data, struct zxdg_toplevel_decoration_v1 *decoration,
                     uint32_t mode)
{
	printf("Decoration configured with mode: %u\n", mode);
	if (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) {
		printf("Server-side decorations enabled\n");
	} else if (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE) {
		printf("Client-side decorations enabled\n");
	}
}

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
	.configure = decoration_configure,
};

static void
set_alpha_05(struct alpha_test_client *client)
{
	// Set alpha to 0.5 (50% transparency)
	uint32_t alpha_value = UINT32_MAX / 2; // 50% of maximum value

	printf("Setting surface alpha to 0.5 (50%% transparency)\n");
	wp_alpha_modifier_surface_v1_set_multiplier(client->alpha_surface, alpha_value);
	wl_surface_commit(client->surface);
	wl_display_roundtrip(client->display);

	client->alpha_applied = true;
	printf("Alpha set to 0.5 - window is now semi-transparent blue\n");
}

int main(int argc, char *argv[])
{
	struct alpha_test_client client = {0};

	printf("Alpha modifier test - Blue window with 0.5 alpha\n");

	client.display = wl_display_connect(NULL);
	if (!client.display) {
		fprintf(stderr, "Failed to connect to Wayland display\n");
		return 1;
	}

	printf("Connected to Wayland display\n");

	client.registry = wl_display_get_registry(client.display);
	wl_registry_add_listener(client.registry, &registry_listener, &client);

	// First roundtrip to get globals
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

	if (!client.alpha_manager) {
		fprintf(stderr, "Alpha modifier protocol not available\n");
		fprintf(stderr, "Make sure you're running a compositor that supports wp_alpha_modifier_v1\n");
		goto cleanup;
	}

	// Create a surface
	client.surface = wl_compositor_create_surface(client.compositor);
	if (!client.surface) {
		fprintf(stderr, "Failed to create surface\n");
		goto cleanup;
	}

	printf("Created surface\n");

	// Set up xdg_wm_base listener
	xdg_wm_base_add_listener(client.xdg_wm_base, &xdg_wm_base_listener, &client);

	// Create XDG surface
	client.xdg_surface = xdg_wm_base_get_xdg_surface(client.xdg_wm_base, client.surface);
	if (!client.xdg_surface) {
		fprintf(stderr, "Failed to create XDG surface\n");
		goto cleanup;
	}

	xdg_surface_add_listener(client.xdg_surface, &xdg_surface_listener, &client);

	// Create XDG toplevel
	client.xdg_toplevel = xdg_surface_get_toplevel(client.xdg_surface);
	if (!client.xdg_toplevel) {
		fprintf(stderr, "Failed to create XDG toplevel\n");
		goto cleanup;
	}

	xdg_toplevel_add_listener(client.xdg_toplevel, &xdg_toplevel_listener, &client);
	xdg_toplevel_set_title(client.xdg_toplevel, "Alpha Modifier Test");
	xdg_toplevel_set_app_id(client.xdg_toplevel, "alpha-modifier-test");

	printf("Created XDG shell surfaces\n");

	// Create window decoration (request server-side decorations)
	if (client.decoration_manager) {
		client.toplevel_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
			client.decoration_manager, client.xdg_toplevel);
		zxdg_toplevel_decoration_v1_add_listener(client.toplevel_decoration,
		                                        &decoration_listener, &client);
		zxdg_toplevel_decoration_v1_set_mode(client.toplevel_decoration,
		                                    ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
		printf("Requested server-side decorations\n");
	} else {
		printf("Decoration manager not available - window will use client-side decorations\n");
	}

	// Commit the surface to trigger the first configure event
	wl_surface_commit(client.surface);

	// Create buffer with blue content
	client.buffer = create_buffer(&client);
	if (!client.buffer) {
		fprintf(stderr, "Failed to create buffer\n");
		goto cleanup;
	}

	printf("Created blue buffer (%dx%d)\n", WINDOW_WIDTH, WINDOW_HEIGHT);

	// Create alpha modifier for the surface
	client.alpha_surface = wp_alpha_modifier_v1_get_surface(
		client.alpha_manager, client.surface);

	if (!client.alpha_surface) {
		fprintf(stderr, "Failed to create alpha modifier surface\n");
		goto cleanup;
	}

	printf("Created alpha modifier surface\n");

	// Wait for everything to be set up
	wl_display_roundtrip(client.display);

	// Keep the window open and wait for configure
	client.running = true;
	printf("Waiting for window to appear...\n");

	// Process events until window is configured
	while (client.running && !client.configured) {
		if (wl_display_dispatch(client.display) == -1) {
			break;
		}
	}

	if (client.configured && !client.alpha_applied) {
		printf("Window is now visible, waiting 2 seconds before applying alpha...\n");
		sleep(2);

		// Set alpha to 0.5
		set_alpha_05(&client);
	}

	printf("Test completed! Window will remain visible. Press Ctrl+C to exit.\n");

	// Keep the window open until user exits
	while (client.running) {
		if (wl_display_dispatch(client.display) == -1) {
			break;
		}
	}

cleanup:
	if (client.toplevel_decoration)
		zxdg_toplevel_decoration_v1_destroy(client.toplevel_decoration);
	if (client.decoration_manager)
		zxdg_decoration_manager_v1_destroy(client.decoration_manager);
	if (client.alpha_surface)
		wp_alpha_modifier_surface_v1_destroy(client.alpha_surface);
	if (client.alpha_manager)
		wp_alpha_modifier_v1_destroy(client.alpha_manager);
	if (client.buffer)
		wl_buffer_destroy(client.buffer);
	if (client.shm_data)
		munmap(client.shm_data, client.shm_size);
	if (client.xdg_toplevel)
		xdg_toplevel_destroy(client.xdg_toplevel);
	if (client.xdg_surface)
		xdg_surface_destroy(client.xdg_surface);
	if (client.xdg_wm_base)
		xdg_wm_base_destroy(client.xdg_wm_base);
	if (client.surface)
		wl_surface_destroy(client.surface);
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
