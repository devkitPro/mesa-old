/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>

#include "libdrm_lists.h"
#include "nouveau_debug.h"
#include "nouveau_drm.h"
#include "nouveau.h"
#include "private.h"

#include "nvif/class.h"
#include "nvif/cl0080.h"
#include "nvif/ioctl.h"
#include "nvif/unpack.h"

#include "os/os_misc.h"

#include <switch.h>


#ifdef DEBUG
#	define TRACE(x...) NOUVEAU_DBG(MISC, "nouveau: " x)
#	define CALLED() TRACE("CALLED: %s\n", __PRETTY_FUNCTION__)
#else
#	define TRACE(x...)
# define CALLED()
#endif
#define ERROR(x...) NOUVEAU_ERR("nouveau: " x)

/* Unused
int
nouveau_object_mthd(struct nouveau_object *obj,
		    uint32_t mthd, void *data, uint32_t size)
{
	return 0;
}
*/

/* Unused
void
nouveau_object_sclass_put(struct nouveau_sclass **psclass)
{
}
*/

/* Unused
int
nouveau_object_sclass_get(struct nouveau_object *obj,
			  struct nouveau_sclass **psclass)
{
	return 0;
}
*/

int
nouveau_object_mclass(struct nouveau_object *obj,
		      const struct nouveau_mclass *mclass)
{
  // TODO: Only used for VP3 firmware upload
	CALLED();
	return 0;
}

/* NVGPU_IOCTL_CHANNEL_ALLOC_OBJ_CTX */
int
nouveau_object_new(struct nouveau_object *parent, uint64_t handle,
		   uint32_t oclass, void *data, uint32_t length,
		   struct nouveau_object **pobj)
{
	struct nouveau_drm *drm = nouveau_drm(parent);
	struct nouveau_object *obj;
	Result rc;
	CALLED();

	if (!(obj = calloc(1, sizeof(*obj))))
		return -ENOMEM;

	if (oclass == NOUVEAU_FIFO_CHANNEL_CLASS) {
		struct nouveau_fifo *fifo;
		if (!(fifo = calloc(1, sizeof(*fifo)))) {
			free(obj);
			return -ENOMEM;
		}
		fifo->object = parent;
		fifo->channel = drm->nvhostgpu;
		fifo->pushbuf = 0;
		obj->data = fifo;
		obj->length = sizeof(*fifo);
	} else if (oclass == MAXWELL_B) {
		rc = nvioctlChannel_AllocObjCtx(drm->nvhostgpu, oclass, 0, &obj->handle);
		if (R_FAILED(rc)) {
			free(obj);
			return -errno;
		}
	}

	obj->parent = parent;
	obj->oclass = oclass;
	*pobj = obj;
	return 0;
}

/* NVGPU_IOCTL_CHANNEL_FREE_OBJ_CTX */
void
nouveau_object_del(struct nouveau_object **pobj)
{
	CALLED();
	if (!pobj)
		return;

	struct nouveau_object *obj = *pobj;
	if (!obj)
		return;

	if (obj->data)
		free(obj->data);
	free(obj);
	*pobj = NULL;
}

void
nouveau_drm_del(struct nouveau_drm **pdrm)
{
	struct nouveau_drm *drm = *pdrm;
	CALLED();
	//nvClose(drm->nvhostctrl);
	nvClose(drm->nvhostgpu);
	nvClose(drm->nvmap);
	nvClose(drm->nvhostasgpu);
	nvClose(drm->nvhostctrlgpu);
	free(drm);
	*pdrm = NULL;
}

int
nouveau_drm_new(uint32_t nvhostctrl, struct nouveau_drm **pdrm)
{
	struct nouveau_drm *drm;
	CALLED();
	if (!(drm = calloc(1, sizeof(*drm)))) {
		return -ENOMEM;
	}
	drm->nvhostctrl = nvhostctrl;

	if (R_FAILED(nvOpen(&drm->nvhostctrlgpu, "/dev/nvhost-ctrl-gpu"))) {
		return -errno;
	}

	if (R_FAILED(nvOpen(&drm->nvhostasgpu, "/dev/nvhost-as-gpu"))) {
		return -errno;
	}

	if (R_FAILED(nvOpen(&drm->nvmap, "/dev/nvmap"))) {
		return -errno;
	}

	if (R_FAILED(nvOpen(&drm->nvhostgpu, "/dev/nvhost-gpu"))) {
		return -errno;
	}

	*pdrm = drm;
	return 0;
}

int
nouveau_device_new(struct nouveau_object *parent, int32_t oclass,
		   void *data, uint32_t size, struct nouveau_device **pdev)
{
	struct nouveau_drm *drm = nouveau_drm(parent);
	struct nouveau_device_priv *nvdev;
	nvioctl_gpu_characteristics gpu_chars;
	char *tmp;
	Result rc;
	CALLED();

	rc = nvioctlNvhostCtrlGpu_GetCharacteristics(drm->nvhostctrlgpu, &gpu_chars);
	if (R_FAILED(rc))
		return -errno;

	if (!(nvdev = calloc(1, sizeof(*nvdev))))
		return -ENOMEM;
	*pdev = &nvdev->base;
	nvdev->base.object.parent = &drm->client;
	nvdev->base.object.handle = ~0ULL;
	nvdev->base.object.oclass = NOUVEAU_DEVICE_CLASS;
	nvdev->base.object.length = ~0;
	nvdev->base.chipset = gpu_chars.arch;

	rc = nvioctlNvhostAsGpu_InitializeEx(drm->nvhostasgpu, 1, /*0*/0x10000);
	if (R_FAILED(rc))
		return -ENOMEM;

	rc = nvioctlNvhostAsGpu_AllocSpace(drm->nvhostasgpu, 0x10000, /*0x20000*/0x10000, 0, 0x10000, &nvdev->allocspace_offset);
	if (R_FAILED(rc))
		return -ENOMEM;

	rc = nvioctlNvhostAsGpu_BindChannel(drm->nvhostasgpu, drm->nvhostgpu);
	if (R_FAILED(rc))
		return -errno;

	rc = nvioctlChannel_SetNvmapFd(drm->nvhostgpu, drm->nvmap);
	if (R_FAILED(rc))
		return -errno;

	rc = nvioctlChannel_SetPriority(drm->nvhostgpu, NvChannelPriority_Medium);
	if (R_FAILED(rc))
		return -errno;

	if (!os_get_total_physical_memory(&nvdev->base.vram_size)) {
		TRACE("Failed to get physical memory size.");
		return -errno;
	}

	tmp = getenv("NOUVEAU_LIBDRM_VRAM_LIMIT_PERCENT");
	if (tmp)
		nvdev->vram_limit_percent = atoi(tmp);
	else
		nvdev->vram_limit_percent = 80;

	nvdev->base.vram_limit =
		(nvdev->base.vram_size * nvdev->vram_limit_percent) / 100;

	mtx_init(&nvdev->lock, mtx_plain);
	DRMINITLISTHEAD(&nvdev->bo_list);
	return 0;
}

void
nouveau_device_del(struct nouveau_device **pdev)
{
	struct nouveau_device_priv *nvdev = nouveau_device(*pdev);
	CALLED();
	if (nvdev) {
		free(nvdev->client);
		mtx_destroy(&nvdev->lock);
		free(nvdev);
		*pdev = NULL;
	}
}

int
nouveau_getparam(struct nouveau_device *dev, uint64_t param, uint64_t *value)
{
  /* NOUVEAU_GETPARAM_PTIMER_TIME = NVGPU_GPU_IOCTL_GET_GPU_TIME */
	return 0;
}

/* Unused
int
nouveau_setparam(struct nouveau_device *dev, uint64_t param, uint64_t value)
{
	return 0;
}
*/

int
nouveau_client_new(struct nouveau_device *dev, struct nouveau_client **pclient)
{
	struct nouveau_device_priv *nvdev = nouveau_device(dev);
	struct nouveau_client_priv *pcli;
	int id = 0, i, ret = -ENOMEM;
	uint32_t *clients;
	CALLED();

	mtx_lock(&nvdev->lock);

	for (i = 0; i < nvdev->nr_client; i++) {
		id = ffs(nvdev->client[i]) - 1;
		if (id >= 0)
			goto out;
	}

	clients = realloc(nvdev->client, sizeof(uint32_t) * (i + 1));
	if (!clients)
		goto unlock;
	nvdev->client = clients;
	nvdev->client[i] = 0;
	nvdev->nr_client++;

out:
	pcli = calloc(1, sizeof(*pcli));
	if (pcli) {
		nvdev->client[i] |= (1 << id);
		pcli->base.device = dev;
		pcli->base.id = (i * 32) + id;
		ret = 0;
	}

	*pclient = &pcli->base;

unlock:
	mtx_unlock(&nvdev->lock);
	return ret;
}

void
nouveau_client_del(struct nouveau_client **pclient)
{
	struct nouveau_client_priv *pcli = nouveau_client(*pclient);
	struct nouveau_device_priv *nvdev;
	CALLED();
	if (pcli) {
		int id = pcli->base.id;
		nvdev = nouveau_device(pcli->base.device);
		mtx_lock(&nvdev->lock);
		nvdev->client[id / 32] &= ~(1 << (id % 32));
		mtx_unlock(&nvdev->lock);
		free(pcli->kref);
		free(pcli);
	}
}

static void
nouveau_bo_del(struct nouveau_bo *bo)
{
	CALLED();

	if (bo->map) {
			free(bo->map);
			bo->map = NULL;
	}
}

int
nouveau_bo_new(struct nouveau_device *dev, uint32_t flags, uint32_t align,
	       uint64_t size, union nouveau_bo_config *config,
	       struct nouveau_bo **pbo)
{
	struct nouveau_drm *drm = nouveau_drm(&dev->object);
	struct nouveau_bo_priv *nvbo = calloc(1, sizeof(*nvbo));
	struct nouveau_bo *bo = &nvbo->base;
	Result rc;
	CALLED();
	if (align == 0)
		align = 0x1000;

	NOUVEAU_DBG(MISC, "nouveau: Allocating BO of size %ld and flags %x\n", size, flags);

	if (!nvbo)
		return -ENOMEM;
	p_atomic_set(&nvbo->refcnt, 1);
	bo->device = dev;
	bo->flags = flags;
	bo->size = size;
	bo->map = memalign(align, size);
	if (!bo->map)
		goto cleanup;
	memset(bo->map, 0, size);
	armDCacheFlush(bo->map, size);

  rc = nvioctlNvmap_Create(drm->nvmap, bo->size, &bo->handle);
	if (R_FAILED(rc)) {
		TRACE("Failed to create bo handle");
		goto cleanup;
	}

  rc = nvioctlNvmap_Alloc(drm->nvmap, bo->handle, 0x20000, 0, align, 0, bo->map);
	if (R_FAILED(rc)) {
		TRACE("Failed to allocate bo");
		goto cleanup;
	}

	// TODO: Should probably specify some flags here
	rc = nvioctlNvhostAsGpu_MapBufferEx(drm->nvhostasgpu, 0, -1, bo->handle, 0, 0, bo->size, 0, &nvbo->map_handle);
	if (R_FAILED(rc)) {
		TRACE("Failed to map bo");
		goto cleanup;
	}
	bo->offset = nvbo->map_handle;

  if (config) {
  	bo->config = *config;
	}
	*pbo = bo;
	return 0;

cleanup:
	free(bo->map);
	free(nvbo);
	return -ENOMEM;
}

/* Unused
static int
nouveau_bo_wrap_locked(struct nouveau_device *dev, uint32_t handle,
		       struct nouveau_bo **pbo, int name)
{
	return 0;
}

static void
nouveau_bo_make_global(struct nouveau_bo_priv *nvbo)
{
}
*/

int
nouveau_bo_wrap(struct nouveau_device *dev, uint32_t handle,
		struct nouveau_bo **pbo)
{
	// TODO: NV30-only
	CALLED();
	return 0;
}

int
nouveau_bo_name_ref(struct nouveau_device *dev, uint32_t name,
		    struct nouveau_bo **pbo)
{
	// TODO: Unimplemented
	CALLED();
	return 0;
}

int
nouveau_bo_name_get(struct nouveau_bo *bo, uint32_t *name)
{
	// TODO: Unimplemented
	CALLED();
	return 0;
}

void
nouveau_bo_ref(struct nouveau_bo *bo, struct nouveau_bo **pref)
{
	CALLED();
	struct nouveau_bo *ref = *pref;
	if (bo) {
		p_atomic_inc(&nouveau_bo(bo)->refcnt);
	}
	if (ref) {
		if (p_atomic_dec_zero(&nouveau_bo(ref)->refcnt))
			nouveau_bo_del(ref);
	}
	*pref = bo;
}

int
nouveau_bo_prime_handle_ref(struct nouveau_device *dev, int prime_fd,
			    struct nouveau_bo **bo)
{
	// TODO: Unimplemented
	CALLED();
	return 0;
}

int
nouveau_bo_set_prime(struct nouveau_bo *bo, int *prime_fd)
{
	// TODO: Unimplemented
	CALLED();
	return 0;
}

int
nouveau_bo_wait(struct nouveau_bo *bo, uint32_t access,
		struct nouveau_client *client)
{
	// TODO: Unimplemented
	CALLED();
	return 0;
}

int
nouveau_bo_map(struct nouveau_bo *bo, uint32_t access,
	       struct nouveau_client *client)
{
	// Memory is always shared, so it doesn't need to be mapped again
	CALLED();
	return nouveau_bo_wait(bo, access, client);
}

void
nouveau_bo_unmap(struct nouveau_bo *bo)
{
	CALLED();
}
