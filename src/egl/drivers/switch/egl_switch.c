/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2014 Adrián Arroyo Calle <adrian.arroyocalle@gmail.com>
 * Copyright (C) 2018 Jules Blok
 * Copyright (C) 2018-2019 fincs
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

#include "target-helpers/inline_debug_helper.h"

#include "nouveau/switch/nouveau_switch_public.h"

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
#include "state_tracker/drm_driver.h"

#include "mapi/glapi/glapi.h"


#ifdef DEBUG
#	define TRACE(x...) _eglLog(_EGL_DEBUG, "egl_switch: " x)
#	define CALLED() TRACE("CALLED: %s\n", __PRETTY_FUNCTION__)
#else
#	define TRACE(x...)
#  define CALLED()
#endif
#define ERROR(x...) _eglLog(_EGL_FATAL, "egl_switch: " x)

_EGL_DRIVER_STANDARD_TYPECASTS(switch_egl)

struct switch_egl_display
{
    struct st_manager *stmgr;
    struct st_api *stapi;
};

struct switch_egl_config
{
    _EGLConfig base;
    struct st_visual stvis;
};

struct switch_egl_context
{
    _EGLContext base;
    struct st_context_iface *stctx;
};

struct switch_egl_surface
{
    _EGLSurface base;
    struct st_framebuffer_iface *stfbi;
    struct pipe_resource *textures[ST_ATTACHMENT_COUNT];
    NvFence fences[2];
    bool fence_swap;
};

struct switch_framebuffer
{
   struct st_framebuffer_iface base;
   struct switch_egl_display* display;
   struct switch_egl_surface* surface;
   struct pipe_resource template;
};

static inline struct switch_framebuffer *
switch_framebuffer(struct st_framebuffer_iface *stfbi)
{
    return (struct switch_framebuffer *)stfbi;
}

static uint32_t drifb_ID = 0;

static boolean
switch_st_framebuffer_flush_front(struct st_context_iface *stctx, struct st_framebuffer_iface *stfbi,
                   enum st_attachment_type statt)
{
    // TODO: Figure out if we need to implement this at all.
    return TRUE;
}

static boolean
switch_st_framebuffer_validate(struct st_context_iface *stctx, struct st_framebuffer_iface *stfbi,
                   const enum st_attachment_type *statts, unsigned count, struct pipe_resource **out)
{
    struct switch_framebuffer *fb = switch_framebuffer(stfbi);
    struct switch_egl_surface *surface = fb->surface;
    struct pipe_screen *screen = stfbi->state_manager->screen;
    enum st_attachment_type i;
    CALLED();

    for (i = 0; i < count; i++)
    {
        enum pipe_format format = PIPE_FORMAT_NONE;
        unsigned bind = 0;
        struct winsys_handle whandle;
        struct pipe_resource* res;

        if (statts[i] == ST_ATTACHMENT_FRONT_LEFT || statts[i] == ST_ATTACHMENT_BACK_LEFT)
        {
            u32 index = (statts[i] == ST_ATTACHMENT_FRONT_LEFT) ? 1 : 0;
            format = stfbi->visual->color_format;
            bind = PIPE_BIND_RENDER_TARGET;
            whandle.type = WINSYS_HANDLE_TYPE_SHARED;
            whandle.handle = gfxGetFramebufferHandle(index, &whandle.offset);
            whandle.stride = gfxGetFramebufferPitch();
        }
        else if (statts[i] == ST_ATTACHMENT_DEPTH_STENCIL)
        {
            format = stfbi->visual->depth_stencil_format;
            bind = PIPE_BIND_DEPTH_STENCIL;
        }
        else if (statts[i] == ST_ATTACHMENT_ACCUM)
        {
            format = stfbi->visual->accum_format;
            bind = PIPE_BIND_RENDER_TARGET;
        }

        fb->template.format = format;
        fb->template.bind = bind;
        res = surface->textures[statts[i]];
        if (!res)
        {
            if (statts[i] == ST_ATTACHMENT_FRONT_LEFT || statts[i] == ST_ATTACHMENT_BACK_LEFT)
                res = screen->resource_from_handle(screen, &fb->template, &whandle, 0);
            else
                res = screen->resource_create(screen, &fb->template);
            surface->textures[statts[i]] = res;
        }
        if (pipe_reference(&out[i]->reference, &res->reference)) {
            screen->resource_destroy(screen, out[i]);
        }
        out[i] = res;
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
    struct switch_egl_surface *surface;
    struct switch_framebuffer *fb;
    struct switch_egl_display *display = switch_egl_display(dpy);
    struct switch_egl_config *config = switch_egl_config(conf);
    u32 width, height;
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

    fb = (struct switch_framebuffer *) calloc(1, sizeof (*fb));
    if (!fb)
    {
        _eglError(EGL_BAD_ALLOC, "switch_create_window_surface");
        goto cleanup;
    }

    gfxGetFramebufferResolution(&width, &height);

    fb->display = display;
    fb->surface = surface;
    fb->template.target = PIPE_TEXTURE_RECT;
    fb->template.width0 = (u16)width;
    fb->template.height0 = (u16)height;
    fb->template.depth0 = 1;
    fb->template.array_size = 1;
    fb->template.usage = PIPE_USAGE_DEFAULT;

    surface->stfbi = &fb->base;
    surface->base.SwapInterval = 1;
    surface->fences[0].id = UINT32_MAX;
    surface->fences[1].id = UINT32_MAX;

    /* setup the st_framebuffer_iface */
    fb->base.visual = &config->stvis;
    fb->base.flush_front = switch_st_framebuffer_flush_front;
    fb->base.validate = switch_st_framebuffer_validate;
    fb->base.flush_swapbuffers = switch_st_framebuffer_flush_swapbuffers;
    p_atomic_set(&fb->base.stamp, 0);
    fb->base.ID = p_atomic_inc_return(&drifb_ID);
    fb->base.state_manager = display->stmgr;

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
    enum st_attachment_type i;
    struct switch_egl_surface* surface = switch_egl_surface(surf);
    struct pipe_screen *screen = surface->stfbi->state_manager->screen;
    CALLED();

    if (_eglPutSurface(surf)) {
        for (i = 0; i < ST_ATTACHMENT_COUNT; i ++) {
            if (pipe_reference(&surface->textures[i]->reference, NULL)) {
                screen->resource_destroy(screen, surface->textures[i]);
            }
        }
        // XXX: detach switch_egl_surface::gl from the native window and destroy it
        free(surface->stfbi);
        free(surface);
    }
    return EGL_TRUE;
}


static EGLBoolean
switch_add_config(_EGLDisplay *dpy, EGLint *id, enum pipe_format colorfmt, enum pipe_format depthfmt)
{
    CALLED();

    struct switch_egl_config* conf;
    conf = (struct switch_egl_config*) calloc(1, sizeof (*conf));
    if (!conf)
        return _eglError(EGL_BAD_ALLOC, "switch_add_config failed to alloc");

    TRACE("Initializing config\n");
    _eglInitConfig(&conf->base, dpy, ++*id);

    // General configuration
    conf->base.NativeRenderable = EGL_TRUE;
    conf->base.SurfaceType = EGL_WINDOW_BIT; // we only support creating window surfaces
    conf->base.RenderableType = EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT_KHR;
    conf->base.Conformant = conf->base.RenderableType;
    conf->base.MinSwapInterval = 1;
    conf->base.MaxSwapInterval = 100; // arbitrary limit

    // Color buffer configuration
    conf->base.RedSize    = util_format_get_component_bits(colorfmt, UTIL_FORMAT_COLORSPACE_RGB, 0);
    conf->base.GreenSize  = util_format_get_component_bits(colorfmt, UTIL_FORMAT_COLORSPACE_RGB, 1);
    conf->base.BlueSize   = util_format_get_component_bits(colorfmt, UTIL_FORMAT_COLORSPACE_RGB, 2);
    conf->base.AlphaSize  = util_format_get_component_bits(colorfmt, UTIL_FORMAT_COLORSPACE_RGB, 3);
    conf->base.BufferSize = conf->base.RedSize+conf->base.GreenSize+conf->base.BlueSize+conf->base.AlphaSize;

    // Depth/stencil buffer configuration
    conf->base.DepthSize   = util_format_get_component_bits(depthfmt, UTIL_FORMAT_COLORSPACE_ZS, 0);
    conf->base.StencilSize = util_format_get_component_bits(depthfmt, UTIL_FORMAT_COLORSPACE_ZS, 1);

    // Visual
    conf->stvis.buffer_mask = ST_ATTACHMENT_FRONT_LEFT_MASK | ST_ATTACHMENT_BACK_LEFT_MASK;
    conf->stvis.color_format = colorfmt;
    conf->stvis.depth_stencil_format = depthfmt;
    conf->stvis.accum_format = PIPE_FORMAT_R16G16B16A16_FLOAT;
    conf->stvis.render_buffer = ST_ATTACHMENT_BACK_LEFT_MASK;

    if (!_eglValidateConfig(&conf->base, EGL_FALSE)) {
        _eglLog(_EGL_DEBUG, "Switch: failed to validate config");
        free(conf);
        return EGL_FALSE;
    }

    _eglLinkConfig(&conf->base);
    return EGL_TRUE;
}


static EGLBoolean
switch_add_configs_for_visuals(_EGLDisplay *dpy)
{
    CALLED();

    // List of supported color buffer formats
    static const enum pipe_format colorfmts[] = {
        PIPE_FORMAT_R8G8B8A8_UNORM,

        // TODO: Support these once we eschew the old libnx gfx api
        //PIPE_FORMAT_R8G8B8X8_UNORM,
        //PIPE_FORMAT_B5G6R5_UNORM,
    };

    // List of supported depth buffer formats
    static const enum pipe_format depthfmts[] = {
        PIPE_FORMAT_NONE,
        PIPE_FORMAT_S8_UINT,
        PIPE_FORMAT_Z16_UNORM,
        PIPE_FORMAT_Z24X8_UNORM,
        PIPE_FORMAT_Z24_UNORM_S8_UINT,
        PIPE_FORMAT_Z32_FLOAT,
        PIPE_FORMAT_Z32_FLOAT_S8X24_UINT,
    };

    // Add all combinations of color/depth buffer formats
    EGLint config_id = 0;
    EGLint i, j;
    for (i = 0; i < sizeof(colorfmts)/sizeof(colorfmts[0]); i ++) {
        for (j = 0; j < sizeof(depthfmts)/sizeof(depthfmts[0]); j ++) {
            EGLBoolean rc = switch_add_config(dpy, &config_id, colorfmts[i], depthfmts[j]);
            if (!rc)
                return rc;
        }
    }

    return EGL_TRUE;
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
    struct pipe_screen *screen;
    CALLED();

    if (!switch_add_configs_for_visuals(dpy))
        return EGL_FALSE;

    display = (struct switch_egl_display*) calloc(1, sizeof (*display));
    if (!display) {
        _eglError(EGL_BAD_ALLOC, "switch_initialize");
        return EGL_FALSE;
    }
    dpy->DriverData = display;
    dpy->Version = 14;

    dpy->ClientAPIs = 0;
    if (_eglIsApiValid(EGL_OPENGL_API))
        dpy->ClientAPIs |= EGL_OPENGL_BIT;
    if (_eglIsApiValid(EGL_OPENGL_ES_API))
        dpy->ClientAPIs |= EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT_KHR;

    dpy->Extensions.KHR_create_context = EGL_TRUE;
    dpy->Extensions.KHR_surfaceless_context = EGL_TRUE;
  
    stmgr = CALLOC_STRUCT(st_manager);
    if (!stmgr) {
        _eglError(EGL_BAD_ALLOC, "switch_initialize");
        return EGL_FALSE;
    }

    stmgr->get_param = switch_st_get_param;

    gfxInitDefault();
    gfxSetMode(GfxMode_TiledDouble);

    /* Create nouveau screen */
    TRACE("Creating nouveau screen\n");
    screen = nouveau_switch_screen_create();
    if (!screen)
    {
        TRACE("Failed to create nouveau screen\n");
        return EGL_FALSE;
    }

    /* Inject optional trace, debug, etc. wrappers */
    TRACE("Wrapping screen\n");
    stmgr->screen = debug_screen_wrap(screen);

    display->stmgr = stmgr;
    display->stapi = st_gl_api_create();
    return EGL_TRUE;
}


static EGLBoolean
switch_terminate(_EGLDriver* drv, _EGLDisplay* dpy)
{
    struct switch_egl_display *display = switch_egl_display(dpy);
    CALLED();

    /* Release all non-current Context/Surfaces. */
    _eglReleaseDisplayResources(drv, dpy);

    _eglCleanupDisplay(dpy);

    display->stapi->destroy(display->stapi);

    display->stmgr->screen->destroy(display->stmgr->screen);
    free(display->stmgr);

    gfxExit();
    free(display);

    return EGL_TRUE;
}


static _EGLContext*
switch_create_context(_EGLDriver *drv, _EGLDisplay *dpy, _EGLConfig *conf,
    _EGLContext *share_list, const EGLint *attrib_list)
{
    struct switch_egl_context *context;
    struct switch_egl_display *display = switch_egl_display(dpy);
    struct switch_egl_config *config = switch_egl_config(conf);
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
    
    attribs.major = context->base.ClientMajorVersion;
    attribs.minor = context->base.ClientMinorVersion;
    attribs.visual = config->stvis;

    switch (eglQueryAPI()) {
        case EGL_OPENGL_API:
            switch (context->base.Profile) {
                case EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR:
                    /* There are no profiles before OpenGL 3.2.  The
                     * EGL_KHR_create_context spec says:
                     *
                     *     "If the requested OpenGL version is less than 3.2,
                     *      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR is ignored and the functionality
                     *      of the context is determined solely by the requested version.."
                     */

                    if (attribs.major > 3 || (attribs.major == 3 && attribs.minor >= 2)) {
                        attribs.profile = ST_PROFILE_OPENGL_CORE;
                        break;
                    }
                    /* fall-through */
                case EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR:
                    attribs.profile = ST_PROFILE_DEFAULT;
                    break;
                default:
                    _eglError(EGL_BAD_CONFIG, "switch_create_context");
                    goto cleanup;
            }
            break;
        case EGL_OPENGL_ES_API:
            switch (context->base.ClientMajorVersion) {
            case 1:
                attribs.profile = ST_PROFILE_OPENGL_ES1;
                break;
            case 2:
            case 3: // ST_PROFILE_OPENGL_ES2 is used for OpenGL ES 3.x too
                attribs.profile = ST_PROFILE_OPENGL_ES2;
                break;
            default:
                _eglError(EGL_BAD_CONFIG, "switch_create_context");
                goto cleanup;
            }
            break;
        default:
            _eglError(EGL_BAD_CONFIG, "switch_create_context");
            goto cleanup;
    }

    enum st_context_error error;
    context->stctx = display->stapi->create_context(display->stapi, display->stmgr, &attribs, &error, NULL);
    if (error != ST_CONTEXT_SUCCESS) {
        _eglError(EGL_BAD_MATCH, "switch_create_context");
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
        context->stctx->destroy(context->stctx);
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
    struct switch_egl_surface* draw_surf = switch_egl_surface(dsurf);
    struct switch_egl_surface* read_surf = switch_egl_surface(rsurf);
    CALLED();

    _EGLContext *old_ctx;
    _EGLSurface *old_dsurf, *old_rsurf;

    if (!_eglBindContext(ctx, dsurf, rsurf, &old_ctx, &old_dsurf, &old_rsurf))
        return EGL_FALSE;

    EGLBoolean ret = disp->stapi->make_current(disp->stapi, cont ? cont->stctx : NULL,
        draw_surf ? draw_surf->stfbi : NULL, read_surf ? read_surf->stfbi : NULL);

    if (old_ctx) {
        if (old_dsurf) {
            switch_destroy_surface(drv, dpy, old_dsurf);
        }
        if (old_rsurf) {
            switch_destroy_surface(drv, dpy, old_rsurf);
        }
        switch_destroy_context(drv, dpy, old_ctx);
    }

    return ret;
}

static EGLBoolean
switch_swap_buffers(_EGLDriver *drv, _EGLDisplay *dpy, _EGLSurface *surf)
{
    CALLED();
    struct switch_egl_surface* surface = switch_egl_surface(surf);
    struct switch_egl_context* context = switch_egl_context(surface->base.CurrentContext);

    TRACE("Flushing context\n");
    context->stctx->flush(context->stctx, ST_FLUSH_END_OF_FRAME, NULL);

    NvFence fence;
    struct pipe_resource *old_back = surface->textures[ST_ATTACHMENT_BACK_LEFT];
    fence.id = nouveau_switch_resource_get_syncpoint(old_back, &fence.value);
    if ((int)fence.id >= 0) {
        NvFence* surf_fence = &surface->fences[surface->fence_swap];
        if (surf_fence->id != fence.id || surf_fence->value != fence.value) {
            TRACE("Appending fence: {%d,%u}\n", (int)fence.id, fence.value);
            *surf_fence = fence;

            NvMultiFence mf;
            nvMultiFenceCreate(&mf, &fence);
            gfxAppendFence(&mf);
        }
    }

    TRACE("Swapping out buffers\n");
    gfxSwapBuffers();

    // Swap buffer attachments and invalidate framebuffer
    surface->fence_swap = !surface->fence_swap;
    surface->textures[ST_ATTACHMENT_BACK_LEFT] = surface->textures[ST_ATTACHMENT_FRONT_LEFT];
    surface->textures[ST_ATTACHMENT_FRONT_LEFT] = old_back;
    p_atomic_inc(&surface->stfbi->stamp);
    return EGL_TRUE;
}


/*
 * Called from eglGetProcAddress() via drv->API.GetProcAddress().
 */
static _EGLProc
switch_get_proc_address(_EGLDriver *drv, const char *procname)
{
    return _glapi_get_proc_address(procname);
}


/**
 * This is the main entrypoint into the driver, called by libEGL.
 * Create a new _EGLDriver object and init its dispatch table.
 */
void
_eglInitDriver(_EGLDriver *driver)
{
    CALLED();

    driver->API.Initialize = switch_initialize;
    driver->API.Terminate = switch_terminate;
    driver->API.CreateContext = switch_create_context;
    driver->API.DestroyContext = switch_destroy_context;
    driver->API.MakeCurrent = switch_make_current;
    driver->API.CreateWindowSurface = switch_create_window_surface;
    driver->API.CreatePixmapSurface = switch_create_pixmap_surface;
    driver->API.CreatePbufferSurface = switch_create_pbuffer_surface;
    driver->API.DestroySurface = switch_destroy_surface;
    driver->API.GetProcAddress = switch_get_proc_address;

    driver->API.SwapBuffers = switch_swap_buffers;
}
