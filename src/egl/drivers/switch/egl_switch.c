/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2014 Adrián Arroyo Calle <adrian.arroyocalle@gmail.com>
 * Copyright (C) 2018 Jules Blok
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "eglconfig.h"
#include "eglcontext.h"
#include "egldisplay.h"
#include "egldriver.h"
#include "eglcurrent.h"
#include "egllog.h"
#include "eglsurface.h"
#include "eglimage.h"
#include "egltypedefs.h"

#include <switch.h>

#include "target-helpers/inline_sw_helper.h"
#include "target-helpers/inline_debug_helper.h"

#include "sw/null/null_sw_winsys.h"

#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"

#include "util/u_atomic.h"
#include "util/u_box.h"
#include "util/u_debug.h"
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"

#include "state_tracker/st_api.h"
#include "state_tracker/st_gl_api.h"

#ifdef DEBUG
#   define CALLED() TRACE(__PRETTY_FUNCTION__)
#   define TRACE(x) svcOutputDebugString(x, sizeof(x))
#else
#   define CALLED()
#   define TRACE(x)
#endif

_EGL_DRIVER_STANDARD_TYPECASTS(switch_egl)

struct switch_egl_display
{
    struct st_manager *stmgr;
    struct st_api *stapi;
};

struct switch_egl_config
{
    _EGLConfig base;
};

struct switch_egl_context
{
    _EGLContext    base;
    struct st_context_iface *ctx;
};

struct switch_egl_surface
{
    _EGLSurface base;
    struct st_framebuffer_iface *stfbi;
    struct st_visual stvis;
    struct pipe_resource *textures[ST_ATTACHMENT_COUNT];

    //Binder session;
    //bufferProducerQueueBufferInput input;
    //bufferProducerQueueBufferOutput output;

    //u8 *framebuf;
    //size_t size;
};

static uint32_t drifb_ID = 0;


static bool
GetNativeWindowID(u8* buf, s32 *out_ID)
{
    CALLED();
    u32 *bufptr = (u32*)buf;
    if(bufptr[0] < 0xc)
        return false;

    //bufptr = start of ParcelData
    bufptr = (u32*)&buf[bufptr[1]];
    *out_ID = (s32)bufptr[2];
    return true;
}

static void
switch_fill_st_visual(struct st_visual *visual, _EGLConfig *conf)
{
    CALLED();
    // TODO: Create the visual from the config
    struct st_visual stvis = {
        ST_ATTACHMENT_FRONT_LEFT_MASK,
        PIPE_FORMAT_RGBA8888_UNORM,
        PIPE_FORMAT_NONE,
        PIPE_FORMAT_NONE,
        1,
        ST_ATTACHMENT_FRONT_LEFT
    };
    *visual = stvis;
}

static inline struct switch_egl_surface *
stfbi_to_surface(struct st_framebuffer_iface *stfbi)
{
    CALLED();
    return (struct switch_egl_surface *) stfbi->st_manager_private;
}

static boolean
switch_st_framebuffer_flush_front(struct st_context_iface *stctx, struct st_framebuffer_iface *stfbi,
                   enum st_attachment_type statt)
{
    struct switch_egl_surface *surface = stfbi_to_surface(stfbi);
    struct pipe_context *pipe = stctx->pipe;
    struct pipe_resource *res = surface->textures[statt];
    struct pipe_transfer *transfer = NULL;
    struct pipe_box box;
    void *map;
    ubyte *src, *dst;
    unsigned y, bytes, bpp, width;
    int dst_stride;
    CALLED();

    u_box_2d(0, 0, res->width0, res->height0, &box);

    map = pipe->transfer_map(pipe, res, 0, PIPE_TRANSFER_READ, &box,
                            &transfer);

    /*
    * Copy the color buffer from the resource to the user's buffer.
    */
    bpp = util_format_get_blocksize(surface->stvis.color_format);
    src = map;
    dst = gfxGetFramebuffer(&width, NULL);
    dst_stride = bpp * width;
    bytes = bpp * res->width0;

    for (y = 0; y < res->height0; y++) {
      memcpy(dst, src, bytes);
      dst += dst_stride;
      src += transfer->stride;
    }

    pipe->transfer_unmap(pipe, transfer);

    return TRUE;
}

static boolean
switch_st_framebuffer_validate(struct st_context_iface *stctx, struct st_framebuffer_iface *stfbi,
                   const enum st_attachment_type *statts, unsigned count, struct pipe_resource **out)
{
    struct pipe_screen *screen = stfbi->state_manager->screen;
    enum st_attachment_type i;
    struct switch_egl_surface *surface = stfbi_to_surface(stfbi);
    struct pipe_resource templat;
    u32 width, height;
    CALLED();

    gfxGetFramebufferResolution(&width, &height);
    memset(&templat, 0, sizeof(templat));
    templat.target = PIPE_TEXTURE_RECT;
    templat.format = 0; /* setup below */
    templat.last_level = 0;
    templat.width0 = (u16)width;
    templat.height0 = (u16)height;
    templat.depth0 = 1;
    templat.array_size = 1;
    templat.usage = PIPE_USAGE_DEFAULT;
    templat.bind = 0; /* setup below */
    templat.flags = 0;

    for (i = 0; i < count; i++)
    {
        enum pipe_format format = PIPE_FORMAT_NONE;
        unsigned bind = 0;

        /*
        * At this time, we really only need to handle the front-left color
        * attachment, since that's all we specified for the visual in
        * osmesa_init_st_visual().
        */
        if (statts[i] == ST_ATTACHMENT_FRONT_LEFT)
        {
            format = surface->stvis.color_format;
            bind = PIPE_BIND_RENDER_TARGET;
        }
        else if (statts[i] == ST_ATTACHMENT_DEPTH_STENCIL)
        {
            format = surface->stvis.depth_stencil_format;
            bind = PIPE_BIND_DEPTH_STENCIL;
        }
        else if (statts[i] == ST_ATTACHMENT_ACCUM)
        {
            format = surface->stvis.accum_format;
            bind = PIPE_BIND_RENDER_TARGET;
        }

        templat.format = format;
        templat.bind = bind;
        pipe_resource_reference(&out[i], NULL);
        out[i] = surface->textures[statts[i]] = screen->resource_create(screen, &templat);
    }

    return TRUE;
}

static boolean
switch_st_framebuffer_flush_swapbuffers(struct st_context_iface *stctx, struct st_framebuffer_iface *stfbi)
{
    return TRUE;
}

/**
 * Called via eglCreateWindowSurface(), drv->API.CreateWindowSurface().
 */
static _EGLSurface *
switch_create_window_surface(_EGLDriver *drv, _EGLDisplay *dpy,
    _EGLConfig *conf, void *native_window, const EGLint *attrib_list)
{
    //Result rc;
    struct switch_egl_surface* surface;
    struct switch_egl_display *display = switch_egl_display(dpy);
    CALLED();

    surface = (struct switch_egl_surface*) calloc(1, sizeof (*surface));
    if (!surface)
    {
        _eglError(EGL_BAD_ALLOC, "switch_create_window_surface");
        return NULL;
    }

    if (!_eglInitSurface(&surface->base, dpy, EGL_WINDOW_BIT,
        conf, attrib_list))
    {
        goto cleanup;
    }

    surface->stfbi = (struct st_framebuffer_iface*) calloc(1, sizeof (*surface->stfbi));
    if (!surface->stfbi)
    {
        _eglError(EGL_BAD_ALLOC, "switch_create_window_surface");
        goto cleanup;
    }

    surface->base.SwapInterval = 1;

    /*s32 window_id;
    if (!GetNativeWindowID((u8*)native_window, &window_id))
    {
        _eglError(EGL_BAD_NATIVE_WINDOW, "GetNativeWindowID");
        goto cleanup;
    }

    binderCreateSession(&surface->session, viGetSession_IHOSBinderDriverRelay()->handle, window_id);
    rc = binderInitSession(&surface->session, 0x0f);
    if (R_FAILED(rc))
    {
        _eglError(EGL_BAD_NATIVE_WINDOW, "binderInitSession");
        goto cleanup;
    }

    rc = bufferProducerInitialize(&surface->session);
    if (R_FAILED(rc))
    {
        _eglError(EGL_BAD_NATIVE_WINDOW, "bufferProducerInitialize");
        goto cleanup;
    }

    rc = bufferProducerConnect(NATIVE_WINDOW_API_CPU, 0, &surface->output);
    if (R_FAILED(rc))
    {
        _eglError(EGL_BAD_NATIVE_WINDOW, "bufferProducerConnect");
        goto cleanup;
    }*/

    switch_fill_st_visual(&surface->stvis, conf);

    /* setup the st_framebuffer_iface */
    surface->stfbi->visual = &surface->stvis;
    surface->stfbi->flush_front = switch_st_framebuffer_flush_front;
    surface->stfbi->validate = switch_st_framebuffer_validate;
    surface->stfbi->flush_swapbuffers = switch_st_framebuffer_flush_swapbuffers;
    surface->stfbi->st_manager_private = (void *)surface;
    p_atomic_set(&surface->stfbi->stamp, 0);
    surface->stfbi->ID = p_atomic_inc_return(&drifb_ID);
    surface->stfbi->state_manager = display->stmgr;

    return &surface->base;

cleanup:
    free(surface);
    return NULL;
}


static _EGLSurface *
switch_create_pixmap_surface(_EGLDriver *drv, _EGLDisplay *disp,
    _EGLConfig *conf, void *native_pixmap, const EGLint *attrib_list)
{
    CALLED();
    return NULL;
}


static _EGLSurface *
switch_create_pbuffer_surface(_EGLDriver *drv, _EGLDisplay *disp,
    _EGLConfig *conf, const EGLint *attrib_list)
{
    CALLED();
    return NULL;
}


static EGLBoolean
switch_destroy_surface(_EGLDriver *drv, _EGLDisplay *disp, _EGLSurface *surf)
{
    CALLED();
    if (_eglPutSurface(surf)) {
        // XXX: detach switch_egl_surface::gl from the native window and destroy it
        free(surf);
    }
    return EGL_TRUE;
}


static EGLBoolean
switch_add_configs_for_visuals(_EGLDisplay *dpy)
{
    struct switch_egl_config* conf;
    CALLED();
    conf = (struct switch_egl_config*) calloc(1, sizeof (*conf));
    if (!conf)
        return _eglError(EGL_BAD_ALLOC, "switch_add_configs_for_visuals failed to alloc");

    TRACE("Initializing config");
    _eglInitConfig(&conf->base, dpy, 1);

    _eglSetConfigKey(&conf->base, EGL_RED_SIZE, 8);
    _eglSetConfigKey(&conf->base, EGL_BLUE_SIZE, 8);
    _eglSetConfigKey(&conf->base, EGL_GREEN_SIZE, 8);
    _eglSetConfigKey(&conf->base, EGL_LUMINANCE_SIZE, 0);
    _eglSetConfigKey(&conf->base, EGL_ALPHA_SIZE, 8);
    _eglSetConfigKey(&conf->base, EGL_COLOR_BUFFER_TYPE, EGL_RGB_BUFFER);
    EGLint r = (_eglGetConfigKey(&conf->base, EGL_RED_SIZE)
        + _eglGetConfigKey(&conf->base, EGL_GREEN_SIZE)
        + _eglGetConfigKey(&conf->base, EGL_BLUE_SIZE)
        + _eglGetConfigKey(&conf->base, EGL_ALPHA_SIZE));
    _eglSetConfigKey(&conf->base, EGL_BUFFER_SIZE, r);
    _eglSetConfigKey(&conf->base, EGL_CONFIG_CAVEAT, EGL_NONE);
    _eglSetConfigKey(&conf->base, EGL_CONFIG_ID, 1);
    _eglSetConfigKey(&conf->base, EGL_BIND_TO_TEXTURE_RGB, EGL_FALSE);
    _eglSetConfigKey(&conf->base, EGL_BIND_TO_TEXTURE_RGBA, EGL_FALSE);
    _eglSetConfigKey(&conf->base, EGL_STENCIL_SIZE, 0);
    _eglSetConfigKey(&conf->base, EGL_TRANSPARENT_TYPE, EGL_NONE);
    _eglSetConfigKey(&conf->base, EGL_NATIVE_RENDERABLE, EGL_TRUE); // Let's say yes
    _eglSetConfigKey(&conf->base, EGL_NATIVE_VISUAL_ID, 0); // No visual
    _eglSetConfigKey(&conf->base, EGL_NATIVE_VISUAL_TYPE, EGL_NONE); // No visual
    _eglSetConfigKey(&conf->base, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT);
    _eglSetConfigKey(&conf->base, EGL_SAMPLE_BUFFERS, 0); // TODO: How to get the right value ?
    _eglSetConfigKey(&conf->base, EGL_SAMPLES, _eglGetConfigKey(&conf->base, EGL_SAMPLE_BUFFERS) == 0 ? 0 : 0);
    _eglSetConfigKey(&conf->base, EGL_DEPTH_SIZE, 24); // TODO: How to get the right value ?
    _eglSetConfigKey(&conf->base, EGL_LEVEL, 0);
    _eglSetConfigKey(&conf->base, EGL_MAX_PBUFFER_WIDTH, 0); // TODO: How to get the right value ?
    _eglSetConfigKey(&conf->base, EGL_MAX_PBUFFER_HEIGHT, 0); // TODO: How to get the right value ?
    _eglSetConfigKey(&conf->base, EGL_MAX_PBUFFER_PIXELS, 0); // TODO: How to get the right value ?
    _eglSetConfigKey(&conf->base, EGL_SURFACE_TYPE, EGL_WINDOW_BIT /*| EGL_PIXMAP_BIT | EGL_PBUFFER_BIT*/);

    if (!_eglValidateConfig(&conf->base, EGL_FALSE)) {
        _eglLog(_EGL_DEBUG, "Switch: failed to validate config");
        goto cleanup;
    }

    _eglLinkConfig(&conf->base);
    if (!_eglGetArraySize(dpy->Configs)) {
        _eglLog(_EGL_WARNING, "Switch: failed to create any config");
        goto cleanup;
    }

    return EGL_TRUE;

cleanup:
    free(conf);
    return EGL_FALSE;
}

/**
 * Called from the ST manager.
 */
static int
switch_st_get_param(struct st_manager *stmgr, enum st_manager_param param)
{
   /* no-op */
   return 0;
}

static EGLBoolean
switch_initialize(_EGLDriver *drv, _EGLDisplay *dpy)
{
    struct switch_egl_display *display;
    struct st_manager *stmgr;
    CALLED();

    if (dpy->Options.ForceSoftware)
        return EGL_FALSE;

    if (!switch_add_configs_for_visuals(dpy))
        return EGL_FALSE;

    dpy->Version = 14;
    stmgr = CALLOC_STRUCT(st_manager);
    if (!stmgr) {
        _eglError(EGL_BAD_ALLOC, "switch_initialize");
        return EGL_FALSE;
    }

    dpy->Extensions.KHR_no_config_context = EGL_TRUE;
    stmgr->get_param = switch_st_get_param;

    {
        struct sw_winsys *winsys;
        struct pipe_screen *screen;
        TRACE("Initializing gfx");
        gfxInitDefault();

        /* We use a null software winsys since we always just render to ordinary
        * driver resources.
        */
        TRACE("Initializing winsys");
        winsys = null_sw_create();
        if (!winsys)
            return EGL_FALSE;

        /* Create llvmpipe or softpipe screen */
        TRACE("Creating screen");
        screen = sw_screen_create(winsys);
        if (!screen)
        {
            _eglError(EGL_BAD_ALLOC, "sw_screen_create");
            winsys->destroy(winsys);
            return EGL_FALSE;
        }

        /* Inject optional trace, debug, etc. wrappers */
        TRACE("Wrapping screen");
        stmgr->screen = debug_screen_wrap(screen);
    }
    /*else
    {
        nvServiceType nv_servicetype;
        switch(__nx_applet_type)
        {
            case AppletType_Application:
            case AppletType_SystemApplication:
                nv_servicetype = NVSERVTYPE_Application;
                break;

            case AppletType_SystemApplet:
            case AppletType_LibraryApplet:
            case AppletType_OverlayApplet:
            default:
                nv_servicetype = NVSERVTYPE_Applet;
                break;
        }

        nvInitialize(nv_servicetype, 0x300000);
    }*/

    display = (struct switch_egl_display*) calloc(1, sizeof (*display));
    if (!display) {
        _eglError(EGL_BAD_ALLOC, "switch_initialize");
        return EGL_FALSE;
    }

    display->stmgr = stmgr;
    display->stapi = st_gl_api_create();
    dpy->DriverData = display;
    return EGL_TRUE;
}


static EGLBoolean
switch_terminate(_EGLDriver* drv,_EGLDisplay* dpy)
{
    CALLED();
    gfxExit();
    return EGL_TRUE;
}


static _EGLContext*
switch_create_context(_EGLDriver *drv, _EGLDisplay *dpy, _EGLConfig *conf,
    _EGLContext *share_list, const EGLint *attrib_list)
{
    struct switch_egl_context *context;
    struct switch_egl_display *display = switch_egl_display(dpy);
    CALLED();

    context = (struct switch_egl_context*) calloc(1, sizeof (*context));
    if (!context) {
        _eglError(EGL_BAD_ALLOC, "switch_create_context");
        return NULL;
    }

    if (!_eglInitContext(&context->base, dpy, conf, attrib_list))
        goto cleanup;

    struct st_context_attribs attribs;
    memset(&attribs, 0, sizeof(attribs));
    attribs.profile = ST_PROFILE_OPENGL_ES2;
    switch_fill_st_visual(&attribs.visual, conf);

    enum st_context_error error;
    context->ctx = display->stapi->create_context(display->stapi, display->stmgr, &attribs, &error, NULL);
    if (error != ST_CONTEXT_SUCCESS) {
        _eglError(EGL_BAD_ATTRIBUTE, "switch_create_context");
        goto cleanup;
    }

    return &context->base;

cleanup:
    free(context);
    return NULL;
}


static EGLBoolean
switch_destroy_context(_EGLDriver* drv, _EGLDisplay *disp, _EGLContext* ctx)
{
    struct switch_egl_context* context = switch_egl_context(ctx);
    CALLED();

    if (_eglPutContext(ctx))
    {
        context->ctx->destroy(context->ctx);
        free(context);
        ctx = NULL;
    }
    return EGL_TRUE;
}


static EGLBoolean
switch_make_current(_EGLDriver* drv, _EGLDisplay* dpy, _EGLSurface *dsurf,
    _EGLSurface *rsurf, _EGLContext *ctx)
{
    struct switch_egl_display* disp = switch_egl_display(dpy);
    struct switch_egl_context* cont = switch_egl_context(ctx);
    struct switch_egl_surface* surf = switch_egl_surface(dsurf);
    CALLED();

    _EGLContext *old_ctx;
    _EGLSurface *old_dsurf, *old_rsurf;

    if (!_eglBindContext(ctx, dsurf, rsurf, &old_ctx, &old_dsurf, &old_rsurf))
        return EGL_FALSE;

    return disp->stapi->make_current(disp->stapi, cont->ctx, surf->stfbi, surf->stfbi);
}


static EGLBoolean
switch_swap_buffers(_EGLDriver *drv, _EGLDisplay *dpy, _EGLSurface *surf)
{
    CALLED();
    gfxFlushBuffers();
    gfxSwapBuffers();
    gfxWaitForVsync();
    return EGL_TRUE;
}


/**
 * This is the main entrypoint into the driver, called by libEGL.
 * Create a new _EGLDriver object and init its dispatch table.
 */
_EGLDriver*
_eglBuiltInDriver(void)
{
    _EGLDriver* driver;
    CALLED();
    driver = (_EGLDriver*) calloc(1, sizeof(*driver));
    if (!driver) {
        _eglError(EGL_BAD_ALLOC, "_eglBuiltInDriver");
        return NULL;
    }

    _eglInitDriverFallbacks(driver);
    driver->API.Initialize = switch_initialize;
    driver->API.Terminate = switch_terminate;
    driver->API.CreateContext = switch_create_context;
    driver->API.DestroyContext = switch_destroy_context;
    driver->API.MakeCurrent = switch_make_current;
    driver->API.CreateWindowSurface = switch_create_window_surface;
    driver->API.CreatePixmapSurface = switch_create_pixmap_surface;
    driver->API.CreatePbufferSurface = switch_create_pbuffer_surface;
    driver->API.DestroySurface = switch_destroy_surface;

    driver->API.SwapBuffers = switch_swap_buffers;

    driver->Name = "Switch";

    return driver;
}
