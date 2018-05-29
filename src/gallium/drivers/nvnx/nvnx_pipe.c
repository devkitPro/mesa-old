/*
 * Copyright 2010 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <stdio.h>
#include <errno.h>
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_format.h"
#include "util/u_upload_mgr.h"
#include "nvnx_public.h"
#include "nvnx_screen.h"
#include "nvnx_context.h"

#include <switch.h>

#ifdef DEBUG
#	define TRACE(x...) printf("nvnx: " x)
#	define CALLED() TRACE("CALLED: %s\n", __PRETTY_FUNCTION__)
#else
#	define TRACE(x...)
#  define CALLED()
#endif

void nvnx_init_state_functions(struct pipe_context *ctx);

/*
 * query
 */
struct nvnx_query {
   unsigned	query;
};
static struct pipe_query *nvnx_create_query(struct pipe_context *ctx, unsigned query_type, unsigned index)
{
   CALLED();
   struct nvnx_query *query = CALLOC_STRUCT(nvnx_query);

   return (struct pipe_query *)query;
}

static void nvnx_destroy_query(struct pipe_context *ctx, struct pipe_query *query)
{
   CALLED();
   FREE(query);
}

static boolean nvnx_begin_query(struct pipe_context *ctx, struct pipe_query *query)
{
   CALLED();
   return true;
}

static bool nvnx_end_query(struct pipe_context *ctx, struct pipe_query *query)
{
   CALLED();
   return true;
}

static boolean nvnx_get_query_result(struct pipe_context *ctx,
                                     struct pipe_query *query,
                                     boolean wait,
                                     union pipe_query_result *vresult)
{
   CALLED();
   uint64_t *result = (uint64_t*)vresult;

   *result = 0;
   return TRUE;
}

static void
nvnx_set_active_query_state(struct pipe_context *pipe, boolean enable)
{
   CALLED();
}


/*
 * resource
 */
struct nvnx_resource {
   struct pipe_resource	base;
   NvBuffer buffer;
};

static struct nvnx_resource *nvnx_resource(struct pipe_resource *res)
{
   return (struct nvnx_resource *)res;
}

static struct pipe_resource *nvnx_resource_create(struct pipe_screen *screen,
                                                  const struct pipe_resource *templ)
{
   CALLED();
   struct nvnx_screen *nvnx_screen = (struct nvnx_screen*)screen;
   struct nvnx_resource *nresource;
   unsigned stride;
   Result rc;

   nresource = CALLOC_STRUCT(nvnx_resource);
   if (!nresource)
      return NULL;

   stride = util_format_get_stride(templ->format, templ->width0);
   nresource->base = *templ;
   nresource->base.screen = screen;
   size_t size = stride * templ->height0 * templ->depth0;
   rc = nvBufferCreateRw(&nresource->buffer, size, 0x1000, 0, &nvnx_screen->gpu.addr_space);
   memset(nvBufferGetCpuAddr(&nresource->buffer), 0x33, size);
   armDCacheFlush(nvBufferGetCpuAddr(&nresource->buffer), size);
   nvBufferMakeCpuUncached(&nresource->buffer);

   pipe_reference_init(&nresource->base.reference, 1);
   if (R_FAILED(rc)) {
      TRACE("Failed to create buffer (%d)\n", rc);
      FREE(nresource);
      return NULL;
   }
   return &nresource->base;
}

static struct pipe_resource *nvnx_resource_from_handle(struct pipe_screen *screen,
                                                       const struct pipe_resource *templ,
                                                       struct winsys_handle *handle,
                                                       unsigned usage)
{
   CALLED();
   struct nvnx_screen *nvnx_screen = (struct nvnx_screen*)screen;
   struct pipe_screen *oscreen = nvnx_screen->oscreen;
   struct pipe_resource *result;
   struct pipe_resource *nvnx_resource;

   result = oscreen->resource_from_handle(oscreen, templ, handle, usage);
   nvnx_resource = nvnx_resource_create(screen, result);
   pipe_resource_reference(&result, NULL);
   return nvnx_resource;
}

static boolean nvnx_resource_get_handle(struct pipe_screen *pscreen,
                                        struct pipe_context *ctx,
                                        struct pipe_resource *resource,
                                        struct winsys_handle *handle,
                                        unsigned usage)
{
   CALLED();
   struct nvnx_screen *nvnx_screen = (struct nvnx_screen*)pscreen;
   struct pipe_screen *screen = nvnx_screen->oscreen;
   struct pipe_resource *tex;
   bool result;

   /* resource_get_handle musn't fail. Just create something and return it. */
   tex = screen->resource_create(screen, resource);
   if (!tex)
      return false;

   result = screen->resource_get_handle(screen, NULL, tex, handle, usage);
   pipe_resource_reference(&tex, NULL);
   return result;
}

static void nvnx_resource_destroy(struct pipe_screen *screen,
                                  struct pipe_resource *resource)
{
   CALLED();
   struct nvnx_resource *nresource = (struct nvnx_resource *)resource;

   nvBufferFree(&nresource->buffer);
   FREE(resource);
}


/*
 * transfer
 */
static void *nvnx_transfer_map(struct pipe_context *pipe,
                               struct pipe_resource *resource,
                               unsigned level,
                               enum pipe_transfer_usage usage,
                               const struct pipe_box *box,
                               struct pipe_transfer **ptransfer)
{
   CALLED();
   struct pipe_transfer *transfer;
   struct nvnx_resource *nresource = (struct nvnx_resource *)resource;

   transfer = CALLOC_STRUCT(pipe_transfer);
   if (!transfer)
      return NULL;
   pipe_resource_reference(&transfer->resource, resource);
   transfer->level = level;
   transfer->usage = usage;
   transfer->box = *box;
   transfer->stride = util_format_get_blocksize(resource->format) * resource->width0;
   transfer->layer_stride = transfer->stride * resource->height0;
   *ptransfer = transfer;

   return nvBufferGetCpuAddr(&nresource->buffer);
}

static void nvnx_transfer_flush_region(struct pipe_context *pipe,
                                       struct pipe_transfer *transfer,
                                       const struct pipe_box *box)
{
   CALLED();
}

static void nvnx_transfer_unmap(struct pipe_context *pipe,
                                struct pipe_transfer *transfer)
{
   CALLED();
   pipe_resource_reference(&transfer->resource, NULL);
   FREE(transfer);
}

static void nvnx_buffer_subdata(struct pipe_context *pipe,
                                struct pipe_resource *resource,
                                unsigned usage, unsigned offset,
                                unsigned size, const void *data)
{
   CALLED();
}

static void nvnx_texture_subdata(struct pipe_context *pipe,
                                 struct pipe_resource *resource,
                                 unsigned level,
                                 unsigned usage,
                                 const struct pipe_box *box,
                                 const void *data,
                                 unsigned stride,
                                 unsigned layer_stride)
{
   CALLED();
}


/*
 * clear/copy
 */
static void nvnx_clear(struct pipe_context *ctx, unsigned buffers,
                       const union pipe_color_union *color, double depth, unsigned stencil)
{
   CALLED();
   struct nvnx_screen *nxscreen = nvnx_screen(ctx);
   struct nvnx_context *nxctx = nvnx_context(ctx);
   struct nvnx_resource *nxres = nvnx_resource(nxctx->framebuffer.cbufs[0]->texture);
   Result rc;
   if (!nxres) {
      TRACE("Framebuffer incomplete!\n");
      return;
   }

   printf("Before clear: %x\n", *(u32*)nvBufferGetCpuAddr(&nxres->buffer));
   vnClearBuffer(&nxscreen->vn, &nxres->buffer, nxctx->framebuffer.width, nxctx->framebuffer.height, 0xd5, (float[]) {1,1,1,1});
   rc = vnSubmit(&nxscreen->vn);
   if (R_FAILED(rc)) {
      TRACE("Failed to clear buffer (%d)\n", rc);
   }

   // TODO: Fencing
   svcSleepThread(1000000000ull);

   printf("After clear: %x\n", *(u32*)nvBufferGetCpuAddr(&nxres->buffer));
}

static void nvnx_clear_render_target(struct pipe_context *ctx,
                                     struct pipe_surface *dst,
                                     const union pipe_color_union *color,
                                     unsigned dstx, unsigned dsty,
                                     unsigned width, unsigned height,
                                     bool render_condition_enabled)
{
   CALLED();
}

static void nvnx_clear_depth_stencil(struct pipe_context *ctx,
                                     struct pipe_surface *dst,
                                     unsigned clear_flags,
                                     double depth,
                                     unsigned stencil,
                                     unsigned dstx, unsigned dsty,
                                     unsigned width, unsigned height,
                                     bool render_condition_enabled)
{
   CALLED();
}

static void nvnx_resource_copy_region(struct pipe_context *ctx,
                                      struct pipe_resource *dst,
                                      unsigned dst_level,
                                      unsigned dstx, unsigned dsty, unsigned dstz,
                                      struct pipe_resource *src,
                                      unsigned src_level,
                                      const struct pipe_box *src_box)
{
   CALLED();
}


static void nvnx_blit(struct pipe_context *ctx,
                      const struct pipe_blit_info *info)
{
   CALLED();
}


static void
nvnx_flush_resource(struct pipe_context *ctx,
                    struct pipe_resource *resource)
{
   CALLED();
}


/*
 * context
 */
static void nvnx_flush(struct pipe_context *ctx,
                       struct pipe_fence_handle **fence,
                       unsigned flags)
{
   CALLED();
   if (fence)
      *fence = NULL;
}

static void nvnx_destroy_context(struct pipe_context *ctx)
{
   CALLED();
   if (ctx->stream_uploader)
      u_upload_destroy(ctx->stream_uploader);

   FREE(ctx);
}

static boolean nvnx_generate_mipmap(struct pipe_context *ctx,
                                    struct pipe_resource *resource,
                                    enum pipe_format format,
                                    unsigned base_level,
                                    unsigned last_level,
                                    unsigned first_layer,
                                    unsigned last_layer)
{
   CALLED();
   return true;
}

static struct pipe_context *nvnx_create_context(struct pipe_screen *screen,
                                                void *priv, unsigned flags)
{
   CALLED();
   struct nvnx_context *nvnx_ctx = CALLOC_STRUCT(nvnx_context);
   if (!nvnx_ctx)
      return NULL;
   struct pipe_context *ctx = &nvnx_ctx->base;

   ctx->screen = screen;
   ctx->priv = priv;

   ctx->stream_uploader = u_upload_create_default(ctx);
   if (!ctx->stream_uploader) {
      FREE(ctx);
      return NULL;
   }
   ctx->const_uploader = ctx->stream_uploader;

   ctx->destroy = nvnx_destroy_context;
   ctx->flush = nvnx_flush;
   ctx->clear = nvnx_clear;
   ctx->clear_render_target = nvnx_clear_render_target;
   ctx->clear_depth_stencil = nvnx_clear_depth_stencil;
   ctx->resource_copy_region = nvnx_resource_copy_region;
   ctx->generate_mipmap = nvnx_generate_mipmap;
   ctx->blit = nvnx_blit;
   ctx->flush_resource = nvnx_flush_resource;
   ctx->create_query = nvnx_create_query;
   ctx->destroy_query = nvnx_destroy_query;
   ctx->begin_query = nvnx_begin_query;
   ctx->end_query = nvnx_end_query;
   ctx->get_query_result = nvnx_get_query_result;
   ctx->set_active_query_state = nvnx_set_active_query_state;
   ctx->transfer_map = nvnx_transfer_map;
   ctx->transfer_flush_region = nvnx_transfer_flush_region;
   ctx->transfer_unmap = nvnx_transfer_unmap;
   ctx->buffer_subdata = nvnx_buffer_subdata;
   ctx->texture_subdata = nvnx_texture_subdata;
   nvnx_init_state_functions(ctx);

   return ctx;
}


/*
 * pipe_screen
 */
static void nvnx_flush_frontbuffer(struct pipe_screen *_screen,
                                   struct pipe_resource *resource,
                                   unsigned level, unsigned layer,
                                   void *context_private, struct pipe_box *box)
{
   CALLED();
}

static const char *nvnx_get_vendor(struct pipe_screen* pscreen)
{
   CALLED();
   return "Nintendo";
}

static const char *nvnx_get_device_vendor(struct pipe_screen* pscreen)
{
   CALLED();
   return "Nvidia";
}

static const char *nvnx_get_name(struct pipe_screen* pscreen)
{
   CALLED();
   return "nvnx";
}

static int nvnx_get_param(struct pipe_screen* pscreen, enum pipe_cap param)
{
   CALLED();
   struct pipe_screen *screen = ((struct nvnx_screen*)pscreen)->oscreen;

   return screen->get_param(screen, param);
}

static float nvnx_get_paramf(struct pipe_screen* pscreen,
                             enum pipe_capf param)
{
   CALLED();
   struct pipe_screen *screen = ((struct nvnx_screen*)pscreen)->oscreen;

   return screen->get_paramf(screen, param);
}

static int nvnx_get_shader_param(struct pipe_screen* pscreen,
                                 enum pipe_shader_type shader,
                                 enum pipe_shader_cap param)
{
   CALLED();
   struct pipe_screen *screen = ((struct nvnx_screen*)pscreen)->oscreen;

   switch (param) {
      case PIPE_SHADER_CAP_PREFERRED_IR:
         return PIPE_SHADER_IR_TGSI;
      default:
         return screen->get_shader_param(screen, shader, param);
   }

   return 0;
}

static int nvnx_get_compute_param(struct pipe_screen *pscreen,
                                  enum pipe_shader_ir ir_type,
                                  enum pipe_compute_cap param,
                                  void *ret)
{
   CALLED();
   struct pipe_screen *screen = ((struct nvnx_screen*)pscreen)->oscreen;

   return screen->get_compute_param(screen, ir_type, param, ret);
}

static boolean nvnx_is_format_supported(struct pipe_screen* pscreen,
                                        enum pipe_format format,
                                        enum pipe_texture_target target,
                                        unsigned sample_count,
                                        unsigned usage)
{
   CALLED();
   struct pipe_screen *screen = ((struct nvnx_screen*)pscreen)->oscreen;

   return screen->is_format_supported(screen, format, target, sample_count, usage);
}

static uint64_t nvnx_get_timestamp(struct pipe_screen *pscreen)
{
   CALLED();
   return 0;
}

static void nvnx_destroy_screen(struct pipe_screen *screen)
{
   CALLED();
   struct nvnx_screen *nvnx_screen = (struct nvnx_screen*)screen;
   struct pipe_screen *oscreen = nvnx_screen->oscreen;

   oscreen->destroy(oscreen);
   FREE(screen);
}

static void nvnx_fence_reference(struct pipe_screen *screen,
                          struct pipe_fence_handle **ptr,
                          struct pipe_fence_handle *fence)
{
   CALLED();
}

static boolean nvnx_fence_finish(struct pipe_screen *screen,
                                 struct pipe_context *ctx,
                                 struct pipe_fence_handle *fence,
                                 uint64_t timeout)
{
   CALLED();
   return true;
}

static void nvnx_query_memory_info(struct pipe_screen *pscreen,
                                   struct pipe_memory_info *info)
{
   CALLED();
   struct nvnx_screen *nvnx_screen = (struct nvnx_screen*)pscreen;
   struct pipe_screen *screen = nvnx_screen->oscreen;

   screen->query_memory_info(screen, info);
}

struct pipe_screen *nvnx_screen_create(struct pipe_screen *oscreen)
{
   CALLED();
   struct nvnx_screen *nvnx_screen;
   struct pipe_screen *screen;
   Result rc;

   nvnx_screen = CALLOC_STRUCT(nvnx_screen);
   if (!nvnx_screen) {
      return NULL;
   }
   nvnx_screen->oscreen = oscreen;
   screen = &nvnx_screen->base;

   rc = nvGpuCreate(&nvnx_screen->gpu);
   if (R_FAILED(rc))
   {
      TRACE("Failed to create GPU (%d)\n", rc);
      FREE(nvnx_screen);
   }

   vnInit(&nvnx_screen->vn, &nvnx_screen->gpu);
   vnInit3D(&nvnx_screen->vn);
   /*rc = vnSubmit(&nvnx_screen->vn);
   if (R_FAILED(rc))
   {
      TRACE("Failed to initialize 3D context (%d)\n", rc);
      FREE(nvnx_screen);
   }*/

   screen->destroy = nvnx_destroy_screen;
   screen->get_name = nvnx_get_name;
   screen->get_vendor = nvnx_get_vendor;
   screen->get_device_vendor = nvnx_get_device_vendor;
   screen->get_param = nvnx_get_param;
   screen->get_shader_param = nvnx_get_shader_param;
   screen->get_compute_param = nvnx_get_compute_param;
   screen->get_paramf = nvnx_get_paramf;
   screen->is_format_supported = nvnx_is_format_supported;
   screen->context_create = nvnx_create_context;
   screen->resource_create = nvnx_resource_create;
   screen->resource_from_handle = nvnx_resource_from_handle;
   screen->resource_get_handle = nvnx_resource_get_handle;
   screen->resource_destroy = nvnx_resource_destroy;
   screen->flush_frontbuffer = nvnx_flush_frontbuffer;
   screen->get_timestamp = nvnx_get_timestamp;
   screen->fence_reference = nvnx_fence_reference;
   screen->fence_finish = nvnx_fence_finish;
   screen->query_memory_info = nvnx_query_memory_info;

   // TODO: Fencing
   //svcSleepThread(1000000000ull);

   return screen;
}
