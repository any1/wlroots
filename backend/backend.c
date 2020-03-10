#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <libinput.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend/drm.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/libinput.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/noop.h>
#include <wlr/backend/session.h>
#include <wlr/backend/wayland.h>
#include <wlr/config.h>
#include <wlr/util/log.h>
#include "backend/multi.h"

#if WLR_HAS_X11_BACKEND
#include <wlr/backend/x11.h>
#endif

enum {
	WLR_BACKEND_WAYLAND = 1UL << 0,
	WLR_BACKEND_X11 = 1UL << 1,
	WLR_BACKEND_HEADLESS = 1UL << 2,
	WLR_BACKEND_NOOP = 1UL << 3,
	WLR_BACKEND_LIBINPUT = 1UL << 4,
	WLR_BACKEND_DRM = 1UL << 5,
};

void wlr_backend_init(struct wlr_backend *backend,
		const struct wlr_backend_impl *impl) {
	assert(backend);
	backend->impl = impl;
	wl_signal_init(&backend->events.destroy);
	wl_signal_init(&backend->events.new_input);
	wl_signal_init(&backend->events.new_output);
}

bool wlr_backend_start(struct wlr_backend *backend) {
	if (backend->impl->start) {
		return backend->impl->start(backend);
	}
	return true;
}

void wlr_backend_destroy(struct wlr_backend *backend) {
	if (!backend) {
		return;
	}

	if (backend->impl && backend->impl->destroy) {
		backend->impl->destroy(backend);
	} else {
		free(backend);
	}
}

struct wlr_renderer *wlr_backend_get_renderer(struct wlr_backend *backend) {
	if (backend->impl->get_renderer) {
		return backend->impl->get_renderer(backend);
	}
	return NULL;
}

struct wlr_session *wlr_backend_get_session(struct wlr_backend *backend) {
	if (backend->impl->get_session) {
		return backend->impl->get_session(backend);
	}
	return NULL;
}

clockid_t wlr_backend_get_presentation_clock(struct wlr_backend *backend) {
	if (backend->impl->get_presentation_clock) {
		return backend->impl->get_presentation_clock(backend);
	}
	return CLOCK_MONOTONIC;
}

static size_t parse_outputs_env(const char *name) {
	const char *outputs_str = getenv(name);
	if (outputs_str == NULL) {
		return 1;
	}

	char *end;
	int outputs = (int)strtol(outputs_str, &end, 10);
	if (*end || outputs < 0) {
		wlr_log(WLR_ERROR, "%s specified with invalid integer, ignoring", name);
		return 1;
	}

	return outputs;
}

static int attempt_wl_backend(struct wlr_backend *parent,
		struct wl_display *display,
		wlr_renderer_create_func_t create_renderer_func) {
	struct wlr_backend *backend = wlr_wl_backend_create(display, NULL, create_renderer_func);
	if (backend == NULL) {
		return -1;
	}

	size_t outputs = parse_outputs_env("WLR_WL_OUTPUTS");
	for (size_t i = 0; i < outputs; ++i) {
		wlr_wl_output_create(backend);
	}

	if (!wlr_multi_backend_add(parent, backend)) {
		wlr_backend_destroy(backend);
		return -1;
	}

	return 0;
}

#if WLR_HAS_X11_BACKEND
static int attempt_x11_backend(struct wlr_backend *parent,
		struct wl_display *display, const char *x11_display,
		wlr_renderer_create_func_t create_renderer_func) {
	struct wlr_backend *backend = wlr_x11_backend_create(display, x11_display, create_renderer_func);
	if (backend == NULL) {
		return -1;
	}

	size_t outputs = parse_outputs_env("WLR_X11_OUTPUTS");
	for (size_t i = 0; i < outputs; ++i) {
		wlr_x11_output_create(backend);
	}

	if (!wlr_multi_backend_add(parent, backend)) {
		wlr_backend_destroy(backend);
		return -1;
	}

	return 0;
}
#endif

static int attempt_headless_backend(struct wlr_backend *parent,
		struct wl_display *display, wlr_renderer_create_func_t create_renderer_func) {
	struct wlr_backend *backend = wlr_headless_backend_create(display, create_renderer_func);
	if (backend == NULL) {
		return -1;
	}

	size_t outputs = parse_outputs_env("WLR_HEADLESS_OUTPUTS");
	for (size_t i = 0; i < outputs; ++i) {
		wlr_headless_add_output(backend, 1280, 720);
	}

	if (!wlr_multi_backend_add(parent, backend)) {
		wlr_backend_destroy(backend);
		return -1;
	}

	return 0;
}

static int attempt_noop_backend(struct wlr_backend *parent,
		struct wl_display *display) {
	struct wlr_backend *backend = wlr_noop_backend_create(display);
	if (backend == NULL) {
		return -1;
	}

	size_t outputs = parse_outputs_env("WLR_NOOP_OUTPUTS");
	for (size_t i = 0; i < outputs; ++i) {
		wlr_noop_add_output(backend);
	}

	if (!wlr_multi_backend_add(parent, backend)) {
		wlr_backend_destroy(backend);
		return -1;
	}

	return 0;
}

static int attempt_libinput_backend(struct wlr_backend *parent,
		struct wlr_session* session, struct wl_display *display) {
	struct wlr_backend *backend = wlr_libinput_backend_create(display,
		session);
	if (backend == NULL) {
		return -1;
	}

	if (!wlr_multi_backend_add(parent, backend)) {
		wlr_backend_destroy(backend);
		return -1;
	}

	return 0;
}

static int attempt_drm_backend(struct wl_display *display,
		struct wlr_backend *backend, struct wlr_session *session,
		wlr_renderer_create_func_t create_renderer_func) {
	int gpus[8];
	size_t num_gpus = wlr_session_find_gpus(session, 8, gpus);
	struct wlr_backend *primary_drm = NULL;
	wlr_log(WLR_INFO, "Found %zu GPUs", num_gpus);

	for (size_t i = 0; i < num_gpus; ++i) {
		struct wlr_backend *drm = wlr_drm_backend_create(display, session,
			gpus[i], primary_drm, create_renderer_func);
		if (!drm) {
			wlr_log(WLR_ERROR, "Failed to open DRM device %d", gpus[i]);
			continue;
		}

		if (!primary_drm) {
			primary_drm = drm;
		}

		wlr_multi_backend_add(backend, drm);
	}

	if (!primary_drm) {
		return -1;
	}

	if (!wlr_multi_backend_add(backend, primary_drm)) {
		wlr_backend_destroy(backend);
		return -1;
	}

	return 0;
}

static int32_t parse_backends(const char *str) {
	char names[256];
	strncpy(names, str, sizeof(names));
	names[sizeof(names) - 1] = '\0';

	uint32_t types = 0;

	char *saveptr, *name = strtok_r(names, ",", &saveptr);
	while (name) {
		if (strcmp(name, "wayland") == 0) {
			types |= WLR_BACKEND_WAYLAND;
		} else if (strcmp(name, "x11") == 0) {
			types |= WLR_BACKEND_X11;
		} else if (strcmp(name, "headless") == 0) {
			types |= WLR_BACKEND_HEADLESS;
		} else if (strcmp(name, "noop") == 0) {
			types |= WLR_BACKEND_NOOP;
		} else if (strcmp(name, "libinput") == 0) {
			types |= WLR_BACKEND_LIBINPUT;
		} else if (strcmp(name, "drm") == 0) {
			types |= WLR_BACKEND_DRM;
		} else {
			wlr_log(WLR_ERROR, "unrecognized backend '%s'", name);
			return -1;
		}

		name = strtok_r(NULL, ",", &saveptr);
	}

	return types;
}

struct wlr_backend *wlr_backend_autocreate(struct wl_display *display,
		wlr_renderer_create_func_t create_renderer_func) {
	struct wlr_backend *backend = wlr_multi_backend_create(display);
	struct wlr_multi_backend *multi = (struct wlr_multi_backend *)backend;
	if (!backend) {
		wlr_log(WLR_ERROR, "could not allocate multibackend");
		return NULL;
	}

	char *names = getenv("WLR_BACKENDS");
	if (names) {
		int32_t types = parse_backends(names);
		if (types < 0) {
			goto error;
		}

		if ((types & (WLR_BACKEND_DRM | WLR_BACKEND_LIBINPUT)) ==
				(WLR_BACKEND_DRM | WLR_BACKEND_LIBINPUT)) {
			multi->session = wlr_session_create(display);
			if (!multi->session) {
				wlr_log(WLR_ERROR, "Failed to start a DRM session");
				goto error;
			}
		}

		if (types & WLR_BACKEND_X11) {
			if (attempt_x11_backend(backend, display, NULL,
					create_renderer_func) < 0) {
				wlr_log(WLR_ERROR, "Failed to start load x11 backend");
				goto error;
			}
		}

		if (types & WLR_BACKEND_WAYLAND) {
			if (attempt_wl_backend(backend, display,
					create_renderer_func) < 0) {
				wlr_log(WLR_ERROR, "Failed to start load wayland backend");
				goto error;
			}
		}

		if (types & WLR_BACKEND_NOOP) {
			if (attempt_noop_backend(backend, display) < 0) {
				wlr_log(WLR_ERROR, "Failed to start load noop backend");
				goto error;
			}
		}

		if (types & WLR_BACKEND_LIBINPUT) {
			if (attempt_libinput_backend(backend, multi->session,
					display) < 0) {
				wlr_log(WLR_ERROR, "Failed to start load libinput backend");
				goto error;
			}
		}

		if (types & WLR_BACKEND_DRM) {
			if (attempt_drm_backend(display, backend,
					multi->session, create_renderer_func) < 0) {
				wlr_log(WLR_ERROR, "Failed to start load drm backend");
				goto error;
			}
		}

		if (types & WLR_BACKEND_HEADLESS) {
			if (attempt_headless_backend(backend, display,
					create_renderer_func) < 0) {
				wlr_log(WLR_ERROR, "Failed to start load headless backend");
				goto error;
			}
		}

		return backend;
	}

	if (getenv("WAYLAND_DISPLAY") || getenv("_WAYLAND_DISPLAY") ||
			getenv("WAYLAND_SOCKET")) {
		int rc = attempt_wl_backend(backend, display, create_renderer_func);
		if (rc < 0) {
			goto error;
		}

		return backend;
	}

#if WLR_HAS_X11_BACKEND
	const char *x11_display = getenv("DISPLAY");
	if (x11_display) {
		int rc = attempt_x11_backend(backend, display, x11_display, create_renderer_func);
		if (rc < 0) {
			goto error;
		}

		return backend;
	}
#endif

	// Attempt DRM+libinput
	multi->session = wlr_session_create(display);
	if (!multi->session) {
		wlr_log(WLR_ERROR, "Failed to start a DRM session");
		goto error;
	}

	int rc = attempt_libinput_backend(backend, multi->session, display);
	if (rc < 0) {
		wlr_log(WLR_ERROR, "Failed to start libinput backend");
		goto error;
	}

	rc = attempt_drm_backend(display, backend,
		multi->session, create_renderer_func);
	if (rc < 0) {
		wlr_log(WLR_ERROR, "Failed to open any DRM device");
		goto error;
	}

	return backend;

error:
	wlr_session_destroy(multi->session);
	wlr_backend_destroy(backend);
	return NULL;
}
