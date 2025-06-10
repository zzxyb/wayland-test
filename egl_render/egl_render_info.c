#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <wayland-egl.h>

#include <EGL/egl.h>

int main() {
	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "Can't connect to display\n");
		exit(EXIT_FAILURE);
	}

	printf("connected to display\n");

	EGLint major, minor, count, n, size;
	EGLConfig *configs;
	// EGLConfig egl_conf;
	EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		//EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	EGLDisplay egl_display = eglGetDisplay((EGLNativeDisplayType) display);
	if (egl_display == EGL_NO_DISPLAY) {
		fprintf(stderr, "Can't create egl display\n");
		exit(EXIT_FAILURE);
	} else {
		printf("Created egl display\n");
	}

	if (eglInitialize(egl_display, &major, &minor) != EGL_TRUE) {
		fprintf(stderr, "Can't initialise egl display\n");
		exit(EXIT_FAILURE);
	}

	printf("EGL major: %d, minor %d\n", major, minor);
	eglGetConfigs(egl_display, NULL, 0, &count);
	printf("EGL has %d configs\n", count);

	configs = calloc(count, sizeof *configs);
	eglChooseConfig(egl_display, config_attribs,
			  configs, count, &n);

	for (int i = 0; i < n; i++) {
		eglGetConfigAttrib(egl_display,
				configs[i], EGL_BUFFER_SIZE, &size);
		printf("Buffer size for config %d is %d\n", i, size);
		eglGetConfigAttrib(egl_display,
				configs[i], EGL_RED_SIZE, &size);
		printf("Red size for config %d is %d\n", i, size);

		// egl_conf = configs[i];
		break;
	}

	fprintf(stderr, "eglQueryString %s\n", eglQueryString(egl_display, EGL_CLIENT_APIS));

	/*const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	EGLContext egl_context = eglCreateContext(egl_display, egl_conf,
		EGL_NO_CONTEXT, context_attribs);

	eglDestroyContext(egl_display, egl_context);*/
	free(configs);
	wl_display_disconnect(display);
	printf("disconnected from display\n");

	exit(EXIT_SUCCESS);
}
