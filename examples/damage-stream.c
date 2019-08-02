#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include <wayland-client-protocol.h>
#include "wlr-damage-stream-unstable-v1-client-protocol.h"

struct damage_client {
	struct wl_output *output;
	struct zwlr_damage_stream_manager_v1 *damage_stream_manager;
	struct zwlr_damage_stream_v1 *damage_stream;

	int do_exit;
};

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct damage_client *ctx = data;

	if (strcmp(interface, wl_output_interface.name) == 0) {
		if (ctx->output)
			return;

		ctx->output = wl_registry_bind(registry, name,
				&wl_output_interface, 1);

	}

	if (strcmp(interface, zwlr_damage_stream_manager_v1_interface.name) == 0) {
		assert(!ctx->damage_stream);

		ctx->damage_stream_manager = wl_registry_bind(registry, name,
				&zwlr_damage_stream_manager_v1_interface, 1);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	/* Do nothing */
}

static void handle_damage(void* data, struct zwlr_damage_stream_v1 *stream,
		uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2) {
	printf("Damage at coordinates (%" PRIu32 ", %" PRIu32 "; %" PRIu32 ", %" PRIu32 ")\n",
			x1, y1, x2, y2);
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

static const struct zwlr_damage_stream_v1_listener damage_listener = {
	.damage = handle_damage,
};

int main(int argc, char *argv[]) {
	struct damage_client ctx = { 0 };

	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "Failed to create display: %m");
		return 1;
	}

	struct wl_registry* registry = wl_display_get_registry(display);
	assert(registry);

	wl_registry_add_listener(registry, &registry_listener, &ctx);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (!ctx.output) {
		fprintf(stderr, "No output\n");
		return 1;
	}

	if (!ctx.damage_stream) {
		fprintf(stderr, "No damage stream is available\n");
		return 1;
	}

	ctx.damage_stream = zwlr_damage_stream_manager_v1_subscribe(
			ctx.damage_stream_manager, ctx.output);

	zwlr_damage_stream_v1_add_listener(ctx.damage_stream, &damage_listener,
			&ctx);

	while (wl_display_dispatch(display) >= 0 && !ctx.do_exit);

	wl_display_disconnect(display);
	return 0;
}
