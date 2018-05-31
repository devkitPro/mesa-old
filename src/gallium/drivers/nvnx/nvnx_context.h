#pragma once
#include "pipe/p_context.h"
#include "pipe/p_state.h"

struct nvnx_context {
   struct pipe_context	         base;
   struct pipe_framebuffer_state framebuffer;

   struct pipe_vertex_buffer     vtxbuf[PIPE_MAX_ATTRIBS];
   unsigned                      num_vtxbufs;
};

static struct nvnx_context *nvnx_context(struct pipe_context *ctx)
{
   return (struct nvnx_context *)ctx;
}
