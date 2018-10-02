#ifndef BACKEND_DRM_ATOMIC_BACKEND_H
#define BACKEND_DRM_ATOMIC_BACKEND_H

#include <wayland-server.h>

#include <wlr/backend/interface.h>
#include <wlr/backend/session.h>

struct adrm_backend {
	struct wlr_backend base;

	int fd;

	struct wlr_session *session;

	struct wl_display *display;
	struct wl_listener display_destroy;
};

#endif
