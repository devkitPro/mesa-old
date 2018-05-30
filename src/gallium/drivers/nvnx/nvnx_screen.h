#include "pipe/p_context.h"
#include "pipe/p_screen.h"

#include <switch.h>

struct nvnx_screen {
   struct pipe_screen	base;
   struct pipe_screen	*oscreen;
   NvGpu	               gpu;
   Vn                   vn;
   u32                  code_offset;
};

static struct nvnx_screen *nvnx_screen(struct pipe_context *ctx)
{
   return (struct nvnx_screen *)ctx->screen;
}

struct nvc0_format {
   uint32_t rt;
   struct {
      unsigned format:7;
      unsigned type_r:3;
      unsigned type_g:3;
      unsigned type_b:3;
      unsigned type_a:3;
      unsigned src_x:3;
      unsigned src_y:3;
      unsigned src_z:3;
      unsigned src_w:3;
   } tic;
   uint32_t usage;
};

struct nvc0_vertex_format {
   uint32_t vtx;
   uint32_t usage;
};

extern const struct nvc0_format nvc0_format_table[];
extern const struct nvc0_vertex_format nvc0_vertex_format[];
