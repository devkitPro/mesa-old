#include "pipe/p_context.h"
#include "pipe/p_state.h"

struct nvnx_context {
   struct pipe_context	         base;
   struct pipe_framebuffer_state framebuffer;
};

static struct nvnx_context *nvnx_context(struct pipe_context *ctx)
{
   return (struct nvnx_context *)ctx;
}
