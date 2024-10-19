/* Spa vulkan plugin */
/* SPDX-FileCopyrightText: Copyright © 2019 Wim Taymans */
/* SPDX-License-Identifier: MIT */

#include <errno.h>

#include <spa/support/plugin.h>
#include <spa/support/log.h>

extern const struct spa_handle_factory spa_vulkan_compute_filter_factory;
extern const struct spa_handle_factory spa_vulkan_compute_source_factory;
extern const struct spa_handle_factory spa_vulkan_blit_filter_factory;
extern const struct spa_handle_factory spa_vulkan_blit_dsp_filter_factory;

SPA_LOG_TOPIC_ENUM_DEFINE_REGISTERED;

SPA_EXPORT
int spa_handle_factory_enum(const struct spa_handle_factory **factory, uint32_t *index)
{
	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(index != NULL, -EINVAL);

	switch (*index) {
	case 0:
		*factory = &spa_vulkan_compute_source_factory;
		break;
	case 1:
		*factory = &spa_vulkan_compute_filter_factory;
		break;
	case 2:
		*factory = &spa_vulkan_blit_filter_factory;
		break;
	case 3:
		*factory = &spa_vulkan_blit_dsp_filter_factory;
		break;
	default:
		return 0;
	}
	(*index)++;
	return 1;
}
