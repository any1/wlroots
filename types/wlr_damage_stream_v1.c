#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_damage_stream_v1.h>

#include "wlr-damage-stream-unstable-v1-protocol.h"
#include "util/signal.h"

static const struct zwlr_damage_stream_v1_interface damage_stream_impl;

static struct wlr_damage_stream_v1 *stream_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
			&zwlr_damage_stream_v1_interface,
			&damage_stream_impl));
	return wl_resource_get_user_data(resource);
}

static void destroy_stream(struct wlr_damage_stream_v1 *self) {
	if (!self)
		return;

	wl_list_remove(&self->damage_listener.link);
	wl_list_remove(&self->link);

	free(self);
}

static void damage_stream_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zwlr_damage_stream_v1_interface damage_stream_impl = {
	.destroy = damage_stream_destroy,
};

static void stream_handle_resource_destroy(struct wl_resource *resource) {
	destroy_stream(stream_from_resource(resource));
}

void damage_listener(struct wl_listener *listener, void* data) {
	struct wlr_damage_stream_v1 *stream =
		wl_container_of(listener, stream, damage_listener);

	int x1, y1, x2, y2;

	struct pixman_box32* ext =
		pixman_region32_extents(&stream->output->damage);

	x1 = ext->x1;
	y1 = ext->y1;
	x2 = ext->x2;
	y2 = ext->y2;

	zwlr_damage_stream_v1_send_damage(stream->resource, x1, y1, x2, y2);
}

static void subscribe(struct wl_client *client,
		struct wlr_damage_stream_manager_v1 *manager, uint32_t version,
		uint32_t stream_id, struct wlr_output* output) {

	struct wlr_damage_stream_v1 *stream = calloc(1, sizeof(*stream));
	if (!stream) {
		wl_client_post_no_memory(client);
		return;
	}

	stream->manager = manager;
	stream->output = output;

	stream->resource = wl_resource_create(client,
			&zwlr_damage_stream_v1_interface, version,
			stream_id);
	if (!stream->resource) {
		free(stream);
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(stream->resource, &damage_stream_impl,
			stream, stream_handle_resource_destroy);

	stream->damage_listener.notify = damage_listener;
	wl_signal_add(&output->events.needs_frame, &stream->damage_listener);

	wl_list_insert(&manager->subscriptions, &stream->link);

}
		

static const struct zwlr_damage_stream_manager_v1_interface damage_stream_manager_impl;

static struct wlr_damage_stream_manager_v1 *manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
			&zwlr_damage_stream_manager_v1_interface,
			&damage_stream_manager_impl));
	return wl_resource_get_user_data(resource);
}

static void damage_stream_manager_subscribe(struct wl_client *client,
		struct wl_resource *resource, uint32_t stream_id,
		struct wl_resource *output_resource) {
	struct wlr_damage_stream_manager_v1 *manager =
		manager_from_resource(resource);
	uint32_t version = wl_resource_get_version(resource);
	struct wlr_output *output = wlr_output_from_resource(output_resource);

	subscribe(client, manager, version, stream_id, output);
}

static void damage_stream_manager_destroy(struct wl_client *client,
		struct wl_resource* resource) {
	wl_resource_destroy(resource);
}

static const struct zwlr_damage_stream_manager_v1_interface
damage_stream_manager_impl = {
	.subscribe = damage_stream_manager_subscribe,
	.destroy = damage_stream_manager_destroy,
};

static void manager_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void manager_bind(struct wl_client *client, void *data, uint32_t version,
		uint32_t id) {
	struct wlr_damage_stream_manager_v1 *self = data;

	struct wl_resource *resource = wl_resource_create(client,
			&zwlr_damage_stream_manager_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &damage_stream_manager_impl,
			self, manager_handle_resource_destroy);

	wl_list_insert(&self->resources, wl_resource_get_link(resource));
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_damage_stream_manager_v1 *self =
		wl_container_of(listener, self, display_destroy);
	wlr_damage_stream_manager_v1_destroy(self);
}

struct wlr_damage_stream_manager_v1 *wlr_damage_stream_manager_v1_create(
		struct wl_display *display) {
	struct wlr_damage_stream_manager_v1 *self = calloc(1, sizeof(*self));
	if (!self)
		return NULL;

	self->global = wl_global_create(display,
			&zwlr_damage_stream_manager_v1_interface, 1, self,
			manager_bind);
	if (!self->global)
		goto failure;

	wl_list_init(&self->resources);
	wl_list_init(&self->subscriptions);

	wl_signal_init(&self->destroy_signal);

	self->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &self->display_destroy);

	return self;

failure:
	free(self);
	return NULL;
}

void wlr_damage_stream_manager_v1_destroy(
		struct wlr_damage_stream_manager_v1 *self) {
	if (!self)
		return;

	wlr_signal_emit_safe(&self->destroy_signal, self);

	wl_list_remove(&self->display_destroy.link);

	struct wlr_damage_stream_v1 *stream, *tmp_stream;
	wl_list_for_each_safe(stream, tmp_stream, &self->subscriptions, link)
		wl_resource_destroy(stream->resource);

	struct wl_resource *resource, *tmp_resource;
	wl_resource_for_each_safe(resource, tmp_resource, &self->resources)
		wl_resource_destroy(resource);

	wl_global_destroy(self->global);
	free(self);
}
