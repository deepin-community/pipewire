/* PipeWire */
/* SPDX-FileCopyrightText: Copyright © 2019 Collabora Ltd. */
/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <locale.h>

#include <spa/utils/result.h>
#include <spa/utils/string.h>
#include <spa/utils/json.h>
#include <spa/debug/types.h>
#include <spa/debug/file.h>

#include <pipewire/pipewire.h>

#define GLOBAL_ID_NONE UINT32_MAX
#define DEFAULT_DOT_PATH "pw.dot"
#define DEFAULT_DOT_DATA_SIZE 2048
#define MAX_ESCAPED_LEN 128

struct global;

typedef void (*draw_t)(struct global *g);
typedef void *(*info_update_t) (void *info, const void *update);

struct dot_data {
	char *data;
	size_t size;
	size_t max_size;
};

struct data {
	struct pw_main_loop *loop;
	struct pw_context *context;

	struct pw_core *core;
	struct spa_hook core_listener;

	struct pw_registry *registry;
	struct spa_hook registry_listener;

	struct spa_list globals;
	struct dot_data dot_data;
	const char *dot_rankdir;
	bool dot_orthoedges;

	bool show_all;
	bool show_smart;
	bool show_detail;
};

struct global {
	struct spa_list link;

	struct data *data;
	struct pw_proxy *proxy;

	uint32_t id;
#define INTERFACE_Port		0
#define INTERFACE_Node		1
#define INTERFACE_Link		2
#define INTERFACE_Client	3
#define INTERFACE_Device	4
#define INTERFACE_Module	5
#define INTERFACE_Factory	6
	uint32_t type;
	void *info;

	pw_destroy_t info_destroy;
	info_update_t info_update;
	draw_t draw;
	bool drawn;

	struct spa_hook proxy_listener;
	struct spa_hook object_listener;
};

static bool dot_data_init(struct dot_data * dd, size_t size)
{
	if (size <= 0)
		return false;

	dd->data = malloc(sizeof (char) * size);
	dd->data[0] = '\0';
	dd->size = 0;
	dd->max_size = size;
	return true;
}

static void dot_data_clear(struct dot_data * dd)
{
	if (dd->data) {
		free(dd->data);
		dd->data = NULL;
	}
	dd->size = 0;
}

static void dot_data_ensure_max_size (struct dot_data * dd, size_t size)
{
	size_t new_size = dd->size + size + 1;
	if (new_size > dd->max_size) {
		size_t next_size = new_size * 2;
		dd->data = realloc (dd->data, next_size);
		dd->max_size = next_size;
	}
}

static void dot_data_add_uint32 (struct dot_data * dd, uint32_t value)
{
	int size;
	dot_data_ensure_max_size (dd, 16);
	size = snprintf (dd->data + dd->size, dd->max_size - dd->size, "%u", value);
	dd->size += size;
}

static void dot_data_add_string (struct dot_data * dd, const char *value)
{
	int size;
	dot_data_ensure_max_size (dd, strlen (value));
	size = snprintf (dd->data + dd->size, dd->max_size - dd->size, "%s", value);
	dd->size += size;
}

static int escape_quotes(char *str, int size, const char *val)
{
	int len = 0;
#define __PUT(c) { if (len < size) *str++ = c; len++; }
	while (*val) {
		switch (*val) {
		case '"':
			__PUT('\\'); __PUT(*val);
			break;
		default:
			__PUT(*val);
			break;
		}
		val++;

		/* Truncate with "..." if string has more than escaped len characters. */
		if (len >= MAX_ESCAPED_LEN) {
			__PUT('.'); __PUT('.'); __PUT('.');
			break;
		}
	}
	__PUT('\0');
#undef __PUT
	return len-1;
}

static void dot_data_add_string_escaped (struct dot_data * dd, const char *value)
{
	size_t escaped_size = escape_quotes (dd->data + dd->size,
			dd->max_size - dd->size, value);
	if (escaped_size + 1 > dd->max_size - dd->size) {
		dot_data_ensure_max_size (dd, escaped_size);
		escaped_size = escape_quotes (dd->data + dd->size,
				dd->max_size - dd->size, value);
	}
	dd->size += escaped_size;
}

static void draw_dict(struct dot_data * dd, const char *title,
				const struct spa_dict *props)
{
	const struct spa_dict_item *item;

	dot_data_add_string(dd, title);
	dot_data_add_string(dd, ":\\l");
	if (props == NULL || props->n_items == 0) {
		dot_data_add_string(dd, "- none\\l");
		return;
	}

	spa_dict_for_each(item, props) {
		if (item->value) {
			dot_data_add_string(dd, "- ");
			dot_data_add_string_escaped(dd, item->key);
			dot_data_add_string(dd, ": ");
			dot_data_add_string_escaped(dd, item->value);
			dot_data_add_string(dd, "\\l");
		} else {
			dot_data_add_string(dd, "- ");
			dot_data_add_string_escaped(dd, item->key);
			dot_data_add_string(dd, ": (null)\\l");
		}
	}
}

static void draw_port(struct global *g)
{
	spa_assert(g != NULL);
	spa_assert(g->info != NULL);
	spa_assert(g->type == INTERFACE_Port);

	struct pw_port_info *info = g->info;
	struct dot_data *dd = &g->data->dot_data;

	const char *port_name = spa_dict_lookup(info->props, PW_KEY_PORT_NAME);

	/* draw the box */
	dot_data_add_string(dd, "port_");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, " [shape=box style=filled fillcolor=");
	dot_data_add_string(dd, info->direction == PW_DIRECTION_INPUT ? "lightslateblue" : "lightcoral");
	dot_data_add_string(dd, "];");

	/* draw the label header */
	dot_data_add_string(dd, "port_");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, " [label=\"");

	/* draw the label body */
	dot_data_add_string(dd, "port_id: ");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, "\\lname: ");
	dot_data_add_string_escaped(dd, port_name ? port_name : "(null)");
	dot_data_add_string(dd, "\\ldirection: ");
	dot_data_add_string_escaped(dd, pw_direction_as_string(info->direction));
	dot_data_add_string(dd, "\\l");
	if (g->data->show_detail)
		draw_dict(dd, "properties", info->props);

	/* draw the label footer */
	dot_data_add_string(dd, "\"];\n");
}

static void draw_node_arrows(struct global *g)
{
	spa_assert(g != NULL);
	spa_assert(g->info != NULL);
	spa_assert(g->type == INTERFACE_Node);

	struct pw_node_info *info = g->info;
	struct dot_data *dd = &g->data->dot_data;

	const char *client_id_str, *factory_id_str;
	uint32_t client_id = GLOBAL_ID_NONE, factory_id = GLOBAL_ID_NONE;

	client_id_str = spa_dict_lookup(info->props, PW_KEY_CLIENT_ID);
	factory_id_str = spa_dict_lookup(info->props, PW_KEY_FACTORY_ID);
	spa_atou32(client_id_str, &client_id, 10);
	spa_atou32(factory_id_str, &factory_id, 10);

	if (client_id != GLOBAL_ID_NONE) {
		dot_data_add_string(dd, "node_");
		dot_data_add_uint32(dd, g->id);
		dot_data_add_string(dd, " -> client_");
		dot_data_add_uint32(dd, client_id);
		dot_data_add_string(dd, " [style=dashed];\n");
	}
	if (factory_id != GLOBAL_ID_NONE) {
		dot_data_add_string(dd, "node_");
		dot_data_add_uint32(dd, g->id);
		dot_data_add_string(dd, " -> factory_");
		dot_data_add_uint32(dd, factory_id);
		dot_data_add_string(dd, " [style=dashed];\n");
	}
}

static void draw_node(struct global *g)
{
	spa_assert(g != NULL);
	spa_assert(g->info != NULL);
	spa_assert(g->type == INTERFACE_Node);

	struct pw_node_info *info = g->info;
	struct dot_data *dd = &g->data->dot_data;

	const char *node_name, *media_class, *client_id_str, *factory_id_str;
	uint32_t client_id = GLOBAL_ID_NONE, factory_id = GLOBAL_ID_NONE;

	node_name = spa_dict_lookup(info->props, PW_KEY_NODE_NAME);
	media_class = spa_dict_lookup(info->props, PW_KEY_MEDIA_CLASS);
	client_id_str = spa_dict_lookup(info->props, PW_KEY_CLIENT_ID);
	factory_id_str = spa_dict_lookup(info->props, PW_KEY_FACTORY_ID);
	spa_atou32(client_id_str, &client_id, 10);
	spa_atou32(factory_id_str, &factory_id, 10);

	/* draw the node header */
	dot_data_add_string(dd, "subgraph cluster_node_");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, "{\n");
	dot_data_add_string(dd, "bgcolor=palegreen;\n");

	/* draw the label header */
	dot_data_add_string(dd, "label=\"");

	/* draw the label body */
	dot_data_add_string(dd, "node_id: ");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, "\\lname: ");
	dot_data_add_string_escaped(dd, node_name ? node_name : "(null)");
	dot_data_add_string(dd, "\\lmedia_class: ");
	dot_data_add_string_escaped(dd, media_class ? media_class : "(null)");
	dot_data_add_string(dd, "\\l");
	if (g->data->show_detail)
		draw_dict(dd, "properties", info->props);

	/* draw the label footer */
	dot_data_add_string(dd, "\"\n");

	/* draw all node ports */
	struct global *p;
	const char *prop_node_id;
	spa_list_for_each(p, &g->data->globals, link) {
		struct pw_port_info *pinfo;
		uint32_t node_id = GLOBAL_ID_NONE;
		if (p->info == NULL)
			continue;
		if (p->type != INTERFACE_Port)
			continue;
		pinfo = p->info;
		prop_node_id = spa_dict_lookup(pinfo->props, PW_KEY_NODE_ID);
		spa_atou32(prop_node_id, &node_id, 10);
		if (node_id == GLOBAL_ID_NONE || node_id != g->id)
			continue;
		if (p->draw)
			p->draw(p);
	}

	/* draw the client/factory box if all option is enabled */
	if (g->data->show_all) {
		dot_data_add_string(dd, "node_");
		dot_data_add_uint32(dd, g->id);
		dot_data_add_string(dd, " [shape=box style=filled fillcolor=white];\n");
		dot_data_add_string(dd, "node_");
		dot_data_add_uint32(dd, g->id);
		dot_data_add_string(dd, " [label=\"");
		if (client_id != GLOBAL_ID_NONE) {
			dot_data_add_string(dd, "client_id: ");
			dot_data_add_uint32(dd, client_id);
			dot_data_add_string(dd, "\\l");
		}
		if (factory_id != GLOBAL_ID_NONE) {
			dot_data_add_string(dd, "factory_id: ");
			dot_data_add_uint32(dd, factory_id);
			dot_data_add_string(dd, "\\l");
		}
		dot_data_add_string(dd, "\"];\n");
	}

	/* draw the node footer */
	dot_data_add_string(dd, "}\n");
}

static void draw_link(struct global *g)
{
	spa_assert(g != NULL);
	spa_assert(g->info != NULL);
	spa_assert(g->type == INTERFACE_Link);

	struct pw_link_info *info = g->info;
	struct dot_data *dd = &g->data->dot_data;

	/* draw the box */
	dot_data_add_string(dd, "link_");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, " [shape=box style=filled fillcolor=lightblue];\n");

	/* draw the label header */
	dot_data_add_string(dd, "link_");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, " [label=\"");

	/* draw the label body */
	dot_data_add_string(dd, "link_id: ");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, "\\loutput_node_id: ");
	dot_data_add_uint32(dd, info->output_node_id);
	dot_data_add_string(dd, "\\linput_node_id: ");
	dot_data_add_uint32(dd, info->output_node_id);
	dot_data_add_string(dd, "\\loutput_port_id: ");
	dot_data_add_uint32(dd, info->output_port_id);
	dot_data_add_string(dd, "\\linput_node_id: ");
	dot_data_add_uint32(dd, info->input_port_id);
	dot_data_add_string(dd, "\\lstate: ");
	dot_data_add_string_escaped(dd, pw_link_state_as_string(info->state));
	dot_data_add_string(dd, "\\l");
	if (g->data->show_detail)
		draw_dict(dd, "properties", info->props);

	/* draw the label footer */
	dot_data_add_string(dd, "\"];\n");

	/* draw the arrows */
	dot_data_add_string(dd, "port_");
	dot_data_add_uint32(dd, info->output_port_id);
	dot_data_add_string(dd, " -> link_");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, " -> port_");
	dot_data_add_uint32(dd, info->input_port_id);
	dot_data_add_string(dd, ";\n");
}

static void draw_client(struct global *g)
{
	spa_assert(g != NULL);
	spa_assert(g->info != NULL);
	spa_assert(g->type == INTERFACE_Client);

	struct pw_client_info *info = g->info;
	struct dot_data *dd = &g->data->dot_data;

	const char *app_name = spa_dict_lookup(info->props, PW_KEY_APP_NAME);
	const char *app_process_id = spa_dict_lookup(info->props, PW_KEY_APP_PROCESS_ID);

	/* draw the box */
	dot_data_add_string(dd, "client_");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, " [shape=box style=filled fillcolor=tan1];\n");

	/* draw the label header */
	dot_data_add_string(dd, "client_");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, " [label=\"");

	/* draw the label body */
	dot_data_add_string(dd, "client_id: ");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, "\\lname: ");
	dot_data_add_string_escaped(dd, app_name ? app_name : "(null)");
	dot_data_add_string(dd, "\\lpid: ");
	dot_data_add_string(dd, app_process_id ? app_process_id : "(null)");
	dot_data_add_string(dd, "\\l");
	if (g->data->show_detail)
		draw_dict(dd, "properties", info->props);

	/* draw the label footer */
	dot_data_add_string(dd, "\"];\n");
}

static void draw_device(struct global *g)
{
	spa_assert(g != NULL);
	spa_assert(g->info != NULL);
	spa_assert(g->type == INTERFACE_Device);

	struct pw_device_info *info = g->info;
	struct dot_data *dd = &g->data->dot_data;

	const char *app_name = spa_dict_lookup(info->props, PW_KEY_APP_NAME);
	const char *media_class = spa_dict_lookup(info->props, PW_KEY_MEDIA_CLASS);
	const char *device_api = spa_dict_lookup(info->props, PW_KEY_DEVICE_API);
	const char *path = spa_dict_lookup(info->props, PW_KEY_OBJECT_PATH);
	const char *client_id_str = spa_dict_lookup(info->props, PW_KEY_CLIENT_ID);
	const char *factory_id_str = spa_dict_lookup(info->props, PW_KEY_FACTORY_ID);
	uint32_t client_id = GLOBAL_ID_NONE, factory_id = GLOBAL_ID_NONE;

	spa_atou32(client_id_str, &client_id, 10);
	spa_atou32(factory_id_str, &factory_id, 10);

	/* draw the box */
	dot_data_add_string(dd, "device_");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, " [shape=box style=filled fillcolor=lightpink];\n");

	/* draw the label header */
	dot_data_add_string(dd, "device_");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, " [label=\"");

	/* draw the label body */
	dot_data_add_string(dd, "device_id: ");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, "\\lname: ");
	dot_data_add_string_escaped(dd, app_name ? app_name : "(null)");
	dot_data_add_string(dd, "\\lmedia_class: ");
	dot_data_add_string_escaped(dd, media_class ? media_class : "(null)");
	dot_data_add_string(dd, "\\lapi: ");
	dot_data_add_string_escaped(dd, device_api ? device_api : "(null)");
	dot_data_add_string(dd, "\\lpath: ");
	dot_data_add_string_escaped(dd, path ? path : "(null)");
	dot_data_add_string(dd, "\\l");
	if (g->data->show_detail)
		draw_dict(dd, "properties", info->props);

	/* draw the label footer */
	dot_data_add_string(dd, "\"];\n");

	/* draw the arrows */
	if (client_id != GLOBAL_ID_NONE) {
		dot_data_add_string(dd, "device_");
		dot_data_add_uint32(dd, g->id);
		dot_data_add_string(dd, " -> client_");
		dot_data_add_uint32(dd, client_id);
		dot_data_add_string(dd, " [style=dashed];\n");
	}
	if (factory_id != GLOBAL_ID_NONE) {
		dot_data_add_string(dd, "device_");
		dot_data_add_uint32(dd, g->id);
		dot_data_add_string(dd, " -> factory_");
		dot_data_add_uint32(dd, factory_id);
		dot_data_add_string(dd, " [style=dashed];\n");
	}
}

static void draw_factory(struct global *g)
{
	spa_assert(g != NULL);
	spa_assert(g->info != NULL);
	spa_assert(g->type == INTERFACE_Factory);

	struct pw_factory_info *info = g->info;
	struct dot_data *dd = &g->data->dot_data;
	const char *module_id_str = spa_dict_lookup(info->props, PW_KEY_MODULE_ID);
	uint32_t module_id = GLOBAL_ID_NONE;

	spa_atou32(module_id_str, &module_id, 10);

	/* draw the box */
	dot_data_add_string(dd, "factory_");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, " [shape=box style=filled fillcolor=lightyellow];\n");

	/* draw the label header */
	dot_data_add_string(dd, "factory_");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, " [label=\"");

	/* draw the label body */
	dot_data_add_string(dd, "factory_id: ");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, "\\lname: ");
	dot_data_add_string_escaped(dd, info->name);
	if (module_id != GLOBAL_ID_NONE) {
		dot_data_add_string(dd, "\\lmodule_id: ");
		dot_data_add_uint32(dd, module_id);
		dot_data_add_string(dd, "\\l");
	}
	if (g->data->show_detail)
		draw_dict(dd, "properties", info->props);

	/* draw the label footer */
	dot_data_add_string(dd, "\"];\n");

	/* draw the arrow */
	if (module_id != GLOBAL_ID_NONE) {
		dot_data_add_string(dd, "factory_");
		dot_data_add_uint32(dd, g->id);
		dot_data_add_string(dd, " -> module_");
		dot_data_add_uint32(dd, module_id);
		dot_data_add_string(dd, " [style=dashed];\n");
	}
}

static void draw_module(struct global *g)
{
	spa_assert(g != NULL);
	spa_assert(g->info != NULL);
	spa_assert(g->type == INTERFACE_Module);

	struct pw_module_info *info = g->info;
	struct dot_data *dd = &g->data->dot_data;

	/* draw the box */
	dot_data_add_string(dd, "module_");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, " [shape=box style=filled fillcolor=lightgrey];\n");

	/* draw the label header */
	dot_data_add_string(dd, "module_");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, " [label=\"");

	/* draw the label body */
	dot_data_add_string(dd, "module_id: ");
	dot_data_add_uint32(dd, g->id);
	dot_data_add_string(dd, "\\lname: ");
	dot_data_add_string_escaped(dd, info->name);
	dot_data_add_string(dd, "\\l");
	if (g->data->show_detail)
		draw_dict(dd, "properties", info->props);

	/* draw the label footer */
	dot_data_add_string(dd, "\"];\n");
}

static void draw_group(struct data *d, const char *group)
{
	struct spa_list *globals = &d->globals;
	struct dot_data *dd = &d->dot_data;
	static uint32_t group_id = 0;

	/* draw the group header */
	dot_data_add_string(dd, "subgraph cluster_group_");
	dot_data_add_uint32(dd, group_id++);
	dot_data_add_string(dd, "{\n");
	dot_data_add_string(dd, "bgcolor=lavender;\n");

	/* draw the label header */
	dot_data_add_string(dd, "label=\"");

	/* draw the label body */
	dot_data_add_string(dd, group);
	dot_data_add_string(dd, "\\l");

	/* draw the label footer */
	dot_data_add_string(dd, "\"\n");

	/* draw all nodes for this group */
	struct global *n;
	const char *node_link_group;
	spa_list_for_each(n, globals, link) {
		struct pw_node_info *info;
		if (n->info == NULL)
			continue;
		if (n->type != INTERFACE_Node)
			continue;
		if (n->drawn)
			continue;
		info = n->info;
		node_link_group = spa_dict_lookup(info->props, PW_KEY_NODE_LINK_GROUP);
		if (node_link_group == NULL || !spa_streq (node_link_group, group))
			continue;
		if (n->draw) {
			n->draw(n);
			n->drawn = true;
		}
	}

	/* draw the group footer */
	dot_data_add_string(dd, "}\n");

	/* draw the client/factory arrows if all option is enabled */
	if (d->show_all) {
		spa_list_for_each(n, globals, link) {
			struct pw_node_info *info;
			if (n->info == NULL)
				continue;
			if (n->type != INTERFACE_Node)
				continue;
			info = n->info;
			node_link_group = spa_dict_lookup(info->props, PW_KEY_NODE_LINK_GROUP);
			if (node_link_group == NULL || !spa_streq (node_link_group, group))
				continue;
			draw_node_arrows(n);
		}
	}
}

static bool is_node_id_link_referenced(uint32_t id, struct spa_list *globals)
{
	struct global *g;
	struct pw_link_info *info;
	spa_list_for_each(g, globals, link) {
		if (g->info == NULL)
			continue;
		if (g->type != INTERFACE_Link)
			continue;
		info = g->info;
		if (info->input_node_id == id || info->output_node_id == id)
			return true;
	}
	return false;
}

static bool is_module_id_factory_referenced(uint32_t id, struct spa_list *globals)
{
	struct global *g;
	struct pw_factory_info *info;
	const char *module_id_str;
	spa_list_for_each(g, globals, link) {
		uint32_t module_id = GLOBAL_ID_NONE;
		if (g->info == NULL)
			continue;
		if (g->type != INTERFACE_Factory)
			continue;
		info = g->info;
		module_id_str = spa_dict_lookup(info->props, PW_KEY_MODULE_ID);
		spa_atou32(module_id_str, &module_id, 10);
		if (module_id != GLOBAL_ID_NONE && module_id == id)
			return true;
	}
	return false;
}

static bool is_global_referenced(struct global *g)
{
	switch (g->type) {
	case INTERFACE_Node:
		return is_node_id_link_referenced(g->id, &g->data->globals);
	case INTERFACE_Module:
		return is_module_id_factory_referenced(g->id, &g->data->globals);
	default:
		break;
	}

	return true;
}

static int draw_graph(struct data *d, const char *path)
{
	FILE *fp;
	struct global *g;

	/* draw the header */
	dot_data_add_string(&d->dot_data, "digraph pipewire {\n");

	if (d->dot_rankdir) {
		/* set rank direction, if provided */
		dot_data_add_string(&d->dot_data, "rankdir = \"");
		dot_data_add_string(&d->dot_data, d->dot_rankdir);
		dot_data_add_string(&d->dot_data, "\";\n");
	}

	if (d->dot_orthoedges) {
		/* enable orthogonal edges */
		dot_data_add_string(&d->dot_data, "splines = ortho;\n");
	}

	/* iterate the globals */
	spa_list_for_each(g, &d->globals, link) {
		/* skip null and non-info globals */
		if (g->info == NULL)
			continue;

		/* always skip ports since they are drawn by the nodes */
		if (g->type == INTERFACE_Port)
			continue;

		/* skip clients, devices, factories and modules if all option is disabled */
		if (!d->show_all) {
			switch (g->type) {
				case INTERFACE_Client:
				case INTERFACE_Device:
				case INTERFACE_Factory:
				case INTERFACE_Module:
					continue;
				default:
					break;
			}
		}

		/* skip not referenced globals if smart option is enabled */
		if (d->show_smart && !is_global_referenced(g))
			continue;

		/* skip already drawn globals */
		if (g->drawn)
			continue;

		/* Draw groups (nodes with node.link-group property) */
		if (g->type == INTERFACE_Node) {
			struct pw_node_info *info = g->info;
			const char *group = spa_dict_lookup(info->props, PW_KEY_NODE_LINK_GROUP);
			if (group != NULL) {
				draw_group (d, group);
				continue;
			}
		}

		/* draw the global */
		if (g->draw) {
			g->draw(g);
			g->drawn = true;
		}

		/* Draw the node arrows */
		if (d->show_all && g->type == INTERFACE_Node)
			draw_node_arrows (g);
	}

	/* draw the footer */
	dot_data_add_string(&d->dot_data, "}\n");

	if (spa_streq(path, "-")) {
		/* wire the dot graph into to stdout */
		fputs(d->dot_data.data, stdout);
	} else {
		/* open the file */
		fp = fopen(path, "we");
		if (fp == NULL) {
			printf("open error: could not open %s for writing\n", path);
			return -1;
		}

		/* wire the dot graph into the file */
		fputs(d->dot_data.data, fp);
		fclose(fp);
	}
	return 0;
}

static void global_event_info(struct global *g, const void *info)
{
	if (g->info_update)
		g->info = g->info_update(g->info, info);
}

static void port_event_info(void *data, const struct pw_port_info *info)
{
	global_event_info(data, info);
}

static const struct pw_port_events port_events = {
	PW_VERSION_PORT_EVENTS,
	.info = port_event_info,
};

static void node_event_info(void *data, const struct pw_node_info *info)
{
	global_event_info(data, info);
}

static const struct pw_node_events node_events = {
	PW_VERSION_NODE_EVENTS,
	.info = node_event_info,
};

static void link_event_info(void *data, const struct pw_link_info *info)
{
	global_event_info(data, info);
}

static const struct pw_link_events link_events = {
	PW_VERSION_LINK_EVENTS,
	.info = link_event_info
};

static void client_event_info(void *data, const struct pw_client_info *info)
{
	global_event_info(data, info);
}

static const struct pw_client_events client_events = {
	PW_VERSION_CLIENT_EVENTS,
	.info = client_event_info
};

static void device_event_info(void *data, const struct pw_device_info *info)
{
	global_event_info(data, info);
}

static const struct pw_device_events device_events = {
	PW_VERSION_DEVICE_EVENTS,
	.info = device_event_info
};

static void factory_event_info(void *data, const struct pw_factory_info *info)
{
	global_event_info(data, info);
}

static const struct pw_factory_events factory_events = {
	PW_VERSION_FACTORY_EVENTS,
	.info = factory_event_info
};

static void module_event_info(void *data, const struct pw_module_info *info)
{
	global_event_info(data, info);
}

static const struct pw_module_events module_events = {
	PW_VERSION_MODULE_EVENTS,
	.info = module_event_info
};

static void removed_proxy(void *data)
{
	struct global *g = data;
	pw_proxy_destroy(g->proxy);
}

static void destroy_proxy(void *data)
{
	struct global *g = data;
	spa_hook_remove(&g->object_listener);
	spa_hook_remove(&g->proxy_listener);
}

static const struct pw_proxy_events proxy_events = {
	PW_VERSION_PROXY_EVENTS,
	.removed = removed_proxy,
	.destroy = destroy_proxy,
};

static void registry_event_global(void *data, uint32_t id, uint32_t permissions,
				  const char *type, uint32_t version,
				  const struct spa_dict *props)
{
	struct data *d = data;
	struct pw_proxy *proxy;
	uint32_t client_version;
	uint32_t object_type;
	const void *events;
	pw_destroy_t info_destroy;
	info_update_t info_update;
	draw_t draw;
	struct global *g;

	if (spa_streq(type, PW_TYPE_INTERFACE_Port)) {
		events = &port_events;
		info_destroy = (pw_destroy_t)pw_port_info_free;
		info_update = (info_update_t)pw_port_info_update;
		draw = draw_port;
		client_version = PW_VERSION_PORT;
		object_type = INTERFACE_Port;
	}
	else if (spa_streq(type, PW_TYPE_INTERFACE_Node)) {
		events = &node_events;
		info_destroy = (pw_destroy_t)pw_node_info_free;
		info_update = (info_update_t)pw_node_info_update;
		draw = draw_node;
		client_version = PW_VERSION_NODE;
		object_type = INTERFACE_Node;
	}
	else if (spa_streq(type, PW_TYPE_INTERFACE_Link)) {
		events = &link_events;
		info_destroy = (pw_destroy_t)pw_link_info_free;
		info_update = (info_update_t)pw_link_info_update;
		draw = draw_link;
		client_version = PW_VERSION_LINK;
		object_type = INTERFACE_Link;
	}
	else if (spa_streq(type, PW_TYPE_INTERFACE_Client)) {
		events = &client_events;
		info_destroy = (pw_destroy_t)pw_client_info_free;
		info_update = (info_update_t)pw_client_info_update;
		draw = draw_client;
		client_version = PW_VERSION_CLIENT;
		object_type = INTERFACE_Client;
	}
	else if (spa_streq(type, PW_TYPE_INTERFACE_Device)) {
		events = &device_events;
		info_destroy = (pw_destroy_t)pw_device_info_free;
		info_update = (info_update_t)pw_device_info_update;
		draw = draw_device;
		client_version = PW_VERSION_DEVICE;
		object_type = INTERFACE_Device;
	}
	else if (spa_streq(type, PW_TYPE_INTERFACE_Factory)) {
		events = &factory_events;
		info_destroy = (pw_destroy_t)pw_factory_info_free;
		info_update = (info_update_t)pw_factory_info_update;
		draw = draw_factory;
		client_version = PW_VERSION_FACTORY;
		object_type = INTERFACE_Factory;
	}
	else if (spa_streq(type, PW_TYPE_INTERFACE_Module)) {
		events = &module_events;
		info_destroy = (pw_destroy_t)pw_module_info_free;
		info_update = (info_update_t)pw_module_info_update;
		draw = draw_module;
		client_version = PW_VERSION_MODULE;
		object_type = INTERFACE_Module;
	}
	else if (spa_streq(type, PW_TYPE_INTERFACE_Core)) {
		/* sync to notify we are done with globals */
		pw_core_sync(d->core, 0, 0);
		return;
	}
	else {
		return;
	}

	proxy = pw_registry_bind(d->registry, id, type, client_version, 0);
	if (proxy == NULL)
		return;

	/* set the global data */
	g = calloc(1, sizeof(struct global));
	g->data = d;
	g->proxy = proxy;

	g->id = id;
	g->type = object_type;
	g->info = NULL;

	g->info_destroy = info_destroy;
	g->info_update = info_update;
	g->draw = draw;

	pw_proxy_add_object_listener(proxy, &g->object_listener, events, g);
	pw_proxy_add_listener(proxy, &g->proxy_listener, &proxy_events, g);

	/* add the global to the list */
	spa_list_insert(&d->globals, &g->link);
}

static const struct pw_registry_events registry_events = {
	PW_VERSION_REGISTRY_EVENTS,
	.global = registry_event_global,
};

static void on_core_done(void *data, uint32_t id, int seq)
{
	struct data *d = data;
	pw_main_loop_quit(d->loop);
}

static void on_core_error(void *data, uint32_t id, int seq, int res, const char *message)
{
	struct data *d = data;

	pw_log_error("error id:%u seq:%d res:%d (%s): %s",
			id, seq, res, spa_strerror(res), message);

	if (id == PW_ID_CORE && res == -EPIPE)
		pw_main_loop_quit(d->loop);
}

static const struct pw_core_events core_events = {
	PW_VERSION_CORE_EVENTS,
	.done = on_core_done,
	.error = on_core_error,
};

static void do_quit(void *data, int signal_number)
{
	struct data *d = data;
	pw_main_loop_quit(d->loop);
}

static int get_data_from_pipewire(struct data *data, const char *opt_remote)
{
	struct pw_loop *l;
	struct global *g;

	data->loop = pw_main_loop_new(NULL);
	if (data->loop == NULL) {
		fprintf(stderr, "can't create main loop: %m\n");
		return -1;
	}

	l = pw_main_loop_get_loop(data->loop);
	pw_loop_add_signal(l, SIGINT, do_quit, &data);
	pw_loop_add_signal(l, SIGTERM, do_quit, &data);

	data->context = pw_context_new(l, NULL, 0);
	if (data->context == NULL) {
		fprintf(stderr, "can't create context: %m\n");
		pw_main_loop_destroy(data->loop);
		return -1;
	}

	data->core = pw_context_connect(data->context,
			pw_properties_new(
				PW_KEY_REMOTE_NAME, opt_remote,
				NULL),
			0);
	if (data->core == NULL) {
		fprintf(stderr, "can't connect: %m\n");
		pw_context_destroy(data->context);
		pw_main_loop_destroy(data->loop);
		return -1;
	}

	pw_core_add_listener(data->core,
			     &data->core_listener,
			     &core_events, data);

	data->registry = pw_core_get_registry(data->core,
					      PW_VERSION_REGISTRY, 0);
	pw_registry_add_listener(data->registry,
				 &data->registry_listener,
				 &registry_events, data);

	pw_main_loop_run(data->loop);

	spa_hook_remove(&data->registry_listener);
	pw_proxy_destroy((struct pw_proxy*)data->registry);
	spa_list_for_each(g, &data->globals, link)
		pw_proxy_destroy(g->proxy);
	spa_hook_remove(&data->core_listener);
	pw_context_destroy(data->context);
	pw_main_loop_destroy(data->loop);

	return 0;
}

static void handle_json_obj(struct data *data, struct pw_properties *obj)
{
	struct global *g;
	struct pw_properties *info, *props;
	const char *str;

	str = pw_properties_get(obj, "type");
	if (!str) {
		fprintf(stderr, "invalid object without type\n");
		return;
	}

	g = calloc(1, sizeof (struct global));
	g->data = data;
	g->draw = false;

	if (spa_streq(str, PW_TYPE_INTERFACE_Port)) {
		g->info_destroy = (pw_destroy_t)pw_port_info_free;
		g->draw = draw_port;
		g->type = INTERFACE_Port;
	}
	else if (spa_streq(str, PW_TYPE_INTERFACE_Node)) {
		g->info_destroy = (pw_destroy_t)pw_node_info_free;
		g->draw = draw_node;
		g->type = INTERFACE_Node;
	}
	else if (spa_streq(str, PW_TYPE_INTERFACE_Link)) {
		g->info_destroy = (pw_destroy_t)pw_link_info_free;
		g->draw = draw_link;
		g->type = INTERFACE_Link;
	}
	else if (spa_streq(str, PW_TYPE_INTERFACE_Client)) {
		g->info_destroy = (pw_destroy_t)pw_client_info_free;
		g->draw = draw_client;
		g->type = INTERFACE_Client;
	}
	else if (spa_streq(str, PW_TYPE_INTERFACE_Device)) {
		g->info_destroy = (pw_destroy_t)pw_device_info_free;
		g->draw = draw_device;
		g->type = INTERFACE_Device;
	}
	else if (spa_streq(str, PW_TYPE_INTERFACE_Factory)) {
		g->info_destroy = (pw_destroy_t)pw_factory_info_free;
		g->draw = draw_factory;
		g->type = INTERFACE_Factory;
	}
	else if (spa_streq(str, PW_TYPE_INTERFACE_Module)) {
		g->info_destroy = (pw_destroy_t)pw_module_info_free;
		g->draw = draw_module;
		g->type = INTERFACE_Module;
	}
	else {
		free(g);
		return;
	}

	g->id = pw_properties_get_uint32(obj, "id", 0);

	str = pw_properties_get(obj, "info");
	info = pw_properties_new_string(str);

	str = pw_properties_get(info, "props");
	props = str ? pw_properties_new_string(str) : NULL;

	switch (g->type) {
		case INTERFACE_Port: {
			struct pw_port_info pinfo = {0};
			pinfo.id = g->id;
			str = pw_properties_get(info, "direction");
			pinfo.direction = spa_streq(str, "output") ?
				PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT;
			pinfo.props = props ? &props->dict : NULL;
			pinfo.change_mask = PW_PORT_CHANGE_MASK_PROPS;
			g->info = pw_port_info_update(NULL, &pinfo);
			break;
		}
		case INTERFACE_Node: {
			struct pw_node_info ninfo = {0};
			ninfo.id = g->id;
			ninfo.max_input_ports =
				pw_properties_get_uint32(info, "max-input-ports", 0);
			ninfo.max_output_ports =
				pw_properties_get_uint32(info, "max-output-ports", 0);
			ninfo.n_input_ports =
				pw_properties_get_uint32(info, "n-input-ports", 0);
			ninfo.n_output_ports =
				pw_properties_get_uint32(info, "n-output-ports", 0);

			str = pw_properties_get(info, "state");
			if (spa_streq(str, "running"))
				ninfo.state = PW_NODE_STATE_RUNNING;
			else if (spa_streq(str, "idle"))
				ninfo.state = PW_NODE_STATE_IDLE;
			else if (spa_streq(str, "suspended"))
				ninfo.state = PW_NODE_STATE_SUSPENDED;
			else if (spa_streq(str, "creating"))
				ninfo.state = PW_NODE_STATE_CREATING;
			else
				ninfo.state = PW_NODE_STATE_ERROR;
			ninfo.error = pw_properties_get(info, "error");

			ninfo.props = props ? &props->dict : NULL;
			ninfo.change_mask = PW_NODE_CHANGE_MASK_INPUT_PORTS |
					    PW_NODE_CHANGE_MASK_OUTPUT_PORTS |
					    PW_NODE_CHANGE_MASK_STATE |
					    PW_NODE_CHANGE_MASK_PROPS;
			g->info = pw_node_info_update(NULL, &ninfo);
			break;
		}
		case INTERFACE_Link: {
			struct pw_link_info linfo = {0};
			linfo.id = g->id;
			linfo.output_node_id =
				pw_properties_get_uint32(info, "output-node-id", 0);
			linfo.output_port_id =
				pw_properties_get_uint32(info, "output-port-id", 0);
			linfo.input_node_id =
				pw_properties_get_uint32(info, "input-node-id", 0);
			linfo.input_port_id =
				pw_properties_get_uint32(info, "input-port-id", 0);

			str = pw_properties_get(info, "state");
			if (spa_streq(str, "active"))
				linfo.state = PW_LINK_STATE_ACTIVE;
			else if (spa_streq(str, "paused"))
				linfo.state = PW_LINK_STATE_PAUSED;
			else if (spa_streq(str, "allocating"))
				linfo.state = PW_LINK_STATE_ALLOCATING;
			else if (spa_streq(str, "negotiating"))
				linfo.state = PW_LINK_STATE_NEGOTIATING;
			else if (spa_streq(str, "init"))
				linfo.state = PW_LINK_STATE_INIT;
			else if (spa_streq(str, "unlinked"))
				linfo.state = PW_LINK_STATE_UNLINKED;
			else
				linfo.state = PW_LINK_STATE_ERROR;
			linfo.error = pw_properties_get(info, "error");

			linfo.props = props ? &props->dict : NULL;
			linfo.change_mask = PW_LINK_CHANGE_MASK_STATE |
					    PW_LINK_CHANGE_MASK_PROPS;
			g->info = pw_link_info_update(NULL, &linfo);
			break;
		}
		case INTERFACE_Client: {
			struct pw_client_info cinfo = {0};
			cinfo.id = g->id;
			cinfo.props = props ? &props->dict : NULL;
			cinfo.change_mask = PW_CLIENT_CHANGE_MASK_PROPS;
			g->info = pw_client_info_update(NULL, &cinfo);
			break;
		}
		case INTERFACE_Device: {
			struct pw_device_info dinfo = {0};
			dinfo.id = g->id;
			dinfo.props = props ? &props->dict : NULL;
			dinfo.change_mask = PW_DEVICE_CHANGE_MASK_PROPS;
			g->info = pw_device_info_update(NULL, &dinfo);
			break;
		}
		case INTERFACE_Factory: {
			struct pw_factory_info finfo = {0};
			finfo.id = g->id;
			finfo.name = pw_properties_get(info, "name");
			finfo.type = pw_properties_get(info, "type");
			finfo.version = pw_properties_get_uint32(info, "version", 0);
			finfo.props = props ? &props->dict : NULL;
			finfo.change_mask = PW_FACTORY_CHANGE_MASK_PROPS;
			g->info = pw_factory_info_update(NULL, &finfo);
			break;
		}
		case INTERFACE_Module: {
			struct pw_module_info minfo = {0};
			minfo.id = g->id;
			minfo.name = pw_properties_get(info, "name");
			minfo.filename = pw_properties_get(info, "filename");
			minfo.args = pw_properties_get(info, "args");
			minfo.props = props ? &props->dict : NULL;
			minfo.change_mask = PW_MODULE_CHANGE_MASK_PROPS;
			g->info = pw_module_info_update(NULL, &minfo);
			break;
		}
		default:
			break;
	}

	pw_properties_free(info);
	pw_properties_free(props);

	/* add the global to the list */
	spa_list_insert(&data->globals, &g->link);
}

static int get_data_from_json(struct data *data, const char *json_path)
{
	int fd, len;
	void *json;
	struct stat sbuf;
	struct spa_json it[2];
	const char *value;
	struct spa_error_location loc;

	if ((fd = open(json_path,  O_CLOEXEC | O_RDONLY)) < 0) {
		fprintf(stderr, "error opening file '%s': %m\n", json_path);
		return -1;
	}
	if (fstat(fd, &sbuf) < 0) {
		fprintf(stderr, "error statting file '%s': %m\n", json_path);
		close(fd);
		return -1;
	}
	if ((json = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
		fprintf(stderr, "error mmapping file '%s': %m\n", json_path);
		close(fd);
		return -1;
	}

	close(fd);
	spa_json_init(&it[0], json, sbuf.st_size);

	if (spa_json_enter_array(&it[0], &it[1]) <= 0) {
		fprintf(stderr, "expected top-level array in JSON file '%s'\n", json_path);
		munmap(json, sbuf.st_size);
		return -1;
	}

	while ((len = spa_json_next(&it[1], &value)) > 0 && spa_json_is_object(value, len)) {
		struct pw_properties *obj;
		obj = pw_properties_new(NULL, NULL);
		len = spa_json_container_len(&it[1], value, len);
		pw_properties_update_string(obj, value, len);
		handle_json_obj(data, obj);
		pw_properties_free(obj);
	}

	munmap(json, sbuf.st_size);

	if (spa_json_get_error(&it[0], json, &loc)) {
		spa_debug_file_error_location(stderr, &loc,
				"JSON syntax error: %s\n", loc.reason);
		return -1;
	}

	return 0;
}

static void show_help(const char *name, bool error)
{
	fprintf(error ? stderr : stdout, "%s [options]\n"
		"  -h, --help                            Show this help\n"
		"      --version                         Show version\n"
		"  -a, --all                             Show all object types\n"
		"  -s, --smart                           Show linked objects only\n"
		"  -d, --detail                          Show all object properties\n"
		"  -r, --remote                          Remote daemon name\n"
		"  -o, --output                          Output file (Default %s)\n"
		"  -L, --lr                              Use left-right rank direction\n"
		"  -9, --90                              Use orthogonal edges\n"
		"  -j, --json                            Read objects from pw-dump JSON file\n",
		name,
		DEFAULT_DOT_PATH);
}

int main(int argc, char *argv[])
{
	struct data data = { 0 };
	struct global *g;
	const char *opt_remote = NULL;
	const char *dot_path = DEFAULT_DOT_PATH;
	const char *json_path = NULL;
	static const struct option long_options[] = {
		{ "help",	no_argument,		NULL, 'h' },
		{ "version",	no_argument,		NULL, 'V' },
		{ "all",	no_argument,		NULL, 'a' },
		{ "smart",	no_argument,		NULL, 's' },
		{ "detail",	no_argument,		NULL, 'd' },
		{ "remote",	required_argument,	NULL, 'r' },
		{ "output",	required_argument,	NULL, 'o' },
		{ "lr",		no_argument,		NULL, 'L' },
		{ "90",		no_argument,		NULL, '9' },
		{ "json",	required_argument,	NULL, 'j' },
		{ NULL, 0, NULL, 0}
	};
	int c;

	setlocale(LC_ALL, "");
	pw_init(&argc, &argv);

	while ((c = getopt_long(argc, argv, "hVasdr:o:L9j:", long_options, NULL)) != -1) {
		switch (c) {
		case 'h' :
			show_help(argv[0], false);
			return 0;
		case 'V' :
			printf("%s\n"
				"Compiled with libpipewire %s\n"
				"Linked with libpipewire %s\n",
				argv[0],
				pw_get_headers_version(),
				pw_get_library_version());
			return 0;
		case 'a' :
			data.show_all = true;
			fprintf(stderr, "all option enabled\n");
			break;
		case 's' :
			data.show_smart = true;
			fprintf(stderr, "smart option enabled\n");
			break;
		case 'd' :
			data.show_detail = true;
			fprintf(stderr, "detail option enabled\n");
			break;
		case 'r' :
			opt_remote = optarg;
			fprintf(stderr, "set remote to %s\n", opt_remote);
			break;
		case 'o' :
			dot_path = optarg;
			fprintf(stderr, "set output file %s\n", dot_path);
			break;
		case 'L' :
			data.dot_rankdir = "LR";
			fprintf(stderr, "set rank direction to LR\n");
			break;
		case '9' :
			data.dot_orthoedges = true;
			fprintf(stderr, "orthogonal edges enabled\n");
			break;
		case 'j' :
			json_path = optarg;
			fprintf(stderr, "Using JSON file %s as input\n", json_path);
			break;
		default:
			show_help(argv[0], true);
			return -1;
		}
	}

	if (!dot_data_init(&data.dot_data, DEFAULT_DOT_DATA_SIZE))
		return -1;

	spa_list_init(&data.globals);

	if (!json_path && get_data_from_pipewire(&data, opt_remote) < 0)
		return -1;
	else if (json_path && get_data_from_json(&data, json_path) < 0)
		return -1;

	draw_graph(&data, dot_path);

	dot_data_clear(&data.dot_data);
	spa_list_consume(g, &data.globals, link) {
		if (g->info && g->info_destroy)
			g->info_destroy(g->info);
		spa_list_remove(&g->link);
		free(g);
	}
	pw_deinit();

	return 0;
}
