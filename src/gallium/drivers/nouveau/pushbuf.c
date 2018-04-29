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
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "libdrm_lists.h"
#include "nouveau_debug.h"
#include "nouveau_drm.h"
#include "nouveau.h"
#include "private.h"

#include <switch.h>


#ifdef DEBUG
#	define TRACE(x...) NOUVEAU_DBG(MISC, "nouveau: " x)
#	define CALLED() TRACE("CALLED: %s\n", __PRETTY_FUNCTION__)
#else
#	define TRACE(x...)
# define CALLED()
#endif
#define ERROR(x...) NOUVEAU_ERR("nouveau: " x)

struct nouveau_pushbuf_krec {
	struct nouveau_pushbuf_krec *next;
	struct drm_nouveau_gem_pushbuf_bo buffer[NOUVEAU_GEM_MAX_BUFFERS];
	struct drm_nouveau_gem_pushbuf_reloc reloc[NOUVEAU_GEM_MAX_RELOCS];
	struct drm_nouveau_gem_pushbuf_push push[NOUVEAU_GEM_MAX_PUSH];
	int nr_buffer;
	int nr_reloc;
	int nr_push;
	uint64_t vram_used;
	uint64_t gart_used;
};

struct nouveau_pushbuf_priv {
	struct nouveau_pushbuf base;
	struct nouveau_pushbuf_krec *list;
	struct nouveau_pushbuf_krec *krec;
	struct nouveau_list bctx_list;
	struct nouveau_bo *bo;
	uint32_t type;
	uint32_t *ptr;
	uint32_t *bgn;
	int bo_next;
	int bo_nr;
	struct nouveau_bo *bos[];
};

static inline struct nouveau_pushbuf_priv *
nouveau_pushbuf(struct nouveau_pushbuf *push)
{
	return (struct nouveau_pushbuf_priv *)push;
}

static int pushbuf_validate(struct nouveau_pushbuf *, bool);
static int pushbuf_flush(struct nouveau_pushbuf *);

static bool
pushbuf_kref_fits(struct nouveau_pushbuf *push, struct nouveau_bo *bo,
		  uint32_t *domains)
{
	CALLED();

	// Unimplemented
	return true;
}

static struct drm_nouveau_gem_pushbuf_bo *
pushbuf_kref(struct nouveau_pushbuf *push, struct nouveau_bo *bo,
	     uint32_t flags)
{
	CALLED();

	// Unimplemented
	return NULL;
}

static uint32_t
pushbuf_krel(struct nouveau_pushbuf *push, struct nouveau_bo *bo,
	     uint32_t data, uint32_t flags, uint32_t vor, uint32_t tor)
{
	CALLED();

	// Unimplemented
	return 0;
}

static void
pushbuf_dump(uint32_t *start, uint32_t *end)
{
	CALLED();
	for (uint32_t cmd = 0; start < end; start++)
	{
		cmd = *start;
		NOUVEAU_DBG(MISC, "0x%08x\n", cmd);
	}
}

static Result
nvEventWait(u32 fd, u32 syncpt_id, u32 threshold, s32 timeout) {
    Result rc=0;
    u32 result;

    do {
        rc = nvioctlNvhostCtrl_EventWait(fd, syncpt_id, threshold, timeout, 0, &result);
    } while(rc==5);//timeout error

    return rc;
}

static int
pushbuf_submit(struct nouveau_pushbuf *push, struct nouveau_object *chan)
{
	CALLED();
	struct nouveau_pushbuf_priv *nvpb = nouveau_pushbuf(push);
	struct nouveau_bo_priv *nvbo = nouveau_bo(nvpb->bo);
	struct nouveau_fifo *fifo = chan->data;
	struct nouveau_drm *drm = nouveau_drm(fifo->object);
	nvioctl_gpfifo_entry entry;
	nvioctl_fence fence;
	Result rc;

	// Calculate the offset of the command start
	u64 offset = (u64)nvpb->bgn - (u64)nvpb->ptr;
	u64 length = (u64)push->cur - (u64)nvpb->bgn;
	u64 va = ((u64)nvbo->map_handle + offset) | (length << 42);
	entry.entry0 = (u32)va;
	entry.entry1 = (u32)(va >> 32);
	err("offset: %ld length: %ld words: %ld va: %lx\n", offset, length, length / 4, va);

	rc = nvioctlChannel_SubmitGpfifo(fifo->channel, &entry, 1, 0x104, &fence);
	if (R_FAILED(rc)) {
		err("kernel rejected pushbuf: %d\n", rc);
		pushbuf_dump(nvpb->bgn, push->cur);
		return -errno;
	}
	pushbuf_dump(nvpb->bgn, push->cur);

	// Set the start for the next command buffer
	nvpb->bgn = push->cur;
	//push->cur = nvpb->bgn;

	TRACE("Waiting for fence\n");
	// Only run nvEventWait when the fence is valid and the id is not NO_FENCE.
	if (fence.id!=0xffffffff)
	{
		rc = nvEventWait(drm->nvhostctrl, fence.id, fence.value, -1);
		if (R_FAILED(rc)) {
			TRACE("Failed to wait for fence\n");
			return -1;
		}
	}
	else
	{
		TRACE("Invalid fence!\n");
	}
	return 0;
}

static int
pushbuf_flush(struct nouveau_pushbuf *push)
{
	CALLED();
	return pushbuf_submit(push, push->channel);
}

static void
pushbuf_refn_fail(struct nouveau_pushbuf *push, int sref, int srel)
{
	CALLED();

	// Unimplemented
}

static int
pushbuf_refn(struct nouveau_pushbuf *push, bool retry,
	     struct nouveau_pushbuf_refn *refs, int nr)
{
	CALLED();

	// Unimplemented
	return 0;
}

static int
pushbuf_validate(struct nouveau_pushbuf *push, bool retry)
{
	CALLED();

	// Unimplemented
	return 0;
}

int
nouveau_pushbuf_new(struct nouveau_client *client, struct nouveau_object *chan,
		    int nr, uint32_t size, bool immediate,
		    struct nouveau_pushbuf **ppush)
{
	CALLED();
	struct nouveau_fifo *fifo = chan->data;
	struct nouveau_drm *drm = nouveau_drm(fifo->object);
	struct nouveau_pushbuf_priv *nvpb;
	struct nouveau_pushbuf *push;
	nvioctl_fence fence;
	Result rc;
	int ret;

	rc = nvioctlChannel_AllocGpfifoEx2(fifo->channel, 0x800, 0x1, 0, 0, 0, 0, &fence);
	if (R_FAILED(rc)) {
		TRACE("Failed to allocate GPFIFO!\n");
		return -ENOMEM;
	}

	// Only run nvEventWait when the fence is valid and the id is not NO_FENCE.
	if (fence.id!=0xffffffff)
	{
		rc = nvEventWait(drm->nvhostctrl, fence.id, fence.value, -1);
		if (R_FAILED(rc)) {
			TRACE("Failed to wait for fence\n");
			return -1;
		}
	}
	else
	{
		TRACE("Invalid fence!\n");
	}

	nvpb = calloc(1, sizeof(*nvpb)); // + nr * sizeof(*nvpb->bos));
	if (!nvpb)
		return -ENOMEM;

	push = &nvpb->base;
	ret = nouveau_bo_new(client->device, NOUVEAU_BO_MAP, 0, size,
						NULL, &nvpb->bo);
	if (ret) {
		TRACE("Failed to create pushbuf bo!\n");
		nouveau_pushbuf_del(&push);
		return ret;
	}

	svcSetMemoryAttribute(nvpb->bo->map, nvpb->bo->size, 8, 8);
	push->channel = chan;
	nvpb->bgn = nvpb->bo->map;
	nvpb->ptr = nvpb->bgn;
	push->cur = nvpb->ptr;
	push->end = nvpb->ptr + (nvpb->bo->size / 4);
	*ppush = push;

	return 0;
}

void
nouveau_pushbuf_del(struct nouveau_pushbuf **ppush)
{
	CALLED();
	struct nouveau_pushbuf_priv *nvpb = nouveau_pushbuf(*ppush);

	nouveau_bo_ref(NULL, &nvpb->bo);
	free(nvpb);
	*ppush = NULL;
}

struct nouveau_bufctx *
nouveau_pushbuf_bufctx(struct nouveau_pushbuf *push, struct nouveau_bufctx *ctx)
{
	CALLED();

	// Unimplemented
	return NULL;
}

int
nouveau_pushbuf_space(struct nouveau_pushbuf *push,
		      uint32_t dwords, uint32_t relocs, uint32_t pushes)
{
	CALLED();

	// Unimplemented
	return 0;
}

void
nouveau_pushbuf_data(struct nouveau_pushbuf *push, struct nouveau_bo *bo,
		     uint64_t offset, uint64_t length)
{
	CALLED();

	// Unimplemented
}

int
nouveau_pushbuf_refn(struct nouveau_pushbuf *push,
		     struct nouveau_pushbuf_refn *refs, int nr)
{
	CALLED();
	return pushbuf_refn(push, true, refs, nr);
}

void
nouveau_pushbuf_reloc(struct nouveau_pushbuf *push, struct nouveau_bo *bo,
		      uint32_t data, uint32_t flags, uint32_t vor, uint32_t tor)
{
	CALLED();

	// Unimplemented
}

int
nouveau_pushbuf_validate(struct nouveau_pushbuf *push)
{
	CALLED();
	return pushbuf_validate(push, true);
}

uint32_t
nouveau_pushbuf_refd(struct nouveau_pushbuf *push, struct nouveau_bo *bo)
{
	CALLED();

	// Unimplemented
	return 0;
}

int
nouveau_pushbuf_kick(struct nouveau_pushbuf *push, struct nouveau_object *chan)
{
	CALLED();
	if (!push->channel)
		return pushbuf_submit(push, chan);
	pushbuf_flush(push);
	return pushbuf_validate(push, false);
}
