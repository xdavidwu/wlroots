#include "backend/drm-atomic/backend.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server.h>

#include <wlr/backend/interface.h>
#include <wlr/backend/session.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/log.h>

static struct wlr_backend_impl adrm_backend_impl;

static bool adrm_backend_start(struct wlr_backend *wlr) {
	return true;
}

static void adrm_backend_destroy(struct wlr_backend *wlr) {
	if (!wlr) {
		return;
	}

	struct adrm_backend *drm = (struct adrm_backend *)wlr;

	wl_list_remove(&drm->display_destroy.link);
	free(wlr);
}

static struct wlr_renderer *adrm_backend_get_renderer(struct wlr_backend *wlr) {
	return NULL;
}

static struct wlr_backend_impl adrm_backend_impl = {
	.start = adrm_backend_start,
	.destroy = adrm_backend_destroy,
	.get_renderer = adrm_backend_get_renderer,
};

static void adrm_backend_display_destroy(struct wl_listener *listener, void *data) {
	struct adrm_backend *drm = wl_container_of(listener, drm, display_destroy);
	adrm_backend_destroy(&drm->base);
}

// TODO: Rename function
struct wlr_backend *wlr_adrm_backend_create(struct wl_display *display,
		struct wlr_session *session, int gpu_fd,
		wlr_renderer_create_func_t create_renderer_func) {
	assert(display);

	struct adrm_backend *drm = calloc(1, sizeof(*drm));
	if (!drm) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	wlr_backend_init(&drm->base, &adrm_backend_impl);

	drm->fd = gpu_fd;
	drm->session = session;

	drm->display = display;
	drm->display_destroy.notify = adrm_backend_display_destroy;
	wl_display_add_destroy_listener(display, &drm->display_destroy);

	return &drm->base;
}
