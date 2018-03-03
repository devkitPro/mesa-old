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

#include "nouveau.h"

#include "nvif/class.h"
#include "nvif/cl0080.h"
#include "nvif/ioctl.h"
#include "nvif/unpack.h"

#include <switch.h>

int
nouveau_object_mthd(struct nouveau_object *obj,
		    uint32_t mthd, void *data, uint32_t size)
{
	return 0;
}

void
nouveau_object_sclass_put(struct nouveau_sclass **psclass)
{
}

int
nouveau_object_sclass_get(struct nouveau_object *obj,
			  struct nouveau_sclass **psclass)
{
	return 0;
}

int
nouveau_object_mclass(struct nouveau_object *obj,
		      const struct nouveau_mclass *mclass)
{
	return 0;
}

static void
nouveau_object_fini(struct nouveau_object *obj)
{
}

static int
nouveau_object_init(struct nouveau_object *parent, uint32_t handle,
		    int32_t oclass, void *data, uint32_t size,
		    struct nouveau_object *obj)
{
	return 0;
}

/* NVGPU_IOCTL_CHANNEL_ALLOC_OBJ_CTX */
int
nouveau_object_new(struct nouveau_object *parent, uint64_t handle,
		   uint32_t oclass, void *data, uint32_t length,
		   struct nouveau_object **pobj)
{
	return 0;
}

/* NVGPU_IOCTL_CHANNEL_FREE_OBJ_CTX */
void
nouveau_object_del(struct nouveau_object **pobj)
{
}

void
nouveau_drm_del(struct nouveau_drm **pdrm)
{
}

int
nouveau_drm_new(int fd, struct nouveau_drm **pdrm)
{
	return 0;
}

int
nouveau_device_new(struct nouveau_object *parent, int32_t oclass,
		   void *data, uint32_t size, struct nouveau_device **pdev)
{
	return 0;
}

void
nouveau_device_del(struct nouveau_device **pdev)
{
}

int
nouveau_getparam(struct nouveau_device *dev, uint64_t param, uint64_t *value)
{
	return 0;
}

int
nouveau_setparam(struct nouveau_device *dev, uint64_t param, uint64_t value)
{
	return 0;
}

int
nouveau_client_new(struct nouveau_device *dev, struct nouveau_client **pclient)
{
	return 0;
}

void
nouveau_client_del(struct nouveau_client **pclient)
{
}

static void
nouveau_bo_del(struct nouveau_bo *bo)
{
}

int
nouveau_bo_new(struct nouveau_device *dev, uint32_t flags, uint32_t align,
	       uint64_t size, union nouveau_bo_config *config,
	       struct nouveau_bo **pbo)
{
	return 0;
}

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

int
nouveau_bo_wrap(struct nouveau_device *dev, uint32_t handle,
		struct nouveau_bo **pbo)
{
	return 0;
}

int
nouveau_bo_name_ref(struct nouveau_device *dev, uint32_t name,
		    struct nouveau_bo **pbo)
{
	return 0;
}

int
nouveau_bo_name_get(struct nouveau_bo *bo, uint32_t *name)
{
	return 0;
}

void
nouveau_bo_ref(struct nouveau_bo *bo, struct nouveau_bo **pref)
{
}

int
nouveau_bo_prime_handle_ref(struct nouveau_device *dev, int prime_fd,
			    struct nouveau_bo **bo)
{
	return 0;
}

int
nouveau_bo_set_prime(struct nouveau_bo *bo, int *prime_fd)
{
	return 0;
}

int
nouveau_bo_wait(struct nouveau_bo *bo, uint32_t access,
		struct nouveau_client *client)
{
	return 0;
}

int
nouveau_bo_map(struct nouveau_bo *bo, uint32_t access,
	       struct nouveau_client *client)
{
	return 0;
}

void
nouveau_bo_map(struct nouveau_bo *bo)
{
   munmap(bo->map, bo->size);
   bo->map = NULL;
}
