#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

/* globals */
static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_subcompositor *subcompositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm_base;

/* parent */
static struct wl_surface *parent_surface;
static struct xdg_surface *xdg_surface;
static struct xdg_toplevel *toplevel;

/* child */
static struct wl_surface *sub_surface;
static struct wl_subsurface *subsurface;

/* size */
static int parent_width  = 1920;
static int parent_height = 1080;
static int sub_width     = 300;
static int sub_height    = 150;

/* ---------- shm helpers ---------- */

static int
create_shm_file(size_t size)
{
    char name[] = "/wlshm-XXXXXX";
    int fd = memfd_create(name, MFD_CLOEXEC);
    if (fd < 0)
        return -1;

    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static struct wl_buffer *
create_red_buffer(void)
{
    size_t stride = sub_width * 4;
    size_t size   = stride * sub_height;

    int fd = create_shm_file(size);
    if (fd < 0)
        return NULL;

    uint32_t *data = mmap(NULL, size,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, 0);

    /* 填充 ARGB8888：A=255, R=255 */
    for (int i = 0; i < sub_width * sub_height; i++)
        data[i] = 0xFFFF0000;

    struct wl_shm_pool *pool =
        wl_shm_create_pool(shm, fd, size);

    struct wl_buffer *buffer =
        wl_shm_pool_create_buffer(pool,
            0,
            sub_width,
            sub_height,
            stride,
            WL_SHM_FORMAT_ARGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);

    return buffer;
}

/* ---------- xdg ---------- */

static void
xdg_surface_configure(void *data,
                      struct xdg_surface *xdg_surface,
                      uint32_t serial)
{
    xdg_surface_ack_configure(xdg_surface, serial);

    /* parent：不 attach buffer，保持透明 */
    wl_surface_commit(parent_surface);

    /* subsurface 底部居中 */
    int x = (parent_width  - sub_width) / 2;
    int y = parent_height - sub_height;

    wl_subsurface_set_position(subsurface, x, y);

    static struct wl_buffer *red = NULL;
    if (!red)
        red = create_red_buffer();

    wl_surface_attach(sub_surface, red, 0, 0);
    wl_surface_damage(sub_surface, 0, 0, sub_width, sub_height);
    wl_surface_commit(sub_surface);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void
wm_base_ping(void *data,
             struct xdg_wm_base *wm_base,
             uint32_t serial)
{
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

/* ---------- registry ---------- */

static void
registry_global(void *data, struct wl_registry *registry,
                uint32_t name, const char *interface,
                uint32_t version)
{
    if (strcmp(interface, wl_compositor_interface.name) == 0)
        compositor = wl_registry_bind(registry, name,
                                      &wl_compositor_interface, 4);
    else if (strcmp(interface, wl_subcompositor_interface.name) == 0)
        subcompositor = wl_registry_bind(registry, name,
                                         &wl_subcompositor_interface, 1);
    else if (strcmp(interface, wl_shm_interface.name) == 0)
        shm = wl_registry_bind(registry, name,
                               &wl_shm_interface, 1);
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        wm_base = wl_registry_bind(registry, name,
                                   &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(wm_base,
                                 &wm_base_listener, NULL);
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
};

/* ---------- main ---------- */

int main(void)
{
    display = wl_display_connect(NULL);
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry,
                             &registry_listener, NULL);
    wl_display_roundtrip(display);

    /* parent */
    parent_surface = wl_compositor_create_surface(compositor);
    xdg_surface = xdg_wm_base_get_xdg_surface(wm_base,
                                              parent_surface);
    xdg_surface_add_listener(xdg_surface,
                             &xdg_surface_listener, NULL);

    toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_set_fullscreen(toplevel, NULL);

    wl_surface_commit(parent_surface);

    /* subsurface */
    sub_surface = wl_compositor_create_surface(compositor);
    subsurface = wl_subcompositor_get_subsurface(
        subcompositor,
        sub_surface,
        parent_surface);

    wl_subsurface_set_sync(subsurface);

    while (wl_display_dispatch(display) != -1) {
    }

    return 0;
}
