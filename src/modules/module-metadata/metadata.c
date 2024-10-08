/* PipeWire */
/* SPDX-FileCopyrightText: Copyright © 2019 Wim Taymans */
/* SPDX-License-Identifier: MIT */

#include <spa/utils/result.h>

#include <pipewire/impl.h>

#include <pipewire/extensions/metadata.h>

#define NAME "metadata"

struct impl {
	struct spa_hook context_listener;

	struct pw_global *global;
	struct spa_hook global_listener;

	struct pw_metadata *metadata;
	struct pw_resource *resource;
	struct spa_hook resource_listener;
	int pending;
};

struct resource_data {
	struct impl *impl;

	struct pw_resource *resource;
	struct spa_hook resource_listener;
	struct spa_hook object_listener;
	struct spa_hook metadata_listener;
	struct spa_hook impl_resource_listener;
	int pong_seq;
};

#define pw_metadata_resource(r,m,v,...)      \
	pw_resource_call_res(r,struct pw_metadata_events,m,v,__VA_ARGS__)

#define pw_metadata_resource_property(r,...)        \
        pw_metadata_resource(r,property,0,__VA_ARGS__)

static int metadata_property(void *data,
			uint32_t subject,
			const char *key,
			const char *type,
			const char *value)
{
	struct resource_data *d = data;
	struct pw_resource *resource = d->resource;
	struct pw_impl_client *client = pw_resource_get_client(resource);
	struct impl *impl = d->impl;

	if (impl->pending == 0 || d->pong_seq != 0) {
		int res = pw_impl_client_check_permissions(client, subject, PW_PERM_R);
		if (res >= 0 ||
		    (res == -ENOENT && key == NULL && type == NULL && value == NULL))
			pw_metadata_resource_property(d->resource, subject, key, type, value);
	}
	return 0;
}

static const struct pw_metadata_events metadata_events = {
	PW_VERSION_METADATA_EVENTS,
	.property = metadata_property,
};

static int metadata_set_property(void *object,
			uint32_t subject,
			const char *key,
			const char *type,
			const char *value)
{
	struct resource_data *d = object;
	struct impl *impl = d->impl;
	struct pw_resource *resource = d->resource;
	struct pw_impl_client *client = pw_resource_get_client(resource);
	int res;

	if ((res = pw_impl_client_check_permissions(client, subject, PW_PERM_R | PW_PERM_M)) < 0)
		goto error;

	pw_metadata_set_property(impl->metadata, subject, key, type, value);
	return 0;

error:
	pw_resource_errorf(resource, res, "set property error for id %d: %s",
		subject, spa_strerror(res));
	return res;
}

static int metadata_clear(void *object)
{
	struct resource_data *d = object;
	struct impl *impl = d->impl;
	pw_metadata_clear(impl->metadata);
	return 0;
}

static const struct pw_metadata_methods metadata_methods = {
	PW_VERSION_METADATA_METHODS,
	.set_property = metadata_set_property,
	.clear = metadata_clear,
};


static void global_unbind(void *data)
{
	struct resource_data *d = data;
	if (d->resource) {
	        spa_hook_remove(&d->resource_listener);
	        spa_hook_remove(&d->object_listener);
	        spa_hook_remove(&d->metadata_listener);
	        spa_hook_remove(&d->impl_resource_listener);
	}
}

static const struct pw_resource_events resource_events = {
	PW_VERSION_RESOURCE_EVENTS,
	.destroy = global_unbind,
};

static void remove_pending(struct resource_data *d)
{
	if (d->pong_seq != 0) {
		pw_impl_client_set_busy(pw_resource_get_client(d->resource), false);
		d->pong_seq = 0;
		d->impl->pending--;
	}
}

static void impl_resource_destroy(void *data)
{
	struct resource_data *d = data;
	remove_pending(d);
}

static void impl_resource_pong(void *data, int seq)
{
	struct resource_data *d = data;
	if (d->pong_seq == seq)
		remove_pending(d);
}

static const struct pw_resource_events impl_resource_events = {
	PW_VERSION_RESOURCE_EVENTS,
	.destroy = impl_resource_destroy,
	.pong = impl_resource_pong,
};

static int
global_bind(void *object, struct pw_impl_client *client, uint32_t permissions,
            uint32_t version, uint32_t id)
{
	struct impl *impl = object;
	struct pw_resource *resource;
	struct resource_data *data;

	resource = pw_resource_new(client, id, permissions, PW_TYPE_INTERFACE_Metadata, version, sizeof(*data));
        if (resource == NULL)
                return -errno;

        data = pw_resource_get_user_data(resource);
        data->impl = impl;
        data->resource = resource;

	pw_global_add_resource(impl->global, resource);

	/* listen for when the resource goes away */
        pw_resource_add_listener(resource,
                        &data->resource_listener,
                        &resource_events, data);

	/* resource methods -> implementation */
	pw_resource_add_object_listener(resource,
			&data->object_listener,
                        &metadata_methods, data);

	pw_impl_client_set_busy(client, true);

	/* implementation events -> resource */
	pw_metadata_add_listener(impl->metadata,
			&data->metadata_listener,
			&metadata_events, data);

	pw_resource_add_listener(impl->resource,
			&data->impl_resource_listener,
                        &impl_resource_events, data);

	data->pong_seq = pw_resource_ping(impl->resource, data->pong_seq);
	impl->pending++;

	return 0;
}

static void global_destroy(void *data)
{
	struct impl *impl = data;
	spa_hook_remove(&impl->global_listener);
	impl->global = NULL;
	if (impl->resource)
		pw_resource_destroy(impl->resource);
}

static const struct pw_global_events global_events = {
	PW_VERSION_GLOBAL_EVENTS,
	.destroy = global_destroy,
};


static void context_global_removed(void *data, struct pw_global *global)
{
	struct impl *impl = data;
	pw_log_trace("Clearing properties for global %u in %u",
				 pw_global_get_id(global), pw_global_get_id(impl->global));
	pw_metadata_set_property(impl->metadata,
			pw_global_get_id(global), NULL, NULL, NULL);
}

static const struct pw_context_events context_events = {
	PW_VERSION_CONTEXT_EVENTS,
	.global_removed = context_global_removed,
};

static void global_resource_destroy(void *data)
{
	struct impl *impl = data;
	spa_hook_remove(&impl->context_listener);
	spa_hook_remove(&impl->resource_listener);
	impl->resource = NULL;
	impl->metadata = NULL;
	if (impl->global)
		pw_global_destroy(impl->global);
	free(impl);
}

static const struct pw_resource_events global_resource_events = {
	PW_VERSION_RESOURCE_EVENTS,
	.destroy = global_resource_destroy,
};

struct pw_metadata *
pw_metadata_new(struct pw_context *context, struct pw_resource *resource,
		   struct pw_properties *properties)
{
	struct impl *impl;
	char serial_str[32];
	struct spa_dict_item items[1] = {
		SPA_DICT_ITEM_INIT(PW_KEY_OBJECT_SERIAL, serial_str),
	};
	struct spa_dict extra_props = SPA_DICT_INIT_ARRAY(items);
	static const char * const keys[] = {
		PW_KEY_OBJECT_SERIAL,
		NULL
	};

	if (properties == NULL)
		properties = pw_properties_new(NULL, NULL);
	if (properties == NULL)
		return NULL;

	impl = calloc(1, sizeof(*impl));
	if (impl == NULL) {
		pw_properties_free(properties);
		return NULL;
	}

	pw_resource_install_marshal(resource, true);

	impl->global = pw_global_new(context,
			PW_TYPE_INTERFACE_Metadata,
			PW_VERSION_METADATA,
			PW_METADATA_PERM_MASK,
			properties,
			global_bind, impl);
	if (impl->global == NULL) {
		free(impl);
		return NULL;
	}
	impl->resource = resource;
	impl->metadata = (struct pw_metadata*)resource;

	spa_scnprintf(serial_str, sizeof(serial_str), "%"PRIu64,
			pw_global_get_serial(impl->global));
	pw_global_update_keys(impl->global, &extra_props, keys);

	pw_context_add_listener(context, &impl->context_listener,
			&context_events, impl);

	pw_global_add_listener(impl->global,
			&impl->global_listener,
			&global_events, impl);

	pw_resource_set_bound_id(resource, pw_global_get_id(impl->global));
	pw_global_register(impl->global);

	pw_resource_add_listener(resource,
			&impl->resource_listener,
			&global_resource_events, impl);

	return impl->metadata;
}
