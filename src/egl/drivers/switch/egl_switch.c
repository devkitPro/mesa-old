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


#ifdef DEBUG
#	define TRACE(x...) printf("egl_switch: " x)
#	define CALLED() TRACE("CALLED: %s\n", __PRETTY_FUNCTION__)
#else
#	define TRACE(x...)
# define CALLED()
#endif
#define ERROR(x...) printf("egl_switch: " x)

#define NUM_SWAP_BUFFERS 2

_EGL_DRIVER_STANDARD_TYPECASTS(switch_egl)

struct switch_egl_display
{
    struct st_manager *stmgr;
    struct st_api *stapi;

    struct pipe_screen *nvscreen;
    struct pipe_context *nvctx;

    u32 nvhostctrl;
    Handle VsyncEvent;
};

struct switch_egl_config
{
    _EGLConfig base;
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
    struct st_visual stvis;

    Binder session;
    bufferProducerQueueBufferOutput output;

    bufferProducerFence DequeueFence;
    s32 CurrentProducerBuffer;
    struct pipe_resource *buffers[NUM_SWAP_BUFFERS];
};

struct switch_framebuffer
{
   struct st_framebuffer_iface base;
   struct switch_egl_display* display;
   struct switch_egl_surface* surface;
};

static uint32_t drifb_ID = 0;
extern u32 __nx_applet_type;

static bufferProducerQueueBufferInput QueueBufferData = {
    .timestamp = 0x0,
    .isAutoTimestamp = 0x1,
    .crop = {0, 0, 1280, 720}, //Official apps which use multiple resolutions configure this for the currently used resolution, depending on the current appletOperationMode.
    .scalingMode = 0x0,
    .transform = NATIVE_WINDOW_TRANSFORM_FLIP_V,
    .stickyTransform = 0x0,
    .unk = {0x0, 0x1},

    .fence = {
        .is_valid = 0x1,
        .nv_fences = {
            {
            .id = 0xffffffff, //Official sw sets this to the output fence from the last nvioctlChannel_SubmitGPFIFO().
            .value = 0x0,
            },
            {0xffffffff, 0x0}, {0xffffffff, 0x0}, {0xffffffff, 0x0},
        },
    }
};

// Some of this struct is based on tegra_dc_ext_flip_windowattr.
static bufferProducerGraphicBuffer BufferInitData = {
    .magic = 0x47424652,//"RFBG"/'GBFR'
    .format = 0x1,
    .usage = 0xb00,

    .pid = 0x2a, //Official sw sets this to the output of "getpid()", which calls a func which is hard-coded for returning 0x2a.
    .refcount = 0x0,  //Official sw sets this to the output of "android_atomic_inc()".

    .numFds = 0x0,
    .numInts = sizeof(BufferInitData.data)>>2,//0x51

    .data = {
        .unk_x0 = 0xffffffff,
        .unk_x8 = 0x0,
        .unk_xc = 0xdaffcaff,
        .unk_x10 = 0x2a,
        .unk_x14 = 0,
        .unk_x18 = 0xb00,
        .unk_x1c = 0x1,
        .unk_x20 = 0x1,
        .unk_x2c = 0x1,
        .unk_x30 = 0,
        .flags = 0x532120,
        .unk_x40 = 0x1,
        .unk_x44 = 0x3,
        .unk_x54 = 0xfe,
        .unk_x58 = 0x4,
    }
};

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
        ST_ATTACHMENT_FRONT_LEFT_MASK | ST_ATTACHMENT_BACK_LEFT,
        PIPE_FORMAT_RGBA8888_UNORM,
        PIPE_FORMAT_NONE,
        PIPE_FORMAT_NONE,
        1,
        ST_ATTACHMENT_BACK_LEFT
    };
    *visual = stvis;
}

static inline struct switch_egl_display *
stfbi_to_display(struct st_framebuffer_iface *stfbi)
{
    return ((struct switch_framebuffer *)stfbi)->display;
}

static inline struct switch_egl_surface *
stfbi_to_surface(struct st_framebuffer_iface *stfbi)
{
    return ((struct switch_framebuffer *)stfbi)->surface;
}

static boolean
switch_st_framebuffer_flush_front(struct st_context_iface *stctx, struct st_framebuffer_iface *stfbi,
                   enum st_attachment_type statt)
{
#if 0
    struct switch_egl_display *display = stfbi_to_display(stfbi);
    struct switch_egl_surface *surface = stfbi_to_surface(stfbi);
    struct pipe_context *pipe = stctx->pipe;
    struct pipe_context *nvpipe = display->nvctx;
    struct pipe_resource *res = surface->textures[statt];
    struct pipe_transfer *transfer = NULL;
    struct pipe_transfer *nvtransfer = NULL;
    struct pipe_box box;
    void *map;
    ubyte *src, *dst;
    unsigned y, bytes, bpp, width = 1280;
    int dst_stride;
    CALLED();

    u_box_2d(0, 0, res->width0, res->height0, &box);

    map = pipe->transfer_map(pipe, res, 0, PIPE_TRANSFER_READ, &box,
                            &transfer);
    dst = nvpipe->transfer_map(nvpipe, surface->buffers[surface->CurrentProducerBuffer],
                              0, PIPE_TRANSFER_WRITE | PIPE_TRANSFER_UNSYNCHRONIZED | PIPE_TRANSFER_MAP_DIRECTLY, &box, &nvtransfer);

    /*
    * Copy the color buffer from the resource to the user's buffer.
    */
    bpp = util_format_get_blocksize(surface->stvis.color_format);
    src = map;
    dst_stride = bpp * width;
    bytes = bpp * res->width0;

    for (y = 0; y < res->height0; y++) {
      memcpy(dst, src, bytes);
      armDCacheFlush(dst, bytes);
      dst += dst_stride;
      src += transfer->stride;
    }

    nvpipe->transfer_unmap(nvpipe, nvtransfer);
    pipe->transfer_unmap(pipe, transfer);
#endif
    return TRUE;
}

static Result
nvEventWait(u32 fd, u32 syncpt_id, u32 threshold, s32 timeout) {
    Result rc=0;
    u32 result;

    do {
        rc = nvioctlNvhostCtrl_EventWait(fd, syncpt_id, threshold, timeout, 0, &result);
    } while(rc==5);//timeout error

    return rc;
}

static boolean
switch_st_framebuffer_validate(struct st_context_iface *stctx, struct st_framebuffer_iface *stfbi,
                   const enum st_attachment_type *statts, unsigned count, struct pipe_resource **out)
{
    struct switch_egl_surface *surface = stfbi_to_surface(stfbi);
    struct pipe_screen *screen = stfbi->state_manager->screen;
    struct pipe_resource templat;
    u32 width = 1280, height = 720; // TODO: Get the resolution through viGetDisplayResolution().
    CALLED();

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

    for (unsigned i = 0; i < count; i++)
    {
        /*
        * At this time, we really only need to handle the front-left color
        * attachment, since that's all we specified for the visual in
        * osmesa_init_st_visual().
        */
        if (statts[i] == ST_ATTACHMENT_FRONT_LEFT || statts[i] == ST_ATTACHMENT_BACK_LEFT)
        {
            TRACE("Backbuffer\n");
            s32 index = surface->CurrentProducerBuffer;
            if (statts[i] == ST_ATTACHMENT_FRONT_LEFT)
                index = (index + 1) % NUM_SWAP_BUFFERS;
            pipe_resource_reference(&out[i], surface->buffers[index]);
        }
        else if (statts[i] == ST_ATTACHMENT_DEPTH_STENCIL)
        {
            TRACE("Depthstencil\n");
            templat.format = surface->stvis.depth_stencil_format;
            templat.bind = PIPE_BIND_DEPTH_STENCIL;
            pipe_resource_reference(&out[i], NULL);
            out[i] = screen->resource_create(screen, &templat);
        }
        else if (statts[i] == ST_ATTACHMENT_ACCUM)
        {
            TRACE("Accumulator\n");
            templat.format = surface->stvis.accum_format;
            templat.bind = PIPE_BIND_RENDER_TARGET;
            pipe_resource_reference(&out[i], NULL);
            out[i] = screen->resource_create(screen, &templat);
        }
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
    Result rc;
    struct switch_egl_surface* surface;
    struct switch_framebuffer *fb;
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

    fb = (struct switch_framebuffer *) calloc(1, sizeof (*fb));
    if (!fb)
    {
        _eglError(EGL_BAD_ALLOC, "switch_create_window_surface");
        goto cleanup;
    }

    fb->display = display;
    fb->surface = surface;
    surface->stfbi = &fb->base;
    surface->base.SwapInterval = 1;

    s32 window_id;
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
    }

    switch_fill_st_visual(&surface->stvis, conf);

    /* setup the st_framebuffer_iface */
    fb->base.visual = &surface->stvis;
    fb->base.flush_front = switch_st_framebuffer_flush_front;
    fb->base.validate = switch_st_framebuffer_validate;
    fb->base.flush_swapbuffers = switch_st_framebuffer_flush_swapbuffers;
    p_atomic_set(&fb->base.stamp, 0);
    fb->base.ID = p_atomic_inc_return(&drifb_ID);
    fb->base.state_manager = display->stmgr;

    u32 width = 1280, height = 720;
    for (int i = 0; i < NUM_SWAP_BUFFERS; i++)
    {
        struct winsys_handle whandle;
        memset(&whandle, 0, sizeof(whandle));
        whandle.type = DRM_API_HANDLE_TYPE_KMS;
        struct pipe_resource templat;
        templat.target = PIPE_TEXTURE_RECT;
        templat.format = surface->stvis.color_format;
        templat.last_level = 0;
        templat.width0 = (u16)width;
        templat.height0 = (u16)height;
        templat.depth0 = 1;
        templat.array_size = 1;
        templat.usage = PIPE_USAGE_DEFAULT;
        templat.bind = PIPE_BIND_RENDER_TARGET;
        templat.flags = 0;

        surface->buffers[i] = display->nvscreen->resource_create(display->nvscreen, &templat);
        display->nvscreen->resource_get_handle(display->nvscreen, display->nvctx, surface->buffers[i], &whandle, PIPE_HANDLE_USAGE_WRITE);
        if (!whandle.handle)
        {
            TRACE("Failed to get resource handle");
            return EGL_FALSE;
        }

        BufferInitData.refcount = i;
        BufferInitData.data.nvmap_handle0 = whandle.handle;
        BufferInitData.data.nvmap_handle1 = whandle.handle;
        BufferInitData.data.buffer_offset = whandle.offset;
        BufferInitData.data.timestamp = svcGetSystemTick();

        rc = bufferProducerGraphicBufferInit(i, &BufferInitData);
        if (R_FAILED(rc)) {
            TRACE("Failed to initialize buffer");
            return EGL_FALSE;
        }
    }

    rc = bufferProducerDequeueBuffer(false, width, height, 0, 0x300, &surface->CurrentProducerBuffer, &surface->DequeueFence);
    if (R_FAILED(rc))
        return false;

    rc = bufferProducerRequestBuffer(surface->CurrentProducerBuffer, NULL);
    if (R_FAILED(rc)) {
        TRACE("Failed to request buffer");
        return EGL_FALSE;
    }

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
    struct switch_egl_display *display = switch_egl_display(disp);
    struct switch_egl_surface* surface = switch_egl_surface(surf);

    //_gfxQueueBuffer(g_gfxCurrentProducerBuffer);
    QueueBufferData.timestamp = svcGetSystemTick();
    bufferProducerQueueBuffer(surface->CurrentProducerBuffer, &QueueBufferData, &surface->output);
    for (int i = 0; i < NUM_SWAP_BUFFERS; i++)
        bufferProducerDetachBuffer(i);
    bufferProducerDisconnect(NATIVE_WINDOW_API_CPU);
    bufferProducerExit();
    binderExitSession(&surface->session);

    for (int i = 0; i < NUM_SWAP_BUFFERS; i++)
         display->nvscreen->resource_destroy(display->nvscreen, surface->buffers[i]);

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

    TRACE("Initializing config\n");
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
    Result rc;
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

    stmgr = CALLOC_STRUCT(st_manager);
    if (!stmgr) {
        _eglError(EGL_BAD_ALLOC, "switch_initialize");
        return EGL_FALSE;
    }

    stmgr->get_param = switch_st_get_param;

    nvServiceType nv_servicetype;

    switch(__nx_applet_type) {
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

    TRACE("Initializing nv service\n");
    rc = nvInitialize(nv_servicetype, 0x300000);
    if (R_FAILED(rc)) {
        TRACE("Failed to initialize nv service\n");
        return EGL_FALSE;
    }

    TRACE("Opening /dev/nvhost-ctrl\n");
    rc = nvOpen(&display->nvhostctrl, "/dev/nvhost-ctrl");
    if (R_FAILED(rc)) {
        TRACE("Failed to open /dev/nvhost-ctrl\n");
        return EGL_FALSE;
    }

    TRACE("Creating nouveau screen\n");
    display->nvscreen = nouveau_switch_screen_create(display->nvhostctrl);
    if (!display->nvscreen)
    {
        TRACE("Failed to create nouveau screen\n");
        return EGL_FALSE;
    }

    TRACE("Creating nouveau context\n");
    display->nvctx = display->nvscreen->context_create(display->nvscreen, display, 0);
    if (!display->nvctx)
    {
        TRACE("Failed to create nouveau context\n");
        return EGL_FALSE;
    }

    if (dpy->Options.ForceSoftware)
    {
        struct sw_winsys *winsys;
        struct pipe_screen *screen;

        /* We use a null software winsys since we always just render to ordinary
        * driver resources.
        */
        TRACE("Initializing winsys\n");
        winsys = null_sw_create();
        if (!winsys)
            return EGL_FALSE;

        /* Create llvmpipe or softpipe screen */
        TRACE("Creating screen\n");
        screen = sw_screen_create(winsys);
        if (!screen)
        {
            _eglError(EGL_BAD_ALLOC, "sw_screen_create");
            winsys->destroy(winsys);
            return EGL_FALSE;
        }

        /* Inject optional trace, debug, etc. wrappers */
        TRACE("Wrapping screen\n");
        stmgr->screen = debug_screen_wrap(screen);
    }
    else
    {
        /* Inject optional trace, debug, etc. wrappers */
        TRACE("Wrapping screen\n");
        stmgr->screen = debug_screen_wrap(display->nvscreen);
    }

    viGetDisplayVsyncEvent((ViDisplay*)dpy->PlatformDisplay, &display->VsyncEvent);
    if (R_FAILED(rc)) {
        TRACE("Failed to get vsync event\n");
        return EGL_FALSE;
    }

    {
        u32 width = 1280, height = 720;
        u32 aligned_width = (width+15) & ~15;//Align to 16.
        u32 aligned_height = (height+127) & ~127;//Align to 128.
        u32 size = aligned_width*aligned_height*4;

        BufferInitData.width = width;
        BufferInitData.height = height;
        BufferInitData.stride = aligned_width;

        BufferInitData.data.width_unk0 = width;
        BufferInitData.data.width_unk1 = width;
        BufferInitData.data.height_unk = height;

        BufferInitData.data.byte_stride = aligned_width*4;

        BufferInitData.data.buffer_size0 = size;
        BufferInitData.data.buffer_size1 = size;
    }

    display->stmgr = stmgr;
    display->stapi = st_gl_api_create();
    return EGL_TRUE;
}


static EGLBoolean
switch_terminate(_EGLDriver* drv, _EGLDisplay* dpy)
{
    CALLED();

    struct switch_egl_display *display = switch_egl_display(dpy);
    display->nvctx->destroy(display->nvctx);
    display->nvscreen->destroy(display->nvscreen);
    nvClose(display->nvhostctrl);
    nvExit();
    svcCloseHandle(display->VsyncEvent);
    free(display);

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
    context->stctx = display->stapi->create_context(display->stapi, display->stmgr, &attribs, &error, NULL);
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
    struct switch_egl_surface* surf = switch_egl_surface(dsurf);
    CALLED();

    _EGLContext *old_ctx;
    _EGLSurface *old_dsurf, *old_rsurf;

    if (!_eglBindContext(ctx, dsurf, rsurf, &old_ctx, &old_dsurf, &old_rsurf))
        return EGL_FALSE;

    return disp->stapi->make_current(disp->stapi, cont->stctx, surf->stfbi, surf->stfbi);
}

static EGLBoolean
_waitevent(Handle *handle)
{
    Result rc=0, rc2=0;

    svcResetSignal(*handle);

    do {
        rc = svcWaitSynchronizationSingle(*handle, U64_MAX);

        if (R_SUCCEEDED(rc))
            rc2 = svcResetSignal(*handle);

    } while(R_FAILED(rc) || (rc2 & 0x3FFFFF)==0xFA01);

    return R_FAILED(rc2) ? EGL_FALSE : EGL_TRUE;
}

static EGLBoolean
switch_swap_buffers(_EGLDriver *drv, _EGLDisplay *dpy, _EGLSurface *surf)
{
    struct switch_egl_display* display = switch_egl_display(dpy);
    struct switch_egl_surface* surface = switch_egl_surface(surf);
    bufferProducerFence *fence = &surface->DequeueFence;
    Result rc;
    u32 width = 1280, height = 720; // TODO: Get the resolution through viGetDisplayResolution().
    CALLED();

    TRACE("Swapping out buffer %d\n", surface->CurrentProducerBuffer);
    QueueBufferData.timestamp = svcGetSystemTick();
    rc = bufferProducerQueueBuffer(surface->CurrentProducerBuffer, &QueueBufferData, &surface->output);
    if (R_FAILED(rc)) {
        return EGL_FALSE;
    }

    rc = bufferProducerDequeueBuffer(false, width, height, 0, 0x300, &surface->CurrentProducerBuffer, fence);
    if (R_FAILED(rc)) {
        TRACE("Failed to dequeue buffer\n");
        return EGL_FALSE;
    }

    TRACE("Swapping in buffer %d\n", surface->CurrentProducerBuffer);
    // Only run nvEventWait when the fence is valid and the id is not NO_FENCE.
    if (fence->is_valid && fence->nv_fences[0].id!=0xffffffff)
      rc = nvEventWait(display->nvhostctrl, fence->nv_fences[0].id, fence->nv_fences[0].value, -1);
    if (R_FAILED(rc)) {
        TRACE("Failed to wait for fence\n");
        //return EGL_FALSE;
    }

    p_atomic_inc(&surface->stfbi->stamp);

    TRACE("Requesting buffer %d\n", surface->CurrentProducerBuffer);
    //if (R_SUCCEEDED(rc)) g_gfxCurrentBuffer = (g_gfxCurrentBuffer + 1) & (g_nvgfx_totalframebufs-1);
    rc = bufferProducerRequestBuffer(surface->CurrentProducerBuffer, NULL);
    if (R_FAILED(rc)) {
        TRACE("Failed to request buffer\n");
        return EGL_FALSE;
    }

    TRACE("Wait for V-Sync event\n");
    return _waitevent(&display->VsyncEvent);
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
