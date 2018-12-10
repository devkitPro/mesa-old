#include <stdint.h>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "pipe/p_screen.h"
#include "util/u_format.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_hash_table.h"
#include "os/os_thread.h"

#include "state_tracker/drm_driver.h"
#include "drm-uapi/drm_fourcc.h"

#include "nouveau_switch_public.h"
#include "nouveau_winsys.h"
#include "nouveau_screen.h"
#include "nouveau_buffer.h"
#include "nouveau_drm.h"

#include <nvif/class.h>
#include <nvif/cl0080.h>

static mtx_t nouveau_screen_mutex = _MTX_INITIALIZER_NP;

bool nouveau_drm_screen_unref(struct nouveau_screen *screen)
{
	int ret;
	if (screen->refcount == -1)
		return true;

	mtx_lock(&nouveau_screen_mutex);
	ret = --screen->refcount;
	assert(ret >= 0);
	mtx_unlock(&nouveau_screen_mutex);
	return ret == 0;
}

PUBLIC struct pipe_screen *
nouveau_switch_screen_create(void)
{
	struct nouveau_drm *drm = NULL;
	struct nouveau_device *dev = NULL;
	struct nouveau_screen *(*init)(struct nouveau_device *);
	struct nouveau_screen *screen = NULL;
	int ret;

	mtx_lock(&nouveau_screen_mutex);

	ret = nouveau_drm_new(0, &drm);
	if (ret)
		goto err;

	ret = nouveau_device_new(&drm->client, NV_DEVICE,
				 &(struct nv_device_v0) {
					.device = ~0ULL,
				 }, sizeof(struct nv_device_v0), &dev);
	if (ret)
		goto err;

	switch (dev->chipset & ~0xf) {
#if 0
	case 0x30:
	case 0x40:
	case 0x60:
		init = nv30_screen_create;
		break;
	case 0x50:
	case 0x80:
	case 0x90:
	case 0xa0:
		init = nv50_screen_create;
		break;
#endif
	case 0xc0:
	case 0xd0:
	case 0xe0:
	case 0xf0:
	case 0x100:
	case 0x110:
	case 0x120:
	case 0x130:
		init = nvc0_screen_create;
		break;
	default:
		debug_printf("%s: unknown chipset nv%02x\n", __func__,
			     dev->chipset);
		goto err;
	}

	screen = init(dev);
	if (!screen || !screen->base.context_create)
		goto err;

	screen->refcount = 1;
	mtx_unlock(&nouveau_screen_mutex);
	return &screen->base;

err:
	if (screen) {
		screen->base.destroy(&screen->base);
	} else {
		nouveau_device_del(&dev);
		nouveau_drm_del(&drm);
	}
	mtx_unlock(&nouveau_screen_mutex);
	return NULL;
}

PUBLIC int
nouveau_switch_resource_get_syncpoint(struct pipe_resource *resource, unsigned int *out_threshold)
{
	struct nv04_resource* priv = nv04_resource(resource);
	return nouveau_bo_get_syncpoint(priv->bo, out_threshold);
}

PUBLIC int
nouveau_switch_resource_get_buffer(struct pipe_screen *screen, struct pipe_resource *resource, NvGraphicBuffer *buffer)
{
	struct winsys_handle whandle = {0};

	if ((resource->target != PIPE_TEXTURE_2D && resource->target != PIPE_TEXTURE_RECT) || resource->last_level != 0 || resource->depth0 != 1 || resource->array_size > 1) {
		debug_printf("%s: unsupported resource type\n", __func__);
		return -1;
	}

	whandle.type = WINSYS_HANDLE_TYPE_SHARED;
	if (!screen->resource_get_handle(screen, NULL, resource, &whandle, 0)) {
		debug_printf("%s: resource_get_handle failed\n", __func__);
		return -2;
	}

	u32 block_height_log2;
	switch (whandle.modifier) {
		case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK_ONE_GOB:
			block_height_log2 = 0;
			break;
		case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK_TWO_GOB:
			block_height_log2 = 1;
			break;
		case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK_FOUR_GOB:
			block_height_log2 = 2;
			break;
		case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK_EIGHT_GOB:
			block_height_log2 = 3;
			break;
		case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK_SIXTEEN_GOB:
			block_height_log2 = 4;
			break;
		case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK_THIRTYTWO_GOB:
			block_height_log2 = 5;
			break;
		default:
			debug_printf("%s: unsupported resource layout\n", __func__);
			return -3;
	}

	u32 format;
	NvColorFormat colorfmt;
	switch (resource->format) {
		case PIPE_FORMAT_R8G8B8A8_UNORM:
			format = PIXEL_FORMAT_RGBA_8888;
			colorfmt = NvColorFormat_A8B8G8R8;
			break;
		case PIPE_FORMAT_R8G8B8X8_UNORM:
			format = PIXEL_FORMAT_RGBX_8888;
			colorfmt = NvColorFormat_X8B8G8R8;
			break;
		case PIPE_FORMAT_B5G6R5_UNORM:
			format = PIXEL_FORMAT_RGB_565;
			colorfmt = NvColorFormat_R5G6B5;
			break;
		default:
			debug_printf("%s: unsupported resource format\n", __func__);
			return -4;
	}

	const u32 bytes_per_pixel = ((u64)colorfmt >> 3) & 0x1F;
	const u32 block_height = 8 * (1U << block_height_log2);
	const u32 width_aligned = whandle.stride / bytes_per_pixel;
	const u32 height_aligned = (resource->height0 + block_height - 1) &~ (block_height - 1);
	const u32 fb_size = whandle.stride*height_aligned;

	memset(buffer, 0, sizeof(*buffer));
	buffer->header.num_ints = (sizeof(NvGraphicBuffer) - sizeof(NativeHandle)) / 4;
	buffer->unk0 = -1;
	buffer->nvmap_id = whandle.handle;
	buffer->magic = 0xDAFFCAFF;
	buffer->pid = 42;
	buffer->usage = GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE;
	buffer->format = format;
	buffer->ext_format = format;
	buffer->stride = width_aligned;
	buffer->total_size = fb_size;
	buffer->num_planes = 1;
	buffer->planes[0].width = resource->width0;
	buffer->planes[0].height = resource->height0;
	buffer->planes[0].color_format = colorfmt;
	buffer->planes[0].layout = NvLayout_BlockLinear;
	buffer->planes[0].pitch = whandle.stride;
	buffer->planes[0].offset = whandle.offset;
	buffer->planes[0].kind = NvKind_Generic_16BX2;
	buffer->planes[0].block_height_log2 = block_height_log2;
	buffer->planes[0].size = fb_size;

	return 0;
}
