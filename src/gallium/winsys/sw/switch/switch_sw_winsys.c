/**************************************************************************
 *
 * Copyright 2010 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 **************************************************************************/

/**
 * @file
 * Switch software rasterizer winsys.
 *
 * There is no present support. Framebuffer data needs to be obtained via
 * transfers.
 *
 * @author Jules Blok
 */

#include <stdio.h>
#include <switch.h>

#include "switch_sw_winsys.h"
#include "pipe/p_format.h"
#include "util/u_debug.h"
#include "util/u_memory.h"
#include "state_tracker/sw_winsys.h"
#include "state_tracker/drm_driver.h"

#ifdef DEBUG
#	define TRACE(x...) debug_printf("egl_switch: " x)
#	define CALLED() TRACE("CALLED: %s\n", __PRETTY_FUNCTION__)
#else
#	define TRACE(x...)
#  define CALLED()
#endif
#define ERROR(x...) debug_printf("egl_switch: " x)

static boolean
switch_sw_is_displaytarget_format_supported(struct sw_winsys *ws,
                                          unsigned tex_usage,
                                          enum pipe_format format )
{
   return format == PIPE_FORMAT_B8G8R8A8_UNORM;
}


static void *
switch_sw_displaytarget_map(struct sw_winsys *ws,
                          struct sw_displaytarget *dt,
                          unsigned flags )
{
   CALLED();
   return dt;
}


static void
switch_sw_displaytarget_unmap(struct sw_winsys *ws,
                            struct sw_displaytarget *dt )
{
   CALLED();
}


static void
switch_sw_displaytarget_destroy(struct sw_winsys *winsys,
                              struct sw_displaytarget *dt)
{
   CALLED();
   assert(0);
}


static struct sw_displaytarget *
switch_sw_displaytarget_create(struct sw_winsys *winsys,
                             unsigned tex_usage,
                             enum pipe_format format,
                             unsigned width, unsigned height,
                             unsigned alignment,
                             const void *front_private,
                             unsigned *stride)
{
   CALLED();
   fprintf(stderr, "switch_sw_displaytarget_create() returning NULL\n");
   return NULL;
}


static struct sw_displaytarget *
switch_sw_displaytarget_from_handle(struct sw_winsys *winsys,
                                  const struct pipe_resource *templat,
                                  struct winsys_handle *whandle,
                                  unsigned *stride)
{
   CALLED();
   *stride = whandle->stride;
   return (void*)gfxGetFramebuffer(NULL, NULL);
}


static boolean
switch_sw_displaytarget_get_handle(struct sw_winsys *winsys,
                                 struct sw_displaytarget *dt,
                                 struct winsys_handle *whandle)
{
   CALLED();
   assert(0);
   return FALSE;
}


static void
switch_sw_displaytarget_display(struct sw_winsys *winsys,
                              struct sw_displaytarget *dt,
                              void *context_private,
                              struct pipe_box *box)
{
   CALLED();
   assert(0);
}


static void
switch_sw_destroy(struct sw_winsys *winsys)
{
   CALLED();
   FREE(winsys);
}


struct sw_winsys *
switch_sw_create(void)
{
   CALLED();
   static struct sw_winsys *winsys;

   winsys = CALLOC_STRUCT(sw_winsys);
   if (!winsys)
      return NULL;

   winsys->destroy = switch_sw_destroy;
   winsys->is_displaytarget_format_supported = switch_sw_is_displaytarget_format_supported;
   winsys->displaytarget_create = switch_sw_displaytarget_create;
   winsys->displaytarget_from_handle = switch_sw_displaytarget_from_handle;
   winsys->displaytarget_get_handle = switch_sw_displaytarget_get_handle;
   winsys->displaytarget_map = switch_sw_displaytarget_map;
   winsys->displaytarget_unmap = switch_sw_displaytarget_unmap;
   winsys->displaytarget_display = switch_sw_displaytarget_display;
   winsys->displaytarget_destroy = switch_sw_displaytarget_destroy;

   return winsys;
}
