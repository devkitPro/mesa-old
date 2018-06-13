#pragma once
#include "pipe/p_state.h"

/*
 * resource
 */
struct nvnx_resource {
   struct pipe_resource	base;
   NvBuffer buffer;
   iova_t   gpu_addr;
   void*    cpu_addr;
};

static struct nvnx_resource *nvnx_resource(struct pipe_resource *res)
{
   return (struct nvnx_resource *)res;
}
