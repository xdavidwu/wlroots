#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-util.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "input-method-unstable-v2-protocol.h"
#include "util/shm.h"
#include "util/signal.h"

static const struct zwp_input_method_v2_interface input_method_impl;
static const struct zwp_input_method_keyboard_grab_v2_interface im_keyboard_grab_impl;

static struct wlr_input_method_v2 *input_method_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwp_input_method_v2_interface, &input_method_impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_input_method_keyboard_grab_v2 *im_keyboard_grab_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwp_input_method_keyboard_grab_v2_interface,
		&im_keyboard_grab_impl));
	return wl_resource_get_user_data(resource);
}

static void im_keyboard_grab_destroy(
		struct wlr_input_method_keyboard_grab_v2 *im_keyboard_grab);

static void input_method_destroy(struct wlr_input_method_v2 *input_method) {
	wlr_signal_emit_safe(&input_method->events.destroy, input_method);
	wl_list_remove(wl_resource_get_link(input_method->resource));
	wl_list_remove(&input_method->seat_destroy.link);
	if (input_method->im_keyboard_grab != NULL) {
		wl_resource_destroy(input_method->im_keyboard_grab->resource);
	}
	free(input_method->pending.commit_text);
	free(input_method->pending.preedit.text);
	free(input_method->current.commit_text);
	free(input_method->current.preedit.text);
	free(input_method);
}

static void input_method_resource_destroy(struct wl_resource *resource) {
	struct wlr_input_method_v2 *input_method =
		input_method_from_resource(resource);
	if (!input_method) {
		return;
	}
	input_method_destroy(input_method);
}

static void im_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void im_commit(struct wl_client *client, struct wl_resource *resource,
		uint32_t serial) {
	struct wlr_input_method_v2 *input_method =
		input_method_from_resource(resource);
	if (!input_method) {
		return;
	}
	input_method->current = input_method->pending;
	input_method->current_serial = serial;
	struct wlr_input_method_v2_state default_state = {0};
	input_method->pending = default_state;
	wlr_signal_emit_safe(&input_method->events.commit, (void*)input_method);
}

static void im_commit_string(struct wl_client *client,
		struct wl_resource *resource, const char *text) {
	struct wlr_input_method_v2 *input_method =
		input_method_from_resource(resource);
	if (!input_method) {
		return;
	}
	free(input_method->pending.commit_text);
	input_method->pending.commit_text = strdup(text);
}

static void im_set_preedit_string(struct wl_client *client,
		struct wl_resource *resource, const char *text, int32_t cursor_begin,
		int32_t cursor_end) {
	struct wlr_input_method_v2 *input_method =
		input_method_from_resource(resource);
	if (!input_method) {
		return;
	}
	input_method->pending.preedit.cursor_begin = cursor_begin;
	input_method->pending.preedit.cursor_end = cursor_end;
	free(input_method->pending.preedit.text);
	input_method->pending.preedit.text = strdup(text);
}

static void im_delete_surrounding_text(struct wl_client *client,
		struct wl_resource *resource,
		uint32_t before_length, uint32_t after_length) {
	struct wlr_input_method_v2 *input_method =
		input_method_from_resource(resource);
	if (!input_method) {
		return;
	}
	input_method->pending.delete.before_length = before_length;
	input_method->pending.delete.after_length = after_length;
}

static void im_get_input_popup_surface(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface) {
	wlr_log(WLR_INFO, "Stub: zwp_input_method_v2::get_input_popup_surface");
}

static void im_keyboard_grab_destroy(
		struct wlr_input_method_keyboard_grab_v2 *im_keyboard_grab) {
	if (im_keyboard_grab->grabbed) {
		im_keyboard_grab->grabbed = false;
		wlr_seat_keyboard_end_grab(im_keyboard_grab->grab->seat);
	}
	im_keyboard_grab->input_method->im_keyboard_grab = NULL;
	free(im_keyboard_grab->grab);
	free(im_keyboard_grab);
}

static void im_keyboard_grab_resource_destroy(struct wl_resource *resource) {
	struct wlr_input_method_keyboard_grab_v2 *im_keyboard_grab =
		im_keyboard_grab_from_resource(resource);
	if (!im_keyboard_grab) {
		return;
	}
	im_keyboard_grab_destroy(im_keyboard_grab);
}

static void im_keyboard_grab_release(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zwp_input_method_keyboard_grab_v2_interface im_keyboard_grab_impl = {
	.release = im_keyboard_grab_release,
};

static void im_keyboard_grab_enter(struct wlr_seat_keyboard_grab *grab,
		struct wlr_surface *surface, uint32_t keycodes[],
		size_t num_keycodes, struct wlr_keyboard_modifiers *modifiers) {
	// Nothing to send for input-method keyboard grab
}

static void im_keyboard_grab_key(struct wlr_seat_keyboard_grab *grab,
		uint32_t time, uint32_t key, uint32_t state) {
	struct wlr_input_method_keyboard_grab_v2 *im_keyboard_grab = grab->data;
	zwp_input_method_keyboard_grab_v2_send_key(im_keyboard_grab->resource,
			im_keyboard_grab->serial++, time, key, state);
}

static void im_keyboard_grab_modifiers(struct wlr_seat_keyboard_grab *grab,
		struct wlr_keyboard_modifiers *modifiers) {
	struct wlr_input_method_keyboard_grab_v2 *im_keyboard_grab = grab->data;
	zwp_input_method_keyboard_grab_v2_send_modifiers(
			im_keyboard_grab->resource, im_keyboard_grab->serial++,
			modifiers->depressed, modifiers->latched,
			modifiers->locked, modifiers->group);
}

static void im_keyboard_grab_cancel(struct wlr_seat_keyboard_grab *grab) {
	struct wlr_input_method_keyboard_grab_v2 *im_keyboard_grab = grab->data;
	if (im_keyboard_grab->grabbed) {
		im_keyboard_grab->grabbed = false;
		struct wl_resource *resource = im_keyboard_grab->resource;
		wl_list_remove(&im_keyboard_grab->keymap_listener.link);
		wl_list_remove(&im_keyboard_grab->repeat_info_listener.link);
		im_keyboard_grab_destroy(im_keyboard_grab);
		wl_resource_set_user_data(resource, NULL);
	}
}

static const struct wlr_keyboard_grab_interface keyboard_grab_impl = {
	.enter = im_keyboard_grab_enter,
	.key = im_keyboard_grab_key,
	.modifiers = im_keyboard_grab_modifiers,
	.cancel = im_keyboard_grab_cancel,
};

static bool im_keyboard_grab_send_keymap(
		struct wlr_input_method_keyboard_grab_v2 *im_keyboard_grab,
		struct wlr_keyboard *keyboard) {
	int keymap_fd = allocate_shm_file(keyboard->keymap_size);
	if (keymap_fd < 0) {
		wlr_log(WLR_ERROR, "creating a keymap file for %zu bytes failed",
				keyboard->keymap_size);
		return false;
	}

	void *ptr = mmap(NULL, keyboard->keymap_size, PROT_READ | PROT_WRITE,
		MAP_SHARED, keymap_fd, 0);
	if (ptr == MAP_FAILED) {
		wlr_log(WLR_ERROR, "failed to mmap() %zu bytes",
				keyboard->keymap_size);
		close(keymap_fd);
		return false;
	}

	strcpy(ptr, keyboard->keymap_string);
	munmap(ptr, keyboard->keymap_size);

	zwp_input_method_keyboard_grab_v2_send_keymap(im_keyboard_grab->resource,
		WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, keymap_fd,
		keyboard->keymap_size);

	close(keymap_fd);
	return true;
}

static void im_keyboard_grab_send_repeat_info(
		struct wlr_input_method_keyboard_grab_v2 *im_keyboard_grab,
		struct wlr_keyboard *keyboard) {
	zwp_input_method_keyboard_grab_v2_send_repeat_info(
		im_keyboard_grab->resource, keyboard->repeat_info.rate,
		keyboard->repeat_info.delay);
}

static void handle_keyboard_keymap(struct wl_listener *listener, void *data) {
	struct wlr_input_method_keyboard_grab_v2 *im_keyboard_grab =
		wl_container_of(listener, im_keyboard_grab, keymap_listener);
	im_keyboard_grab_send_keymap(im_keyboard_grab, data);
}

static void handle_keyboard_repeat_info(struct wl_listener *listener,
		void *data) {
	struct wlr_input_method_keyboard_grab_v2 *im_keyboard_grab =
		wl_container_of(listener, im_keyboard_grab, keymap_listener);
	im_keyboard_grab_send_repeat_info(im_keyboard_grab, data);
}

static void im_grab_keyboard(struct wl_client *client,
		struct wl_resource *resource, uint32_t keyboard) {
	struct wlr_input_method_v2 *input_method =
		input_method_from_resource(resource);
	if (!input_method) {
		return;
	}
	if (input_method->im_keyboard_grab) {
		// Already grabbed
		return;
	}
	struct wlr_input_method_keyboard_grab_v2 *im_keyboard_grab =
		calloc(1, sizeof(struct wlr_input_method_keyboard_grab_v2));
	if (!im_keyboard_grab) {
		wl_client_post_no_memory(client);
		return;
	}
	struct wlr_seat_keyboard_grab *keyboard_grab = calloc(1,
			sizeof(struct wlr_seat_keyboard_grab));
	if (!keyboard_grab) {
		wl_client_post_no_memory(client);
		return;
	}
	struct wl_resource *im_keyboard_grab_resource = wl_resource_create(
			client, &zwp_input_method_keyboard_grab_v2_interface,
			wl_resource_get_version(resource), keyboard);
	if (im_keyboard_grab_resource == NULL) {
		free(im_keyboard_grab);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(im_keyboard_grab_resource,
			&im_keyboard_grab_impl, im_keyboard_grab,
			im_keyboard_grab_resource_destroy);
	keyboard_grab->interface = &keyboard_grab_impl;
	keyboard_grab->data = im_keyboard_grab;
	im_keyboard_grab->resource = im_keyboard_grab_resource;
	im_keyboard_grab->grab = keyboard_grab;
	im_keyboard_grab->input_method = input_method;
	input_method->im_keyboard_grab = im_keyboard_grab;

	if (!im_keyboard_grab_send_keymap(im_keyboard_grab,
				input_method->seat->keyboard_state.keyboard)) {
		// send initial keymap memory map failed
		wl_client_post_no_memory(client);
		wl_resource_destroy(im_keyboard_grab_resource);
		return;
	}
	im_keyboard_grab_send_repeat_info(im_keyboard_grab,
			input_method->seat->keyboard_state.keyboard);
	wlr_seat_keyboard_start_grab(input_method->seat, keyboard_grab);
	wlr_seat_keyboard_notify_modifiers(input_method->seat,
			&input_method->seat->keyboard_state.keyboard->modifiers);
	im_keyboard_grab->grabbed = true;

	im_keyboard_grab->keymap_listener.notify = handle_keyboard_keymap;
	wl_signal_add(&input_method->seat->keyboard_state.keyboard->events.keymap,
			&im_keyboard_grab->keymap_listener);
	im_keyboard_grab->repeat_info_listener.notify = handle_keyboard_repeat_info;
	wl_signal_add(&input_method->seat->keyboard_state.keyboard->events.repeat_info,
			&im_keyboard_grab->repeat_info_listener);
}

static const struct zwp_input_method_v2_interface input_method_impl = {
	.destroy = im_destroy,
	.commit = im_commit,
	.commit_string = im_commit_string,
	.set_preedit_string = im_set_preedit_string,
	.delete_surrounding_text = im_delete_surrounding_text,
	.get_input_popup_surface = im_get_input_popup_surface,
	.grab_keyboard = im_grab_keyboard,
};

void wlr_input_method_v2_send_activate(
		struct wlr_input_method_v2 *input_method) {
	zwp_input_method_v2_send_activate(input_method->resource);
	input_method->active = true;
}

void wlr_input_method_v2_send_deactivate(
		struct wlr_input_method_v2 *input_method) {
	zwp_input_method_v2_send_deactivate(input_method->resource);
	input_method->active = false;
}

void wlr_input_method_v2_send_surrounding_text(
		struct wlr_input_method_v2 *input_method, const char *text,
		uint32_t cursor, uint32_t anchor) {
	const char *send_text = text;
	if (!send_text) {
		send_text = "";
	}
	zwp_input_method_v2_send_surrounding_text(input_method->resource, send_text,
		cursor, anchor);
}

void wlr_input_method_v2_send_text_change_cause(
		struct wlr_input_method_v2 *input_method, uint32_t cause) {
	zwp_input_method_v2_send_text_change_cause(input_method->resource, cause);
}

void wlr_input_method_v2_send_content_type(
		struct wlr_input_method_v2 *input_method,
		uint32_t hint, uint32_t purpose) {
	zwp_input_method_v2_send_content_type(input_method->resource, hint,
		purpose);
}

void wlr_input_method_v2_send_done(struct wlr_input_method_v2 *input_method) {
	zwp_input_method_v2_send_done(input_method->resource);
	input_method->client_active = input_method->active;
	input_method->current_serial++;
}

void wlr_input_method_v2_send_unavailable(
		struct wlr_input_method_v2 *input_method) {
	zwp_input_method_v2_send_unavailable(input_method->resource);
	struct wl_resource *resource = input_method->resource;
	input_method_destroy(input_method);
	wl_resource_set_user_data(resource, NULL);
}

static const struct zwp_input_method_manager_v2_interface
	input_method_manager_impl;

static struct wlr_input_method_manager_v2 *input_method_manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwp_input_method_manager_v2_interface, &input_method_manager_impl));
	return wl_resource_get_user_data(resource);
}

static void input_method_handle_seat_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_input_method_v2 *input_method = wl_container_of(listener,
		input_method, seat_destroy);
	wlr_input_method_v2_send_unavailable(input_method);
}

static void manager_get_input_method(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat,
		uint32_t input_method_id) {
	struct wlr_input_method_manager_v2 *im_manager =
		input_method_manager_from_resource(resource);

	struct wlr_input_method_v2 *input_method = calloc(1,
		sizeof(struct wlr_input_method_v2));
	if (!input_method) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_signal_init(&input_method->events.commit);
	wl_signal_init(&input_method->events.destroy);
	int version = wl_resource_get_version(resource);
	struct wl_resource *im_resource = wl_resource_create(client,
		&zwp_input_method_v2_interface, version, input_method_id);
	if (im_resource == NULL) {
		free(input_method);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(im_resource, &input_method_impl,
		input_method, input_method_resource_destroy);

	struct wlr_seat_client *seat_client = wlr_seat_client_from_resource(seat);
	wl_signal_add(&seat_client->events.destroy,
		&input_method->seat_destroy);
	input_method->seat_destroy.notify =
		input_method_handle_seat_destroy;

	input_method->resource = im_resource;
	input_method->seat = seat_client->seat;
	wl_list_insert(&im_manager->input_methods,
		wl_resource_get_link(input_method->resource));
	wlr_signal_emit_safe(&im_manager->events.input_method, input_method);
}

static void manager_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zwp_input_method_manager_v2_interface
		input_method_manager_impl = {
	.get_input_method = manager_get_input_method,
	.destroy = manager_destroy,
};

static void input_method_manager_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	assert(wl_client);
	struct wlr_input_method_manager_v2 *im_manager = data;

	struct wl_resource *bound_resource = wl_resource_create(wl_client,
		&zwp_input_method_manager_v2_interface, version, id);
	if (bound_resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(bound_resource, &input_method_manager_impl,
		im_manager, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_input_method_manager_v2 *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_signal_emit_safe(&manager->events.destroy, manager);
	wl_list_remove(&manager->display_destroy.link);
	wl_global_destroy(manager->global);
	free(manager);
}

struct wlr_input_method_manager_v2 *wlr_input_method_manager_v2_create(
		struct wl_display *display) {
	struct wlr_input_method_manager_v2 *im_manager = calloc(1,
		sizeof(struct wlr_input_method_manager_v2));
	if (!im_manager) {
		return NULL;
	}
	wl_signal_init(&im_manager->events.input_method);
	wl_signal_init(&im_manager->events.destroy);
	wl_list_init(&im_manager->input_methods);

	im_manager->global = wl_global_create(display,
		&zwp_input_method_manager_v2_interface, 1, im_manager,
		input_method_manager_bind);
	if (!im_manager->global) {
		free(im_manager);
		return NULL;
	}

	im_manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &im_manager->display_destroy);

	return im_manager;
}
