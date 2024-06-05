/* Spa */
/* SPDX-FileCopyrightText: Copyright © 2019 Wim Taymans */
/* SPDX-License-Identifier: MIT */

#pragma once

#include <vulkan/vulkan.h>

#include <spa/buffer/buffer.h>
#include <spa/node/node.h>

#include "vulkan-types.h"

#define VK_CHECK_RESULT(f)								\
{											\
	VkResult _result = (f);								\
	int _r = -vulkan_vkresult_to_errno(_result);						\
	if (_result != VK_SUCCESS) {							\
		spa_log_error(s->log, "error: %d (%d %s)", _result, _r, spa_strerror(_r));	\
		return _r;								\
	}										\
}
#define VK_CHECK_RESULT_WITH_CLEANUP(f, c)						\
{											\
	VkResult _result = (f);								\
	int _r = -vkresult_to_errno(_result);						\
	if (_result != VK_SUCCESS) {							\
		spa_log_error(s->log, "error: %d (%d %s)", _result, _r, spa_strerror(_r));	\
		(c);									\
		return _r;								\
	}										\
}
#define VK_CHECK_RESULT_LOOP(f)								\
{											\
	VkResult _result = (f);								\
	int _r = -vkresult_to_errno(_result);						\
	if (_result != VK_SUCCESS) {							\
		spa_log_error(s->log, "error: %d (%d %s)", _result, _r, spa_strerror(_r));	\
		continue;								\
	}										\
}
#define CHECK(f)									\
{											\
	int _res = (f);									\
	if (_res < 0) 									\
		return _res;								\
}

struct vulkan_write_pixels_info {
	struct spa_rectangle size;
	uint32_t offset;
	uint32_t stride;
	uint32_t bytes_per_pixel;

	VkBufferImageCopy *copies;
	void *data;
};

struct vulkan_read_pixels_info {
	struct spa_rectangle size;
	void *data;
	uint32_t offset;
	uint32_t stride;
	uint32_t bytes_per_pixel;
};

struct dmabuf_fixation_info {
	VkFormat format;
	uint64_t modifierCount;
	uint64_t *modifiers;
	struct spa_rectangle size;
	VkImageUsageFlags usage;
};

struct external_buffer_info {
	VkFormat format;
	uint64_t modifier;
	struct spa_rectangle size;
	VkImageUsageFlags usage;
	struct spa_buffer *spa_buf;
};

int vulkan_write_pixels(struct vulkan_base *s, struct vulkan_write_pixels_info *info, struct vulkan_staging_buffer *vk_sbuf);
int vulkan_read_pixels(struct vulkan_base *s, struct vulkan_read_pixels_info *info, struct vulkan_buffer *vk_buf);

int vulkan_sync_foreign_dmabuf(struct vulkan_base *s, struct vulkan_buffer *vk_buf);
bool vulkan_sync_export_dmabuf(struct vulkan_base *s, struct vulkan_buffer *vk_buf, int sync_file_fd);

int vulkan_staging_buffer_create(struct vulkan_base *s, uint32_t size, struct vulkan_staging_buffer *s_buf);
void vulkan_staging_buffer_destroy(struct vulkan_base *s, struct vulkan_staging_buffer *s_buf);

int vulkan_validate_dmabuf_properties(const struct vulkan_modifier_info *modInfo, uint32_t *planeCount, struct spa_rectangle *dim);
int vulkan_fixate_modifier(struct vulkan_base *s, struct dmabuf_fixation_info *info, uint64_t *modifier);
int vulkan_create_dmabuf(struct vulkan_base *s, struct external_buffer_info *info, struct vulkan_buffer *vk_buf);
int vulkan_import_dmabuf(struct vulkan_base *s, struct external_buffer_info *info, struct vulkan_buffer *vk_buf);
int vulkan_import_memptr(struct vulkan_base *s, struct external_buffer_info *info, struct vulkan_buffer *vk_buf);

int vulkan_fence_create(struct vulkan_base *s, VkFence *fence);
int vulkan_commandPool_create(struct vulkan_base *s, VkCommandPool *commandPool);
int vulkan_commandBuffer_create(struct vulkan_base *s, VkCommandPool commandPool, VkCommandBuffer *commandBuffer);

uint32_t vulkan_memoryType_find(struct vulkan_base *s,
		uint32_t memoryTypeBits, VkMemoryPropertyFlags properties);
struct vulkan_format_info *vulkan_formatInfo_find(struct vulkan_format_infos *fmtInfo, VkFormat format);
struct vulkan_modifier_info *vulkan_modifierInfo_find(struct vulkan_format_infos *fmtInfo, VkFormat format, uint64_t modifier);

int vulkan_buffer_get_implicit_syncfd(struct vulkan_base *s, struct vulkan_buffer *vk_buf);
bool vulkan_buffer_set_implicit_syncfd(struct vulkan_base *s, struct vulkan_buffer *vk_buf, int sync_file_fd);
int vulkan_buffer_import_implicit_syncfd(struct vulkan_base *s, struct vulkan_buffer *vk_buf);
int vulkan_buffer_wait_dmabuf_fence(struct vulkan_base *s, struct vulkan_buffer *vk_buf);
int vulkan_buffer_import_syncfd(struct vulkan_base *s, struct vulkan_buffer *vk_buf, int sync_file_fd);
void vulkan_buffer_clear(struct vulkan_base *s, struct vulkan_buffer *buffer);

uint32_t vulkan_vkformat_to_id(VkFormat vkFormat);
VkFormat vulkan_id_to_vkformat(uint32_t id);

int vulkan_vkresult_to_errno(VkResult result);

int vulkan_wait_fence(struct vulkan_base *s, VkFence fence);
int vulkan_wait_idle(struct vulkan_base *s);

int vulkan_format_infos_init(struct vulkan_base *s, uint32_t formatCount, uint32_t *formats,
		struct vulkan_format_infos *info);
void vulkan_format_infos_deinit(struct vulkan_format_infos *info);
int vulkan_base_init(struct vulkan_base *s, struct vulkan_base_info *info);
void vulkan_base_deinit(struct vulkan_base *s);
