#pragma once
#include "pipe/p_context.h"
#include "pipe/p_state.h"

struct nvnx_vertex_stateobj
{
   struct pipe_vertex_element pipe[PIPE_MAX_ATTRIBS];
   u32 elements[PIPE_MAX_ATTRIBS];
};

struct nvnx_context {
   struct pipe_context	         base;
   struct pipe_framebuffer_state framebuffer;

   struct pipe_vertex_buffer     vtxbuf[PIPE_MAX_ATTRIBS];
   struct nvnx_vertex_stateobj*  vtxstate;
   unsigned                      num_vtxbufs;

   VnViewportConfig              viewports[PIPE_MAX_VIEWPORTS];
   VnRenderTargetConfig          targets[PIPE_MAX_COLOR_BUFS];
};

static struct nvnx_context *nvnx_context(struct pipe_context *ctx)
{
   return (struct nvnx_context *)ctx;
}
