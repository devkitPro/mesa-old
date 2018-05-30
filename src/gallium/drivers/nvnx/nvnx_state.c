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
#include "util/u_transfer.h"
#include "tgsi/tgsi_parse.h"
#include "nvnx_screen.h"
#include "nvnx_context.h"

#include "nouveau/nvc0/nvc0_program.h"

/* nvc0_program.c */
bool nvc0_program_translate(struct nvc0_program *, uint16_t chipset,
                            struct pipe_debug_callback *);

#ifdef DEBUG
#	define TRACE(x...) printf("nvnx: " x)
#	define CALLED() TRACE("CALLED: %s\n", __PRETTY_FUNCTION__)
#else
#	define TRACE(x...)
#  define CALLED()
#endif

#define NVGPU_GPU_ARCH_GM200 0x120

static void nvnx_draw_vbo(struct pipe_context *ctx, const struct pipe_draw_info *info)
{
   CALLED();
}

static void nvnx_launch_grid(struct pipe_context *ctx,
                             const struct pipe_grid_info *info)
{
   CALLED();
}

static void nvnx_set_blend_color(struct pipe_context *ctx,
                                 const struct pipe_blend_color *state)
{
   CALLED();
}

static void *nvnx_create_blend_state(struct pipe_context *ctx,
                                     const struct pipe_blend_state *state)
{
   CALLED();
   return MALLOC(1);
}

static void *nvnx_create_dsa_state(struct pipe_context *ctx,
                                   const struct pipe_depth_stencil_alpha_state *state)
{
   CALLED();
   return MALLOC(1);
}

static void *nvnx_create_rs_state(struct pipe_context *ctx,
                                  const struct pipe_rasterizer_state *state)
{
   CALLED();
   return MALLOC(1);
}

static void *nvnx_create_sampler_state(struct pipe_context *ctx,
                                       const struct pipe_sampler_state *state)
{
   CALLED();
   return MALLOC(1);
}

static struct pipe_sampler_view *nvnx_create_sampler_view(struct pipe_context *ctx,
                                                          struct pipe_resource *texture,
                                                          const struct pipe_sampler_view *state)
{
   CALLED();
   struct pipe_sampler_view *sampler_view = CALLOC_STRUCT(pipe_sampler_view);

   if (!sampler_view)
      return NULL;

   /* initialize base object */
   *sampler_view = *state;
   sampler_view->texture = NULL;
   pipe_resource_reference(&sampler_view->texture, texture);
   pipe_reference_init(&sampler_view->reference, 1);
   sampler_view->context = ctx;
   return sampler_view;
}

static struct pipe_surface *nvnx_create_surface(struct pipe_context *ctx,
                                                struct pipe_resource *texture,
                                                const struct pipe_surface *surf_tmpl)
{
   CALLED();
   struct pipe_surface *surface = CALLOC_STRUCT(pipe_surface);

   if (!surface)
      return NULL;
   pipe_reference_init(&surface->reference, 1);
   pipe_resource_reference(&surface->texture, texture);
   surface->context = ctx;
   surface->format = surf_tmpl->format;
   surface->width = texture->width0;
   surface->height = texture->height0;
   surface->texture = texture;
   surface->u.tex.first_layer = surf_tmpl->u.tex.first_layer;
   surface->u.tex.last_layer = surf_tmpl->u.tex.last_layer;
   surface->u.tex.level = surf_tmpl->u.tex.level;

   return surface;
}

static void nvnx_set_sampler_views(struct pipe_context *ctx,
                                   enum pipe_shader_type shader,
                                   unsigned start, unsigned count,
                                   struct pipe_sampler_view **views)
{
   CALLED();
}

static void nvnx_bind_sampler_states(struct pipe_context *ctx,
                                     enum pipe_shader_type shader,
                                     unsigned start, unsigned count,
                                     void **states)
{
   CALLED();
}

static void nvnx_set_clip_state(struct pipe_context *ctx,
                                const struct pipe_clip_state *state)
{
   CALLED();
}

static void nvnx_set_polygon_stipple(struct pipe_context *ctx,
                                     const struct pipe_poly_stipple *state)
{
   CALLED();
}

static void nvnx_set_sample_mask(struct pipe_context *pipe, unsigned sample_mask)
{
   CALLED();
}

static void nvnx_set_scissor_states(struct pipe_context *ctx,
                                    unsigned start_slot,
                                    unsigned num_scissors,
                                    const struct pipe_scissor_state *state)
{
   CALLED();
}

static void nvnx_set_stencil_ref(struct pipe_context *ctx,
                                 const struct pipe_stencil_ref *state)
{
   CALLED();
}

static void nvnx_set_viewport_states(struct pipe_context *ctx,
                                     unsigned start_slot,
                                     unsigned num_viewports,
                                     const struct pipe_viewport_state *state)
{
   CALLED();
}

static void nvnx_set_framebuffer_state(struct pipe_context *ctx,
                                       const struct pipe_framebuffer_state *state)
{
   CALLED();
   struct nvnx_context *nxctx = nvnx_context(ctx);
   nxctx->framebuffer = *state;
}

static void nvnx_set_constant_buffer(struct pipe_context *ctx,
                                     enum pipe_shader_type shader, uint index,
                                     const struct pipe_constant_buffer *cb)
{
   CALLED();
}


static void nvnx_sampler_view_destroy(struct pipe_context *ctx,
                                      struct pipe_sampler_view *state)
{
   CALLED();
   pipe_resource_reference(&state->texture, NULL);
   FREE(state);
}


static void nvnx_surface_destroy(struct pipe_context *ctx,
                                 struct pipe_surface *surface)
{
   CALLED();
   pipe_resource_reference(&surface->texture, NULL);
   FREE(surface);
}

static void nvnx_bind_state(struct pipe_context *ctx, void *state)
{
   CALLED();
}

static void nvnx_delete_state(struct pipe_context *ctx, void *state)
{
   CALLED();
   FREE(state);
}

static void nvnx_set_vertex_buffers(struct pipe_context *ctx,
                                    unsigned start_slot, unsigned count,
                                    const struct pipe_vertex_buffer *buffers)
{
   CALLED();
}

static void *nvnx_create_vertex_elements(struct pipe_context *ctx,
                                         unsigned count,
                                         const struct pipe_vertex_element *state)
{
   CALLED();
   return MALLOC(1);
}

static void *
nvnx_sp_state_create(struct pipe_context *pipe,
                     const struct pipe_shader_state *cso, unsigned type)
{
   CALLED();
   struct nvnx_screen *nxscreen = nvnx_screen(pipe);
   struct nvc0_program *prog;

   prog = CALLOC_STRUCT(nvc0_program);
   if (!prog)
      return NULL;

   prog->type = type;

   if (cso->tokens)
      prog->pipe.tokens = tgsi_dup_tokens(cso->tokens);

   if (cso->stream_output.num_outputs)
      prog->pipe.stream_output = cso->stream_output;

   prog->translated = nvc0_program_translate(prog, NVGPU_GPU_ARCH_GM200, NULL);
   if (!prog->translated)
   {
      TRACE("Failed to translate program!\n");
      return NULL;
   }

   /* On Fermi, SP_START_ID must be aligned to 0x40.
   * On Kepler, the first instruction must be aligned to 0x80 because
   * latency information is expected only at certain positions.
   */
   prog->code_base = nxscreen->code_offset;
   switch (nxscreen->code_offset & 0xff) {
      case 0x40: prog->code_base += 0x70; break;
      case 0x80: prog->code_base += 0x30; break;
      case 0xc0: prog->code_base += 0x70; break;
      default:
         prog->code_base += 0x30;
         assert((nxscreen->code_offset & 0xff) == 0x00);
         break;
   }

   void *dst = nvBufferGetCpuAddr(&nxscreen->vn.code_segment) + prog->code_base;
   memcpy(dst, prog->code, prog->code_size);
   armDCacheFlush(dst, prog->code_size);

   // TODO: Handle code segment resize
   nxscreen->code_offset += align(prog->code_size + 0x70, 0x40);
   return (void *)prog;
}

static void
nvnx_sp_bind_state(struct pipe_context *pipe, void *po)
{
   CALLED();
   struct nvnx_screen *nxscreen = nvnx_screen(pipe);
   struct nvc0_program *prog = (struct nvc0_program *)po;

   NvProgramStage stage;
   switch (prog->type)
   {
      case PIPE_SHADER_VERTEX:
         TRACE("Binding vertex shader program\n");
         stage = NvProgramStage_VP_B;
         break;
      case PIPE_SHADER_FRAGMENT:
         TRACE("Binding fragment shader program\n");
         stage = NvProgramStage_FP;
         break;
      case PIPE_SHADER_GEOMETRY:
         TRACE("Binding geometry shader program\n");
         stage = NvProgramStage_GP;
         break;
      case PIPE_SHADER_TESS_CTRL:
         TRACE("Binding control shader program\n");
         stage = NvProgramStage_TCP;
         break;
      case PIPE_SHADER_TESS_EVAL:
         TRACE("Binding evaluation shader program\n");
         stage = NvProgramStage_TEP;
         break;
      default:
         TRACE("Unknown shader type\n");
         return;
   }

   vnBindProgram(&nxscreen->vn, stage, prog->code_base, prog->num_gprs);
}

static void
nvnx_sp_state_delete(struct pipe_context *pipe, void *hwcso)
{
   CALLED();
   struct nvc0_program *prog = (struct nvc0_program *)hwcso;

   //nvc0_program_destroy(nvc0_context(pipe), prog);

   FREE(prog->mem);
   FREE((void *)prog->pipe.tokens);
   FREE(prog);
}

static void *
nvnx_vp_state_create(struct pipe_context *pipe,
                     const struct pipe_shader_state *cso)
{
   return nvnx_sp_state_create(pipe, cso, PIPE_SHADER_VERTEX);
}

static void *
nvnx_fp_state_create(struct pipe_context *pipe,
                     const struct pipe_shader_state *cso)
{
   return nvnx_sp_state_create(pipe, cso, PIPE_SHADER_FRAGMENT);
}

static void *
nvnx_gp_state_create(struct pipe_context *pipe,
                     const struct pipe_shader_state *cso)
{
   return nvnx_sp_state_create(pipe, cso, PIPE_SHADER_GEOMETRY);
}

static void *
nvnx_tcp_state_create(struct pipe_context *pipe,
                     const struct pipe_shader_state *cso)
{
   return nvnx_sp_state_create(pipe, cso, PIPE_SHADER_TESS_CTRL);
}

static void *
nvnx_tep_state_create(struct pipe_context *pipe,
                     const struct pipe_shader_state *cso)
{
   return nvnx_sp_state_create(pipe, cso, PIPE_SHADER_TESS_EVAL);
}

static void *
nvnx_cp_state_create(struct pipe_context *pipe,
                     const struct pipe_compute_state *cso)
{
   struct nvc0_program *prog;

   prog = CALLOC_STRUCT(nvc0_program);
   if (!prog)
      return NULL;
   prog->type = PIPE_SHADER_COMPUTE;

   prog->cp.smem_size = cso->req_local_mem;
   prog->cp.lmem_size = cso->req_private_mem;
   prog->parm_size = cso->req_input_mem;

   prog->pipe.tokens = tgsi_dup_tokens((const struct tgsi_token *)cso->prog);

   prog->translated = nvc0_program_translate(prog, NVGPU_GPU_ARCH_GM200, NULL);

   return (void *)prog;
}

static struct pipe_stream_output_target *nvnx_create_stream_output_target(
      struct pipe_context *ctx,
      struct pipe_resource *res,
      unsigned buffer_offset,
      unsigned buffer_size)
{
   CALLED();
   struct pipe_stream_output_target *t = CALLOC_STRUCT(pipe_stream_output_target);
   if (!t)
      return NULL;

   pipe_reference_init(&t->reference, 1);
   pipe_resource_reference(&t->buffer, res);
   t->buffer_offset = buffer_offset;
   t->buffer_size = buffer_size;
   return t;
}

static void nvnx_stream_output_target_destroy(struct pipe_context *ctx,
                                              struct pipe_stream_output_target *t)
{
   CALLED();
   pipe_resource_reference(&t->buffer, NULL);
   FREE(t);
}

static void nvnx_set_stream_output_targets(struct pipe_context *ctx,
                                           unsigned num_targets,
                                           struct pipe_stream_output_target **targets,
                                           const unsigned *offsets)
{
   CALLED();
}

void nvnx_init_state_functions(struct pipe_context *ctx);

void nvnx_init_state_functions(struct pipe_context *ctx)
{
   CALLED();
   ctx->create_blend_state = nvnx_create_blend_state;
   ctx->create_depth_stencil_alpha_state = nvnx_create_dsa_state;
   ctx->create_rasterizer_state = nvnx_create_rs_state;
   ctx->create_sampler_state = nvnx_create_sampler_state;
   ctx->create_sampler_view = nvnx_create_sampler_view;
   ctx->create_surface = nvnx_create_surface;
   ctx->create_vertex_elements_state = nvnx_create_vertex_elements;
   ctx->create_vs_state = nvnx_vp_state_create;
   ctx->create_fs_state = nvnx_fp_state_create;
   ctx->create_gs_state = nvnx_gp_state_create;
   ctx->create_tcs_state = nvnx_tcp_state_create;
   ctx->create_tes_state = nvnx_tep_state_create;
   ctx->create_compute_state = nvnx_cp_state_create;
   ctx->bind_blend_state = nvnx_bind_state;
   ctx->bind_depth_stencil_alpha_state = nvnx_bind_state;
   ctx->bind_sampler_states = nvnx_bind_sampler_states;
   ctx->bind_rasterizer_state = nvnx_bind_state;
   ctx->bind_vertex_elements_state = nvnx_bind_state;
   ctx->bind_compute_state = nvnx_bind_state;
   ctx->bind_vs_state = nvnx_sp_bind_state;
   ctx->bind_fs_state = nvnx_sp_bind_state;
   ctx->bind_tcs_state = nvnx_sp_bind_state;
   ctx->bind_tes_state = nvnx_sp_bind_state;
   ctx->bind_gs_state = nvnx_sp_bind_state;
   ctx->delete_blend_state = nvnx_delete_state;
   ctx->delete_depth_stencil_alpha_state = nvnx_delete_state;
   ctx->delete_rasterizer_state = nvnx_delete_state;
   ctx->delete_sampler_state = nvnx_delete_state;
   ctx->delete_vertex_elements_state = nvnx_delete_state;
   ctx->delete_vs_state = nvnx_sp_state_delete;
   ctx->delete_fs_state = nvnx_sp_state_delete;
   ctx->delete_tcs_state = nvnx_sp_state_delete;
   ctx->delete_tes_state = nvnx_sp_state_delete;
   ctx->delete_gs_state = nvnx_sp_state_delete;
   ctx->delete_compute_state = nvnx_sp_state_delete;
   ctx->set_blend_color = nvnx_set_blend_color;
   ctx->set_clip_state = nvnx_set_clip_state;
   ctx->set_constant_buffer = nvnx_set_constant_buffer;
   ctx->set_sampler_views = nvnx_set_sampler_views;
   ctx->set_framebuffer_state = nvnx_set_framebuffer_state;
   ctx->set_polygon_stipple = nvnx_set_polygon_stipple;
   ctx->set_sample_mask = nvnx_set_sample_mask;
   ctx->set_scissor_states = nvnx_set_scissor_states;
   ctx->set_stencil_ref = nvnx_set_stencil_ref;
   ctx->set_vertex_buffers = nvnx_set_vertex_buffers;
   ctx->set_viewport_states = nvnx_set_viewport_states;
   ctx->sampler_view_destroy = nvnx_sampler_view_destroy;
   ctx->surface_destroy = nvnx_surface_destroy;
   ctx->draw_vbo = nvnx_draw_vbo;
   ctx->launch_grid = nvnx_launch_grid;
   ctx->create_stream_output_target = nvnx_create_stream_output_target;
   ctx->stream_output_target_destroy = nvnx_stream_output_target_destroy;
   ctx->set_stream_output_targets = nvnx_set_stream_output_targets;
}
