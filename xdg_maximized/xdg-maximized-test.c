#include "xdg-shell-client-protocol.h"

#include <cairo/cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
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
#define TITLEBAR_HEIGHT 42
#define TITLEBAR_BUTTON_WIDTH 52
#define TITLEBAR_BUTTON_MARGIN 4

struct maximized_client {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct xdg_wm_base *xdg_wm_base;

	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct wl_buffer *buffer;

	void *shm_data;
	size_t shm_size;
	int width;
	int height;
	wl_fixed_t pointer_x;
	wl_fixed_t pointer_y;
	bool configured;
	bool maximized;
	bool pointer_on_surface;
	bool maximize_button_pressed;
	bool running;
};

static bool
point_in_maximize_button(const struct maximized_client *client,
                         wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	double x = wl_fixed_to_double(surface_x);
	double y = wl_fixed_to_double(surface_y);
	int left = client->width - TITLEBAR_BUTTON_WIDTH - TITLEBAR_BUTTON_MARGIN;

	return x >= left && x < client->width - TITLEBAR_BUTTON_MARGIN &&
	       y >= TITLEBAR_BUTTON_MARGIN &&
	       y < TITLEBAR_HEIGHT - TITLEBAR_BUTTON_MARGIN;
}

static void
draw_restore_icon(cairo_t *cr, double x, double y)
{
	cairo_rectangle(cr, x + 3.5, y + 0.5, 10.0, 8.0);
	cairo_stroke(cr);
	cairo_rectangle(cr, x + 0.5, y + 3.5, 10.0, 8.0);
	cairo_set_source_rgb(cr, 0.16, 0.19, 0.24);
	cairo_fill_preserve(cr);
	cairo_set_source_rgb(cr, 0.93, 0.95, 0.98);
	cairo_stroke(cr);
}

static void
draw_window(struct maximized_client *client, unsigned char *data, int stride)
{
	cairo_surface_t *image;
	cairo_t *cr;
	cairo_text_extents_t extents;
	const char *title = "XDG Maximized Test";
	double button_x = client->width - TITLEBAR_BUTTON_WIDTH - TITLEBAR_BUTTON_MARGIN;
	double button_width = TITLEBAR_BUTTON_WIDTH;

	image = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_RGB24,
	                                            client->width, client->height,
	                                            stride);
	cr = cairo_create(image);

	/* Application content. */
	cairo_set_source_rgb(cr, 0.07, 0.09, 0.13);
	cairo_paint(cr);
	for (int y = TITLEBAR_HEIGHT; y < client->height; y += 32) {
		for (int x = 0; x < client->width; x += 32) {
			if (((x / 32) + (y / 32)) % 2 == 0)
				cairo_rectangle(cr, x, y, 32, 32);
		}
	}
	cairo_set_source_rgb(cr, 0.12, 0.16, 0.22);
	cairo_fill(cr);

	/* Client-side titlebar. */
	cairo_rectangle(cr, 0, 0, client->width, TITLEBAR_HEIGHT);
	cairo_set_source_rgb(cr, 0.10, 0.12, 0.16);
	cairo_fill(cr);
	cairo_rectangle(cr, 0, TITLEBAR_HEIGHT - 1, client->width, 1);
	cairo_set_source_rgb(cr, 0.20, 0.25, 0.33);
	cairo_fill(cr);

	cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL,
	                       CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 14.0);
	cairo_text_extents(cr, title, &extents);
	cairo_move_to(cr, 16.0,
	              (TITLEBAR_HEIGHT - extents.height) / 2.0 - extents.y_bearing);
	cairo_set_source_rgb(cr, 0.93, 0.95, 0.98);
	cairo_show_text(cr, title);

	/* Maximize/restore button. */
	cairo_rectangle(cr, button_x, TITLEBAR_BUTTON_MARGIN, button_width,
	                TITLEBAR_HEIGHT - 2 * TITLEBAR_BUTTON_MARGIN);
	cairo_set_source_rgb(cr, 0.16, 0.19, 0.24);
	cairo_fill(cr);
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgb(cr, 0.93, 0.95, 0.98);
	if (client->maximized) {
		draw_restore_icon(cr, button_x + (button_width - 14.0) / 2.0,
		                  (TITLEBAR_HEIGHT - 12.0) / 2.0);
	} else {
		cairo_rectangle(cr, button_x + (button_width - 13.0) / 2.0 + 0.5,
		                (TITLEBAR_HEIGHT - 11.0) / 2.0 + 0.5, 12.0, 10.0);
		cairo_stroke(cr);
	}

	cairo_destroy(cr);
	cairo_surface_destroy(image);
}

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

	draw_window(client, client->shm_data, stride);

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
	} else if (strcmp(interface, "wl_seat") == 0) {
		client->seat = wl_registry_bind(registry, id, &wl_seat_interface,
		                                version < 5 ? version : 5);
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
pointer_handle_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                     struct wl_surface *surface, wl_fixed_t surface_x,
                     wl_fixed_t surface_y)
{
	struct maximized_client *client = data;

	client->pointer_on_surface = surface == client->surface;
	client->pointer_x = surface_x;
	client->pointer_y = surface_y;
}

static void
pointer_handle_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                     struct wl_surface *surface)
{
	struct maximized_client *client = data;

	client->pointer_on_surface = false;
	client->maximize_button_pressed = false;
}

static void
pointer_handle_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                      wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	struct maximized_client *client = data;

	client->pointer_x = surface_x;
	client->pointer_y = surface_y;
}

static void
pointer_handle_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                      uint32_t time, uint32_t button, uint32_t state)
{
	struct maximized_client *client = data;

	if (button != BTN_LEFT)
		return;

	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		client->maximize_button_pressed = client->pointer_on_surface &&
			point_in_maximize_button(client, client->pointer_x,
			                         client->pointer_y);
		return;
	}

	if (!client->maximize_button_pressed)
		return;

	client->maximize_button_pressed = false;
	if (!client->pointer_on_surface ||
	    !point_in_maximize_button(client, client->pointer_x, client->pointer_y))
		return;

	if (client->maximized)
		xdg_toplevel_unset_maximized(client->xdg_toplevel);
	else
		xdg_toplevel_set_maximized(client->xdg_toplevel);
}

static void
pointer_handle_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                    uint32_t axis, wl_fixed_t value)
{
}

static void
pointer_handle_frame(void *data, struct wl_pointer *pointer)
{
}

static void
pointer_handle_axis_source(void *data, struct wl_pointer *pointer,
                           uint32_t axis_source)
{
}

static void
pointer_handle_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis)
{
}

static void
pointer_handle_axis_discrete(void *data, struct wl_pointer *pointer,
                             uint32_t axis, int32_t discrete)
{
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_handle_enter,
	.leave = pointer_handle_leave,
	.motion = pointer_handle_motion,
	.button = pointer_handle_button,
	.axis = pointer_handle_axis,
	.frame = pointer_handle_frame,
	.axis_source = pointer_handle_axis_source,
	.axis_stop = pointer_handle_axis_stop,
	.axis_discrete = pointer_handle_axis_discrete,
};

static void
seat_handle_capabilities(void *data, struct wl_seat *seat,
                         uint32_t capabilities)
{
	struct maximized_client *client = data;

	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !client->pointer) {
		client->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(client->pointer, &pointer_listener, client);
	} else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && client->pointer) {
		if (wl_proxy_get_version((struct wl_proxy *)client->pointer) >= 3)
			wl_pointer_release(client->pointer);
		else
			wl_pointer_destroy(client->pointer);
		client->pointer = NULL;
	}
}

static void
seat_handle_name(void *data, struct wl_seat *seat, const char *name)
{
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
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
	if (client->seat)
		wl_seat_add_listener(client->seat, &seat_listener, client);
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

	if (client->pointer) {
		if (wl_proxy_get_version((struct wl_proxy *)client->pointer) >= 3)
			wl_pointer_release(client->pointer);
		else
			wl_pointer_destroy(client->pointer);
	}
	if (client->seat) {
		if (wl_proxy_get_version((struct wl_proxy *)client->seat) >= 5)
			wl_seat_release(client->seat);
		else
			wl_seat_destroy(client->seat);
	}
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
