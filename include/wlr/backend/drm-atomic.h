/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_BACKEND_DRM_ATOMIC_H
#define WLR_BACKEND_DRM_ATOMIC_H

#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>

struct wlr_backend *wlr_adrm_backend_create(struct wl_display *display,
	struct wlr_session *session, int gpu_fd,
	wlr_renderer_create_func_t create_renderer_func);

#endif
