#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_DAMAGE_STREAM_V1_H
#define WLR_TYPES_WLR_DAMAGE_STREAM_V1_H

#include <wayland-server-core.h>

struct wlr_output;

struct wlr_damage_stream_manager_v1 {
        struct wl_global *global;

        struct wl_list resources;
        struct wl_list subscriptions;

        struct wl_listener display_destroy;
        struct wl_signal destroy_signal;
};

struct wlr_damage_stream_v1 {
        struct wl_global *global;

        struct wl_list link;
        struct wl_resource* resource;

        struct wlr_damage_stream_manager_v1 *manager;
        struct wlr_output *output;

        struct wl_listener damage_listener;
};

struct wlr_damage_stream_manager_v1 *wlr_damage_stream_manager_v1_create(
		struct wl_display *display);

void wlr_damage_stream_manager_v1_destroy(
		struct wlr_damage_stream_manager_v1 *self);

#endif /* WLR_TYPES_WLR_DAMAGE_STREAM_V1_H */
