/* Spa ALSA support */
/* SPDX-FileCopyrightText: Copyright © 2018 Wim Taymans */
/* SPDX-License-Identifier: MIT */

#include "config.h"

#include <errno.h>

#include <spa/support/plugin.h>
#include <spa/support/log.h>

#include "alsa.h"

extern const struct spa_handle_factory spa_alsa_source_factory;
extern const struct spa_handle_factory spa_alsa_sink_factory;
extern const struct spa_handle_factory spa_alsa_udev_factory;
extern const struct spa_handle_factory spa_alsa_pcm_device_factory;
extern const struct spa_handle_factory spa_alsa_seq_bridge_factory;
extern const struct spa_handle_factory spa_alsa_acp_device_factory;
#ifdef HAVE_ALSA_COMPRESS_OFFLOAD
extern const struct spa_handle_factory spa_alsa_compress_offload_sink_factory;
extern const struct spa_handle_factory spa_alsa_compress_offload_device_factory;
#endif

SPA_LOG_TOPIC_DEFINE(alsa_log_topic, "spa.alsa");

SPA_LOG_TOPIC_ENUM_DEFINE_REGISTERED;

SPA_EXPORT
int spa_handle_factory_enum(const struct spa_handle_factory **factory, uint32_t *index)
{
	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(index != NULL, -EINVAL);

	switch (*index) {
	case 0:
		*factory = &spa_alsa_source_factory;
		break;
	case 1:
		*factory = &spa_alsa_sink_factory;
		break;
	case 2:
#ifdef HAVE_LIBUDEV
		*factory = &spa_alsa_udev_factory;
		break;
#else
		(*index)++;
		SPA_FALLTHROUGH;
#endif
	case 3:
		*factory = &spa_alsa_pcm_device_factory;
		break;
	case 4:
		*factory = &spa_alsa_seq_bridge_factory;
		break;
	case 5:
		*factory = &spa_alsa_acp_device_factory;
		break;
#ifdef HAVE_ALSA_COMPRESS_OFFLOAD
	case 6:
		*factory = &spa_alsa_compress_offload_sink_factory;
		break;
	case 7:
		*factory = &spa_alsa_compress_offload_device_factory;
		break;
#endif
	default:
		return 0;
	}
	(*index)++;
	return 1;
}
