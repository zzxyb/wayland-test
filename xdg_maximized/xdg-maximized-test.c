#include "xdg-shell-client-protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

#define FALLBACK_WIDTH 800
#define FALLBACK_HEIGHT 600

struct maximized_client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *xdg_wm_base;

	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct wl_buffer *buffer;

	void *shm_data;
	size_t shm_size;
	int width;
	int height;
	bool configured;
	bool maximized;
	bool running;
};

static void
destroy_buffer(struct maximized_client *client)
{
	if (client->buffer) {
		wl_buffer_destroy(client->buffer);
		client->buffer = NULL;
	}

	if (client->shm_data) {
		munmap(client->shm_data, client->shm_size);
		client->shm_data = NULL;
		client->shm_size = 0;
	}
}

static int
create_shm_file(size_t size)
{
	static const char template[] = "/xdg-maximized-test-XXXXXX";
	const char *runtime_dir;
	char *name;
	int fd;

	runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) {
		errno = ENOENT;
		return -1;
	}

	name = malloc(strlen(runtime_dir) + sizeof(template));
	if (!name)
		return -1;

	strcpy(name, runtime_dir);
	strcat(name, template);

	fd = mkstemp(name);
	if (fd >= 0) {
		unlink(name);
		fcntl(fd, F_SETFD, FD_CLOEXEC);
	}
	free(name);

	if (fd < 0)
		return -1;

	if (ftruncate(fd, size) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

static struct wl_buffer *
create_buffer(struct maximized_client *client, int width, int height)
{
	struct wl_shm_pool *pool;
	struct wl_buffer *buffer;
	uint32_t *pixels;
	int stride = width * 4;
	int size = stride * height;
	int fd;

	fd = create_shm_file(size);
	if (fd < 0) {
		fprintf(stderr, "creating shm file failed: %s\n", strerror(errno));
		return NULL;
	}

	client->shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (client->shm_data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %s\n", strerror(errno));
		client->shm_data = NULL;
		close(fd);
		return NULL;
	}
	client->shm_size = size;

	pool = wl_shm_create_pool(client->shm, fd, size);
	buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
	                                  WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	pixels = client->shm_data;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			bool stripe = ((x / 32) + (y / 32)) % 2 == 0;
			bool border = x < 12 || y < 12 || x >= width - 12 || y >= height - 12;

			if (border)
				pixels[y * width + x] = 0xff2f6fed;
			else if (stripe)
				pixels[y * width + x] = 0xff1f2937;
			else
				pixels[y * width + x] = 0xff111827;
		}
	}

	return buffer;
}

static void
redraw(struct maximized_client *client)
{
	destroy_buffer(client);

	client->buffer = create_buffer(client, client->width, client->height);
	if (!client->buffer) {
		client->running = false;
		return;
	}

	wl_surface_attach(client->surface, client->buffer, 0, 0);
	wl_surface_damage_buffer(client->surface, 0, 0, client->width, client->height);
	wl_surface_commit(client->surface);
}

static void
registry_handle_global(void *data, struct wl_registry *registry,
                       uint32_t id, const char *interface, uint32_t version)
{
	struct maximized_client *client = data;

	printf("global: %s v%u\n", interface, version);

	if (strcmp(interface, "wl_compositor") == 0) {
		client->compositor = wl_registry_bind(registry, id,
		                                      &wl_compositor_interface, 4);
	} else if (strcmp(interface, "wl_shm") == 0) {
		client->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
	} else if (strcmp(interface, "xdg_wm_base") == 0) {
		client->xdg_wm_base = wl_registry_bind(registry, id,
		                                       &xdg_wm_base_interface, 1);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id)
{
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove,
};

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
	xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	.ping = xdg_wm_base_ping,
};

static void
xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                       int32_t width, int32_t height, struct wl_array *states)
{
	struct maximized_client *client = data;
	uint32_t *state;

	client->width = width > 0 ? width : FALLBACK_WIDTH;
	client->height = height > 0 ? height : FALLBACK_HEIGHT;
	client->maximized = false;

	wl_array_for_each(state, states) {
		if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED)
			client->maximized = true;
	}

	printf("configure: %dx%d, maximized=%s\n",
	       client->width, client->height, client->maximized ? "true" : "false");
}

static void
xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	struct maximized_client *client = data;

	client->running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_configure,
	.close = xdg_toplevel_close,
};

static void
xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
	struct maximized_client *client = data;

	xdg_surface_ack_configure(xdg_surface, serial);
	client->configured = true;
	redraw(client);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure,
};

static bool
init_client(struct maximized_client *client)
{
	client->display = wl_display_connect(NULL);
	if (!client->display) {
		fprintf(stderr, "failed to connect to Wayland display\n");
		return false;
	}

	client->registry = wl_display_get_registry(client->display);
	wl_registry_add_listener(client->registry, &registry_listener, client);
	wl_display_roundtrip(client->display);

	if (!client->compositor || !client->shm || !client->xdg_wm_base) {
		fprintf(stderr, "required globals: wl_compositor=%s wl_shm=%s xdg_wm_base=%s\n",
		        client->compositor ? "yes" : "no",
		        client->shm ? "yes" : "no",
		        client->xdg_wm_base ? "yes" : "no");
		return false;
	}

	xdg_wm_base_add_listener(client->xdg_wm_base, &xdg_wm_base_listener, client);
	return true;
}

static bool
create_window(struct maximized_client *client)
{
	client->width = FALLBACK_WIDTH;
	client->height = FALLBACK_HEIGHT;

	client->surface = wl_compositor_create_surface(client->compositor);
	client->xdg_surface = xdg_wm_base_get_xdg_surface(client->xdg_wm_base,
	                                                  client->surface);
	xdg_surface_add_listener(client->xdg_surface, &xdg_surface_listener, client);

	client->xdg_toplevel = xdg_surface_get_toplevel(client->xdg_surface);
	xdg_toplevel_add_listener(client->xdg_toplevel, &xdg_toplevel_listener, client);
	xdg_toplevel_set_title(client->xdg_toplevel, "XDG Maximized Test");
	xdg_toplevel_set_app_id(client->xdg_toplevel, "xdg-maximized-test");

	xdg_toplevel_set_maximized(client->xdg_toplevel);
	wl_surface_commit(client->surface);

	return true;
}

static void
cleanup_client(struct maximized_client *client)
{
	destroy_buffer(client);

	if (client->xdg_toplevel)
		xdg_toplevel_destroy(client->xdg_toplevel);
	if (client->xdg_surface)
		xdg_surface_destroy(client->xdg_surface);
	if (client->surface)
		wl_surface_destroy(client->surface);
	if (client->xdg_wm_base)
		xdg_wm_base_destroy(client->xdg_wm_base);
	if (client->shm)
		wl_shm_destroy(client->shm);
	if (client->compositor)
		wl_compositor_destroy(client->compositor);
	if (client->registry)
		wl_registry_destroy(client->registry);
	if (client->display)
		wl_display_disconnect(client->display);
}

int
main(int argc, char *argv[])
{
	struct maximized_client client = {
		.running = true,
	};

	printf("XDG toplevel default maximized test\n");

	if (!init_client(&client))
		goto out;

	if (!create_window(&client))
		goto out;

	while (client.running) {
		if (wl_display_dispatch(client.display) < 0)
			break;
	}

out:
	cleanup_client(&client);
	return 0;
}
