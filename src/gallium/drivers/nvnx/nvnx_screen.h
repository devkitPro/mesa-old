#include "pipe/p_context.h"
#include "pipe/p_screen.h"

#include <switch.h>

struct nvnx_screen {
   struct pipe_screen	base;
   struct pipe_screen	*oscreen;
   NvGpu	               gpu;
   Vn                   vn;
};

static struct nvnx_screen *nvnx_screen(struct pipe_context *ctx)
{
   return (struct nvnx_screen *)ctx->screen;
}
