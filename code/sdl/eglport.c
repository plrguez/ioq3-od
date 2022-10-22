/**
 *
 *  EGLPORT.C
 *  Copyright (C) 2011-2013 Scott R. Smith
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 *
 */

#include "eglport.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef OPENDINGUX
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#if !defined(GCW0)
#define USE_EGL_SDL	1
#endif
#define USE_GLES1	1

#if defined(USE_EGL_SDL)
#include "SDL.h"
#include "SDL_syswm.h"
SDL_SysWMinfo sysWmInfo;      /** Holds our X Display/Window information */
#endif /* USE_EGL_SDL */

#if defined(PANDORA) /* Pandora VSync Support */
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, __u32)
#endif
int fbdev = -1;

#elif defined(RPI)
#include "bcm_host.h"
#endif /* PANDORA */

enum EGL_RENDER_T {
    RENDER_RAW=0,           /** Sets render mode to raw or framebuffer mode. */
    RENDER_SDL,             /** Sets render mode to X11/SDL mode. */
    RENDER_TOTAL
};

enum EGL_SETTINGS_T {
    CFG_MODE=0,             /** Render mode for EGL 0=RAW 1=SDL. */
    CFG_VSYNC,              /** Controls system vsync if available. */
    CFG_FSAA,               /** Number of samples for full screen AA. 0 is off, 2/4 samples. */
    CFG_FPS,                /** Calculate and report frame per second. */
    CFG_RED_SIZE,           /** Number of bits of Red in the color buffer. */
    CFG_GREEN_SIZE,         /** Number of bits of Green in the color buffer. */
    CFG_BLUE_SIZE,          /** Number of bits of Blue in the color buffer. */
    CFG_ALPHA_SIZE,         /** Number of bits of Alpha in the color buffer. */
    CFG_DEPTH_SIZE,         /** Number of bits of Z in the depth buffer. */
    CFG_BUFFER_SIZE,        /** The total color component bits in the color buffer. */
    CFG_STENCIL_SIZE,       /** Number of bits of Stencil in the stencil buffer. */
    CFG_TOTAL               /** Total number of settings. */
};

NativeDisplayType   nativeDisplay = 0;      /** Reference to the systems native display */
NativeWindowType    nativeWindow  = 0;      /** Reference to the systems native window */
EGLint              eglSettings[CFG_TOTAL]; /** Stores setting values. */
EGLDisplay          eglDisplay    = NULL;   /** Reference to the EGL display */
EGLConfig           eglConfig     = NULL;   /** Reference to the EGL config */
EGLContext          eglContext    = NULL;   /** Reference to the EGL context */
EGLSurface          eglSurface    = NULL;   /** Reference to the EGL surface */

#ifdef OPENDINGUX
#define     totalConfigsIn 10                /** Total number of configurations to request */
#else
#define     totalConfigsIn 5                /** Total number of configurations to request */
#endif
EGLint      totalConfigsFound = 0;          /** Total number of configurations matching attributes */
EGLConfig   eglConfigs[totalConfigsIn];     /** Structure containing references to matching configurations */

uint32_t    fpsCount    = 0;                /** Total number of frames counted */
uint32_t    fpsTime     = 0;                /** Start time of frame count measurment */

int8_t	eglColorbits 	= 0;
int8_t	eglDepthbits	= 0;
int8_t	eglStencilbits	= 0;


/** Private API */
void        OpenCfg                 ( const char* file );
int8_t      ConfigureEGL            ( EGLConfig config );
int8_t      FindEGLConfigs          ( void );
int8_t      CheckEGLErrors          ( const char* file, uint16_t line );

int8_t      GetNativeDisplay        ( void );
int8_t      GetNativeWindow         ( uint16_t width, uint16_t height );
void        FreeNativeDisplay       ( void );
void        FreeNativeWindow        ( void );

void        Platform_Open           ( void );
void        Platform_Close          ( void );
void        Platform_VSync          ( void );
uint32_t    Platform_GetTicks       ( void );

#ifdef OPENDINGUX
#define LOG printf

struct drm {
    int fd;

    drmModeModeInfo *mode;
    uint32_t crtc_id;
    uint32_t connector_id;
} drm;

struct drm_fb {
    struct gbm_bo *bo;
    uint32_t fb_id;
};

struct gbm {
    struct gbm_device *dev;
    struct gbm_surface *surface;
    uint32_t format;
    int width, height;
} gbm;

struct gbm_bo *bo;

static void page_flip_handler(int fd, unsigned int frame,
                  unsigned int sec, unsigned int usec, void *data)
{
    /* suppress 'unused parameter' warnings */
    (void)fd, (void)frame, (void)sec, (void)usec;

    int *waiting_for_flip = data;
    *waiting_for_flip = 0;
}

drmEventContext evctx = {
    .version = 2,
    .page_flip_handler = page_flip_handler,
};

static int get_resources(int fd, drmModeRes **resources)
{
    *resources = drmModeGetResources(fd);
    if (*resources == NULL)
        return -1;
    return 0;
}

static uint32_t find_crtc_for_encoder(const drmModeRes *resources,
                                    const drmModeEncoder *encoder) {
    int i;

    for (i = 0; i < resources->count_crtcs; i++) {
        /* possible_crtcs is a bitmask as described here:
         * https://dvdhrm.wordpress.com/2012/09/13/linux-drm-mode-setting-api
         */
        const uint32_t crtc_mask = 1 << i;
        const uint32_t crtc_id = resources->crtcs[i];
        if (encoder->possible_crtcs & crtc_mask) {
            return crtc_id;
        }
    }

    /* no match found */
    return -1;
}

static uint32_t find_crtc_for_connector(const struct drm *drm, const drmModeRes *resources,
                                        const drmModeConnector *connector) {
    int i;

    for (i = 0; i < connector->count_encoders; i++) {
        const uint32_t encoder_id = connector->encoders[i];
        drmModeEncoder *encoder = drmModeGetEncoder(drm->fd, encoder_id);

        if (encoder) {
            const uint32_t crtc_id = find_crtc_for_encoder(resources, encoder);

            drmModeFreeEncoder(encoder);
            if (crtc_id != 0) {
                return crtc_id;
            }
        }
    }

    /* no match found */
    return -1;
}

#define MAX_DRM_DEVICES 64

static int find_drm_device(drmModeRes **resources)
{
    drmDevicePtr devices[MAX_DRM_DEVICES] = { NULL };
    int num_devices, fd = -1;

    num_devices = drmGetDevices2(0, devices, MAX_DRM_DEVICES);
    if (num_devices < 0) {
        printf("drmGetDevices2 failed: %s\n", strerror(-num_devices));
        return -1;
    }

    for (int i = 0; i < num_devices; i++) {
        drmDevicePtr device = devices[i];
        int ret;

        if (!(device->available_nodes & (1 << DRM_NODE_PRIMARY)))
            continue;
        /* OK, it's a primary device. If we can get the
         * drmModeResources, it means it's also a
         * KMS-capable device.
         */
        fd = open(device->nodes[DRM_NODE_PRIMARY], O_RDWR);
        if (fd < 0)
            continue;
        ret = get_resources(fd, resources);
        if (!ret)
            break;
        close(fd);
        fd = -1;
    }
    drmFreeDevices(devices, num_devices);

    if (fd < 0)
        printf("no drm device found!\n");
    return fd;
}

static void
drm_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
    int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
    struct drm_fb *fb = data;

    if (fb->fb_id)
        drmModeRmFB(drm_fd, fb->fb_id);

    free(fb);
}

struct drm_fb * drm_fb_get_from_bo(struct gbm_bo *bo)
{
    int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
    struct drm_fb *fb = gbm_bo_get_user_data(bo);
    uint32_t width, height, format,
         strides[4] = {0}, handles[4] = {0},
         offsets[4] = {0}, flags = 0;
    int ret = -1;

    if (fb)
        return fb;

    fb = calloc(1, sizeof *fb);
    fb->bo = bo;

    width = gbm_bo_get_width(bo);
    height = gbm_bo_get_height(bo);
    format = gbm_bo_get_format(bo);

    if (gbm_bo_get_modifier && gbm_bo_get_plane_count &&
        gbm_bo_get_stride_for_plane && gbm_bo_get_offset) {

        uint64_t modifiers[4] = {0};
        modifiers[0] = gbm_bo_get_modifier(bo);
        const int num_planes = gbm_bo_get_plane_count(bo);
        for (int i = 0; i < num_planes; i++) {
            strides[i] = gbm_bo_get_stride_for_plane(bo, i);
            handles[i] = gbm_bo_get_handle(bo).u32;
            offsets[i] = gbm_bo_get_offset(bo, i);
            modifiers[i] = modifiers[0];
        }

        if (modifiers[0]) {
            flags = DRM_MODE_FB_MODIFIERS;
            printf("Using modifier %i \n", modifiers[0]);
        }

        ret = drmModeAddFB2WithModifiers(drm_fd, width, height,
                                format, handles, strides, offsets,
                                modifiers, &fb->fb_id, flags);
    }

    if (ret) {
        if (flags)
            fprintf(stderr, "Modifiers failed!\n");

        uint32_t tmp_handles[4] = {gbm_bo_get_handle(bo).u32,0,0,0};
        uint32_t tmp_strides[4] = {gbm_bo_get_stride(bo),0,0,0};
        memcpy(handles, tmp_handles, 16);
        memcpy(strides, tmp_strides, 16);
        memset(offsets, 0, 16);
        ret = drmModeAddFB2(drm_fd, width, height, format,
                            handles, strides, offsets, &fb->fb_id, 0);
    }

    if (ret) {
        printf("failed to create fb: %s\n", strerror(errno));
        free(fb);
        return NULL;
    }

    gbm_bo_set_user_data(bo, fb, drm_fb_destroy_callback);

    return fb;
}

int init_drm(struct drm *drm, unsigned int vrefresh)
{
    drmModeRes *resources;
    drmModeConnector *connector = NULL;
    drmModeEncoder *encoder = NULL;
    int i, area;

    drm->fd = find_drm_device(&resources);
    if (drm->fd < 0) {
        printf("could not open drm device\n");
        return -1;
    }

    if (!resources) {
        printf("drmModeGetResources failed: %s\n", strerror(errno));
        return -1;
    }

    /* find a connected connector: */
    for (i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(drm->fd, resources->connectors[i]);
        if (connector->connection == DRM_MODE_CONNECTED) {
            /* it's connected, let's use this! */
            break;
        }
        drmModeFreeConnector(connector);
        connector = NULL;
    }

    if (!connector) {
        /* we could be fancy and listen for hotplug events and wait for
         * a connector..
         */
        printf("no connected connector!\n");
        return -1;
    }

    /* find preferred mode or the highest resolution mode: */
    if (!drm->mode) {
        for (i = 0, area = 0; i < connector->count_modes; i++) {
            drmModeModeInfo *current_mode = &connector->modes[i];

            if (current_mode->type & DRM_MODE_TYPE_PREFERRED) {
                drm->mode = current_mode;
                break;
            }

            int current_area = current_mode->hdisplay * current_mode->vdisplay;
            if (current_area > area) {
                drm->mode = current_mode;
                area = current_area;
            }
        }
    }

    if (!drm->mode) {
        printf("could not find mode!\n");
        return -1;
    }

    /* find encoder: */
    for (i = 0; i < resources->count_encoders; i++) {
        encoder = drmModeGetEncoder(drm->fd, resources->encoders[i]);
        if (encoder->encoder_id == connector->encoder_id)
            break;
        drmModeFreeEncoder(encoder);
        encoder = NULL;
    }

    if (encoder) {
        drm->crtc_id = encoder->crtc_id;
    } else {
        uint32_t crtc_id = find_crtc_for_connector(drm, resources, connector);
        if (crtc_id == 0) {
            printf("no crtc found!\n");
            return -1;
        }

        drm->crtc_id = crtc_id;
    }

    drmModeFreeResources(resources);

    drm->connector_id = connector->connector_id;

    return 0;
}

static int drm_run(const struct gbm *gbm)
{
    int waiting_for_flip = 1;
    struct gbm_bo *next_bo;
    struct drm_fb *drm_fb;

    next_bo = gbm_surface_lock_front_buffer(gbm->surface);
    drm_fb = drm_fb_get_from_bo(next_bo);
    if (!drm_fb) {
        LOG("Failed to get a new framebuffer BO\n");
        return -1;
    }

    int ret = drmModePageFlip(drm.fd, drm.crtc_id, drm_fb->fb_id,
                                DRM_MODE_PAGE_FLIP_EVENT, &waiting_for_flip);
    if (ret) {
        LOG("failed to queue page flip: %s\n", strerror(errno));
        return -1;
    }
    while (waiting_for_flip) {
        drmHandleEvent(drm.fd, &evctx);
    }

    // release last buffer to render on again
    gbm_surface_release_buffer(gbm->surface, bo);
    bo = next_bo;

    return 0;
}

#define WEAK __attribute__((weak))

WEAK struct gbm_surface *
gbm_surface_create_with_modifiers(struct gbm_device *gbm,
                                  uint32_t width, uint32_t height,
                                  uint32_t format,
                                  const uint64_t *modifiers,
                                  const unsigned int count);

const struct gbm * init_gbm(int drm_fd, int w, int h, uint32_t format, uint64_t modifier)
{
    gbm.dev = gbm_create_device(drm_fd);
    gbm.format = format;
    gbm.surface = NULL;

    if (gbm_surface_create_with_modifiers) {
        gbm.surface = gbm_surface_create_with_modifiers(gbm.dev, w, h,
                                                        gbm.format,
                                                        &modifier, 1);

    }

    if (!gbm.surface) {
        if (modifier != DRM_FORMAT_MOD_LINEAR) {
            fprintf(stderr, "Modifiers requested but support isn't available\n");
            return NULL;
        }
        gbm.surface = gbm_surface_create(gbm.dev, w, h,
                                            gbm.format,
                                            GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

    }

    if (!gbm.surface) {
        printf("failed to create gbm surface\n");
        return NULL;
    }

    gbm.width = w;
    gbm.height = h;

    return &gbm;
}
#endif //OPENDINGUX

/** @brief Release all EGL and system resources
 */
void EGL_Close( void )
{
    /* Release EGL resources */
    if (eglDisplay != NULL)
    {
        peglMakeCurrent( eglDisplay, NULL, NULL, EGL_NO_CONTEXT );
        if (eglContext != NULL) {
            peglDestroyContext( eglDisplay, eglContext );
        }
        if (eglSurface != NULL) {
            peglDestroySurface( eglDisplay, eglSurface );
        }
        peglTerminate( eglDisplay );
    }

    eglSurface = NULL;
    eglContext = NULL;
    eglDisplay = NULL;
	
	eglColorbits = 0;
	eglDepthbits = 0;
	eglStencilbits = 0;

    /* Release platform resources */
    FreeNativeWindow();
    FreeNativeDisplay();
    Platform_Close();
#ifdef OPENDINGUX
    drmDropMaster(drm.fd);
#endif
    
    CheckEGLErrors( __FILE__, __LINE__ );

    printf( "EGLport: Closed\n" );
}

/** @brief Swap the surface buffer onto the display
 */
void EGL_SwapBuffers( void )
{
    if (eglSettings[CFG_VSYNC] != 0) {
        Platform_VSync();
    }

    peglSwapBuffers( eglDisplay, eglSurface );
#ifdef OPENDINGUX
    drm_run(&gbm);
#endif

    if (eglSettings[CFG_FPS] != 0) {
        fpsCount++;

        if (fpsTime - Platform_GetTicks() >= 1000)
        {
            printf( "EGLport: %d fps\n", fpsCount );
            fpsTime = Platform_GetTicks();
            fpsCount = 0;
        }
    }
}

#ifdef OPENDINGUX
int8_t EGL_Init_DRM( uint16_t *width, uint16_t *height )
{
    unsigned int vrefresh = 0;
    
    struct gbm *pgbm = &gbm;
    
    if (drm.fd) {
        LOG("DRM already initialized\n");
        return 1;
    }
    
    init_drm(&drm, vrefresh);
    if (!drm.fd) {
        LOG("failed to initialize DRM\n");
        return 0;
    }
    
    pgbm = init_gbm(drm.fd, drm.mode->hdisplay, drm.mode->vdisplay,
                        DRM_FORMAT_RGB565, DRM_FORMAT_MOD_LINEAR);
    if (!pgbm) {
        LOG("failed to initialize GBM\n");
        return 0;
    }

    *width = drm.mode->hdisplay;
    *height = drm.mode->vdisplay;
    return 1;
}
#endif

/** @brief Obtain the system display and initialize EGL
 * @param width : desired pixel width of the window (not used by all platforms)
 * @param height : desired pixel height of the window (not used by all platforms)
 * @return : 0 if the function passed, else 1
 */
int8_t EGL_Open( uint16_t width, uint16_t height )
{
    EGLint eglMajorVer, eglMinorVer;
    EGLBoolean result;
    uint32_t configIndex = 0;
    const char* output;

    static const EGLint contextAttribs[] =
    {
#if defined(USE_GLES2)
          EGL_CONTEXT_CLIENT_VERSION,     2,
#endif
          EGL_NONE
    };

#if defined(DEBUG)
    printf( "EGLport Warning: DEBUG is enabled which may effect performance\n" );
#endif

    /* Check that system is not open */
    if (eglDisplay != NULL || eglContext != NULL || eglSurface != NULL)
    {
        printf( "EGLport ERROR: EGL system is already open!\n" );
        return 1;
    }

    /* Check for the cfg file to alternative settings */
    OpenCfg( "eglport.cfg" );

    /* Setup any platform specific bits */
    Platform_Open();

    printf( "EGLport: Opening EGL display\n" );
#ifndef OPENDINGUX
    if (GetNativeDisplay() != 0)
    {
        printf( "EGLport ERROR: Unable to obtain native display!\n" );
        return 1;
    }
#endif
    
#ifdef OPENDINGUX
    struct gbm *pgbm = &gbm;
    struct drm *pdrm = &drm;
    
    struct drm_fb *drm_fb;
    
    EGLConfig config;
    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT;

    eglGetPlatformDisplayEXT = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (eglGetPlatformDisplayEXT) {
        eglDisplay = eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR, pgbm->dev, NULL);
    }
#else
    eglDisplay = peglGetDisplay( nativeDisplay );
#endif //OPENDINGUX
    if (eglDisplay == EGL_NO_DISPLAY)
    {
        CheckEGLErrors( __FILE__, __LINE__ );
        printf( "EGLport ERROR: Unable to create EGL display.\n" );
        return 1;
    }

    printf( "EGLport: Initializing\n" );
    result = peglInitialize( eglDisplay, &eglMajorVer, &eglMinorVer );
    if (result != EGL_TRUE )
    {
        CheckEGLErrors( __FILE__, __LINE__ );
        printf( "EGLport ERROR: Unable to initialize EGL display.\n" );
        return 1;
    }

    /* Get EGL Library Information */
    printf( "EGL Implementation Version: Major %d Minor %d\n", eglMajorVer, eglMinorVer );
    output = peglQueryString( eglDisplay, EGL_VENDOR );
    printf( "EGL_VENDOR: %s\n", output );
    output = peglQueryString( eglDisplay, EGL_VERSION );
    printf( "EGL_VERSION: %s\n", output );
    output = peglQueryString( eglDisplay, EGL_EXTENSIONS );
    printf( "EGL_EXTENSIONS: %s\n", output );

    if (FindEGLConfigs() != 0)
    {
        printf( "EGLport ERROR: Unable to configure EGL. See previous error.\n" );
        return 1;
    }

    printf( "EGLport: Using Config %d\n", configIndex );
#if defined(EGL_VERSION_1_2)
    /* Bind GLES and create the context */
    printf( "EGLport: Binding API\n" );
    result = peglBindAPI( EGL_OPENGL_ES_API );
    if ( result == EGL_FALSE )
    {
        CheckEGLErrors( __FILE__, __LINE__ );
        printf( "EGLport ERROR: Could not bind EGL API.\n" );
        return 1;
    }
#endif /* EGL_VERSION_1_2 */

    printf( "EGLport: Creating Context\n" );
    eglContext = peglCreateContext( eglDisplay, eglConfigs[configIndex], NULL, contextAttribs );
    if (eglContext == EGL_NO_CONTEXT)
    {
        CheckEGLErrors( __FILE__, __LINE__ );
        printf( "EGLport ERROR: Unable to create GLES context!\n");
        return 1;
    }

    printf( "EGLport: Creating window surface\n" );
#ifdef OPENDINGUX
    eglSurface = peglCreateWindowSurface( eglDisplay, eglConfigs[configIndex], (EGLNativeWindowType)pgbm->surface, 0 );
#else
    if (GetNativeWindow( width, height ) != 0)
    {
        printf( "EGLport ERROR: Unable to obtain native window!\n" );
        return 1;
    }

    eglSurface = peglCreateWindowSurface( eglDisplay, eglConfigs[configIndex], nativeWindow, 0 );
#endif
    if (eglSurface == EGL_NO_SURFACE)
    {
        CheckEGLErrors( __FILE__, __LINE__ );
        printf( "EGLport ERROR: Unable to create EGL surface!\n" );
        return 1;
    }

    printf( "EGLport: Making Current\n" );
    result = peglMakeCurrent( eglDisplay,  eglSurface,  eglSurface, eglContext );
    if (result != EGL_TRUE)
    {
        CheckEGLErrors( __FILE__, __LINE__ );
        printf( "EGLport ERROR: Unable to make GLES context current\n" );
        return 1;
    }

	{
	  EGLint color, depth, stencil;
	  eglGetConfigAttrib(eglDisplay, eglConfigs[configIndex], EGL_BUFFER_SIZE, &color);
	  eglGetConfigAttrib(eglDisplay, eglConfigs[configIndex], EGL_DEPTH_SIZE, &depth);
	  eglGetConfigAttrib(eglDisplay, eglConfigs[configIndex], EGL_STENCIL_SIZE, &stencil);
	  eglColorbits = (color==16)?5:8; //quick hack
	  eglDepthbits = depth;
	  eglStencilbits = stencil;
	}

#ifdef OPENDINGUX
    peglSwapBuffers(eglDisplay, eglSurface);
    bo = gbm_surface_lock_front_buffer(pgbm->surface);
    drm_fb = drm_fb_get_from_bo(bo);
    if (!drm_fb) {
        LOG("Failed to get a new framebuffer BO\n");
        return 1;
    }

    // set mode
    drmSetMaster(pdrm->fd);
    int ret = drmModeSetCrtc(pdrm->fd, pdrm->crtc_id, drm_fb->fb_id, 0, 0,
                              &pdrm->connector_id, 1, pdrm->mode);
    if (ret) {
        LOG("failed to set mode: %s\n", strerror(errno));
        return 1;
    }
#endif

    printf( "EGLport: Setting swap interval\n" );
    peglSwapInterval( eglDisplay, (eglSettings[CFG_VSYNC] > 0) ? 1 : 0 );
    
    printf( "EGLport: Complete\n" );

    CheckEGLErrors( __FILE__, __LINE__ );
	
    return 0;
}

/** @brief Read settings that configure how to use EGL
 * @param file : name of the config file
 */
void OpenCfg ( const char* file )
{
    #define MAX_STRING 20
    #define MAX_SIZE 100
    uint8_t i;
    FILE* fp = NULL;
    char* location = NULL;
    char eglStrings[CFG_TOTAL][MAX_STRING];
    char buffer[MAX_SIZE];

    strncpy( eglStrings[CFG_MODE], "egl_mode=", MAX_STRING );
    strncpy( eglStrings[CFG_VSYNC], "use_vsync=", MAX_STRING );
    strncpy( eglStrings[CFG_FSAA], "use_fsaa=", MAX_STRING );
    strncpy( eglStrings[CFG_RED_SIZE], "size_red=", MAX_STRING );
    strncpy( eglStrings[CFG_GREEN_SIZE], "size_green=", MAX_STRING );
    strncpy( eglStrings[CFG_BLUE_SIZE], "size_blue=", MAX_STRING );
    strncpy( eglStrings[CFG_ALPHA_SIZE], "size_alpha=", MAX_STRING );
    strncpy( eglStrings[CFG_DEPTH_SIZE], "size_depth=", MAX_STRING );
    strncpy( eglStrings[CFG_BUFFER_SIZE], "size_buffer=", MAX_STRING );
    strncpy( eglStrings[CFG_STENCIL_SIZE], "size_stencil=", MAX_STRING );

    /* Set defaults */
//#if defined(USE_EGL_SDL)
//    eglSettings[CFG_MODE]           = RENDER_SDL;
//#else
    eglSettings[CFG_MODE]           = RENDER_RAW;
//#endif
    eglSettings[CFG_VSYNC]          = 0;
    eglSettings[CFG_FSAA]           = 0;
    eglSettings[CFG_FPS]            = 0;
    eglSettings[CFG_RED_SIZE]       = 5;
    eglSettings[CFG_GREEN_SIZE]     = 6;
    eglSettings[CFG_BLUE_SIZE]      = 5;
    eglSettings[CFG_ALPHA_SIZE]     = 0;
    eglSettings[CFG_DEPTH_SIZE]     = 16;
    eglSettings[CFG_BUFFER_SIZE]    = 16;
    eglSettings[CFG_STENCIL_SIZE]   = 0;

    /* Parse INI file */
    fp = fopen( file, "r");
    if (fp != NULL)
    {
        while (fgets( buffer, MAX_SIZE, fp ) != NULL)
        {
            for (i=0; i<CFG_TOTAL; i++)
            {
                location = strstr( buffer, eglStrings[i] );
                if (location != NULL)
                {
                    eglSettings[i] = atol( location+strlen( eglStrings[i] ) );
                    printf( "EGLport: %s set to %d.\n", eglStrings[i], eglSettings[i] );
                    break;
                }
            }
        }

        fclose( fp );
    }
    else
    {
        printf( "EGL NOTICE: Unable to read ini settings from file '%s'. Using defaults\n", file );
    }
}

/** @brief Find a EGL configuration tht matches the defined attributes
 * @return : 0 if the function passed, else 1
 */
int8_t FindEGLConfigs( void )
{
    EGLBoolean result;
    int attrib = 0;
    EGLint ConfigAttribs[23];

    ConfigAttribs[attrib++] = EGL_RED_SIZE;                         /* 1 */
    ConfigAttribs[attrib++] = eglSettings[CFG_RED_SIZE];            /* 2 */
    ConfigAttribs[attrib++] = EGL_GREEN_SIZE;                       /* 3 */
    ConfigAttribs[attrib++] = eglSettings[CFG_GREEN_SIZE];          /* 4 */
    ConfigAttribs[attrib++] = EGL_BLUE_SIZE;                        /* 5 */
    ConfigAttribs[attrib++] = eglSettings[CFG_BLUE_SIZE];           /* 6 */
    ConfigAttribs[attrib++] = EGL_ALPHA_SIZE;                       /* 7 */
    ConfigAttribs[attrib++] = eglSettings[CFG_ALPHA_SIZE];          /* 8 */
    ConfigAttribs[attrib++] = EGL_DEPTH_SIZE;                       /* 9 */
    ConfigAttribs[attrib++] = eglSettings[CFG_DEPTH_SIZE];          /* 10 */
    ConfigAttribs[attrib++] = EGL_BUFFER_SIZE;                      /* 11 */
    ConfigAttribs[attrib++] = eglSettings[CFG_BUFFER_SIZE];         /* 12 */
    ConfigAttribs[attrib++] = EGL_STENCIL_SIZE;                     /* 13 */
    ConfigAttribs[attrib++] = eglSettings[CFG_STENCIL_SIZE];        /* 14 */
    ConfigAttribs[attrib++] = EGL_SURFACE_TYPE;                     /* 15 */
    ConfigAttribs[attrib++] = EGL_WINDOW_BIT;                       /* 16 */
#if defined(EGL_VERSION_1_2)
    ConfigAttribs[attrib++] = EGL_RENDERABLE_TYPE;                  /* 17 */
#if defined(USE_GLES1)
    ConfigAttribs[attrib++] = EGL_OPENGL_ES_BIT;
#elif defined(USE_GLES2)
    ConfigAttribs[attrib++] = EGL_OPENGL_ES2_BIT;                   /* 18 */
#endif /* USE_GLES1 */
#endif /* EGL_VERSION_1_2 */
    ConfigAttribs[attrib++] = EGL_SAMPLE_BUFFERS;                   /* 19 */
    ConfigAttribs[attrib++] = (eglSettings[CFG_FSAA] > 0) ? 1 : 0;  /* 20 */
    ConfigAttribs[attrib++] = EGL_SAMPLES;                          /* 21 */
    ConfigAttribs[attrib++] = eglSettings[CFG_FSAA];                /* 22 */
    ConfigAttribs[attrib++] = EGL_NONE;                             /* 23 */

    result = peglChooseConfig( eglDisplay, ConfigAttribs, eglConfigs, totalConfigsIn, &totalConfigsFound );
    if (result != EGL_TRUE || totalConfigsFound == 0)
    {
        CheckEGLErrors( __FILE__, __LINE__ );
        printf( "EGLport ERROR: Unable to query for available configs, found %d.\n", totalConfigsFound );
        return 1;
    }
#if defined(OPENDINGUX)
    int config_index = -1;
    for (int i = 0; i < totalConfigsFound; ++i) {
        EGLint id;

        if (!eglGetConfigAttrib(eglDisplay, eglConfigs[i], EGL_NATIVE_VISUAL_ID, &id))
            continue;

        if (id == gbm.format) {
            config_index = i;
            break;
        }
    }
    if (config_index != -1)
      eglConfigs[0] = eglConfigs[config_index];
#endif
    printf( "EGLport: Found %d available configs\n", totalConfigsFound );

    return 0;
}

/** @brief Error checking function
 * @param file : string reference that contains the source file that the check is occuring in
 * @param line : numeric reference that contains the line number that the check is occuring in
 * @return : 0 if the function passed, else 1
 */
int8_t CheckEGLErrors( const char* file, uint16_t line )
{
    EGLenum error;
    const char* errortext;
    const char* description;

    error = eglGetError();

    if (error != EGL_SUCCESS && error != 0)
    {
        switch (error)
        {
            case EGL_NOT_INITIALIZED:
                errortext   = "EGL_NOT_INITIALIZED.";
                description = "EGL is not or could not be initialized, for the specified display.";
                break;
            case EGL_BAD_ACCESS:
                errortext   = "EGL_BAD_ACCESS EGL";
                description = "cannot access a requested resource (for example, a context is bound in another thread).";
                break;
            case EGL_BAD_ALLOC:
                errortext   = "EGL_BAD_ALLOC EGL";
                description = "failed to allocate resources for the requested operation.";
                break;
            case EGL_BAD_ATTRIBUTE:
                errortext   = "EGL_BAD_ATTRIBUTE";
                description = "An unrecognized attribute or attribute value was passed in anattribute list.";
                break;
            case EGL_BAD_CONFIG:
                errortext   = "EGL_BAD_CONFIG";
                description = "An EGLConfig argument does not name a valid EGLConfig.";
                break;
            case EGL_BAD_CONTEXT:
                errortext   = "EGL_BAD_CONTEXT";
                description = "An EGLContext argument does not name a valid EGLContext.";
                break;
            case EGL_BAD_CURRENT_SURFACE:
                errortext   = "EGL_BAD_CURRENT_SURFACE";
                description = "The current surface of the calling thread is a window, pbuffer,or pixmap that is no longer valid.";
                break;
            case EGL_BAD_DISPLAY:
                errortext   = "EGL_BAD_DISPLAY";
                description = "An EGLDisplay argument does not name a valid EGLDisplay.";
                break;
            case EGL_BAD_MATCH:
                errortext   = "EGL_BAD_MATCH";
                description = "Arguments are inconsistent; for example, an otherwise valid context requires buffers (e.g. depth or stencil) not allocated by an otherwise valid surface.";
                break;
            case EGL_BAD_NATIVE_PIXMAP:
                errortext   = "EGL_BAD_NATIVE_PIXMAP";
                description = "An EGLNativePixmapType argument does not refer to a validnative pixmap.";
                break;
            case EGL_BAD_NATIVE_WINDOW:
                errortext   = "EGL_BAD_NATIVE_WINDOW";
                description = "An EGLNativeWindowType argument does not refer to a validnative window.";
                break;
            case EGL_BAD_PARAMETER:
                errortext   = "EGL_BAD_PARAMETER";
                description = "One or more argument values are invalid.";
                break;
            case EGL_BAD_SURFACE:
                errortext   = "EGL_BAD_SURFACE";
                description = "An EGLSurface argument does not name a valid surface (window,pbuffer, or pixmap) configured for rendering";
                break;
            case EGL_CONTEXT_LOST:
                errortext   = "EGL_CONTEXT_LOST";
                description = "A power management event has occurred. The application mustdestroy all contexts and reinitialise client API state and objects to continue rendering.";
                break;
            default:
                errortext   = "Unknown EGL Error";
                description = "";
                break;
        }

        printf( "EGLport ERROR: EGL Error detected in file %s at line %d: %s (0x%X)\n  Description: %s\n", file, line, errortext, error, description );
        return 1;
    }

    return 0;
}

/** @brief Obtain a reference to the system's native display
 * @param window : pointer to save the display reference
 * @return : 0 if the function passed, else 1
 */
int8_t GetNativeDisplay( void )
{
    if (eglSettings[CFG_MODE] == RENDER_RAW)        /* RAW FB mode */
    {
        printf( "EGLport: Using EGL_DEFAULT_DISPLAY\n" );
        nativeDisplay = EGL_DEFAULT_DISPLAY;
    }
    else if (eglSettings[CFG_MODE] == RENDER_SDL)   /* SDL/X11 mode */
    {
#if defined(USE_EGL_SDL)
        printf( "EGLport: Opening SDL/X11 display\n" );
        SDL_VERSION(&sysWmInfo.version);
        SDL_GetWMInfo(&sysWmInfo);
        nativeDisplay = (EGLNativeDisplayType)sysWmInfo.info.x11.display;

        if (nativeDisplay == 0)
        {
            printf( "EGLport ERROR: unable to get display!\n" );
            return 1;
        }
#else
        printf( "EGLport ERROR: SDL mode was not enabled in this compile!\n" );
#endif
    }

    return 0;
}

/** @brief Obtain a reference to the system's native window
 * @param width : desired pixel width of the window (not used by all platforms)
 * @param height : desired pixel height of the window (not used by all platforms)
 * @return : 0 if the function passed, else 1
 */
int8_t GetNativeWindow( uint16_t width, uint16_t height )
{
    nativeWindow = 0;

#if defined(WIZ) || defined(CAANOO)

    nativeWindow = (NativeWindowType)malloc(16*1024);

    if(nativeWindow == NULL) {
        printf( "EGLport ERROR: Memory for window Failed\n" );
        return 1;
    }

#elif defined(RPI)

    EGLBoolean result;
    uint32_t screen_width, screen_height;
    static EGL_DISPMANX_WINDOW_T nativewindow;
    DISPMANX_ELEMENT_HANDLE_T dispman_element;
    DISPMANX_DISPLAY_HANDLE_T dispman_display;
    DISPMANX_UPDATE_HANDLE_T dispman_update;
    VC_RECT_T dst_rect;
    VC_RECT_T src_rect;

    /* create an EGL window surface */
    result = graphics_get_display_size(0 /* LCD */, &screen_width, &screen_height);
    if(result < 0) {
        printf( "EGLport ERROR: RPi graphicget_display_size failed\n" );
        return 1;
    }

    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.width = screen_width;
    dst_rect.height = screen_height;

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = width << 16;
    src_rect.height = height << 16;

    dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
    dispman_update  = vc_dispmanx_update_start( 0 );
    dispman_element = vc_dispmanx_element_add ( dispman_update, dispman_display,
      0 /*layer*/, &dst_rect, 0 /*src*/,
      &src_rect, DISPMANX_PROTECTION_NONE,  (VC_DISPMANX_ALPHA_T*)0 /*alpha*/,  (DISPMANX_CLAMP_T*)0 /*clamp*/,  (DISPMANX_TRANSFORM_T)0 /*transform*/);

    nativewindow.element = dispman_element;
    nativewindow.width = screen_width;
    nativewindow.height = screen_height;
    vc_dispmanx_update_submit_sync( dispman_update );

    nativeWindow = (NativeWindowType)&nativewindow;

#else /* default */

    if (eglSettings[CFG_MODE] == RENDER_RAW)        /* RAW FB mode */
    {
        nativeWindow = 0;
    }
    else if(eglSettings[CFG_MODE] == RENDER_SDL)    /* SDL/X11 mode */
    {
#if defined(USE_EGL_SDL)
        /* SDL_GetWMInfo is populated when display was opened */
        nativeWindow = (NativeWindowType)sysWmInfo.info.x11.window;

        if (nativeWindow == 0)
        {
            printf( "EGLport ERROR: unable to get window!\n" );
            return 1;
        }
#else
        printf( "EGLport ERROR: SDL mode was not enabled in this compile!\n" );
#endif
    }
    else
    {
        printf( "EGLport ERROR: Unknown EGL render mode %d!\n", eglSettings[CFG_MODE] );
        return 1;
    }

#endif /* WIZ / CAANOO */

    return 0;
}

/** @brief Release the system's native display
 */
void FreeNativeDisplay( void )
{
}

/** @brief Release the system's native window
 */
void FreeNativeWindow( void )
{
#if defined(WIZ) || defined(CAANOO)
    if (nativeWindow != NULL) {
        free( nativeWindow );
    }
    nativeWindow = NULL;
#endif /* WIZ / CAANOO */
}

/** @brief Open any system specific resources
 */
void Platform_Open( void )
{
#if defined(PANDORA)
    /* Pandora VSync */
    fbdev = open( "/dev/fb0", O_RDONLY /* O_RDWR */ );
    if ( fbdev < 0 ) {
        printf( "EGLport ERROR: Couldn't open /dev/fb0 for Pandora Vsync\n" );
    }
#elif defined(RPI)
    bcm_host_init();
#endif /* PANDORA */
}

/** @brief Release any system specific resources
 */
void Platform_Close( void )
{
#if defined(PANDORA)
    /* Pandora VSync */
    close( fbdev );
    fbdev = -1;
#endif /* PANDORA */
}

/** @brief Check the systems vsync state
 */
void Platform_VSync( void )
{
#if defined(PANDORA)
    /* Pandora VSync */
    if (fbdev >= 0) {
        int arg = 0;
        ioctl( fbdev, FBIO_WAITFORVSYNC, &arg );
    }
#endif /* PANDORA */
}

/** @brief Get the system tick time (ms)
 */
uint32_t Platform_GetTicks( void )
{
    uint32_t ticks = 0;
#if defined(USE_EGL_SDL)
    ticks = SDL_GetTicks();
#else
    printf( "EGLport ERROR: SDL mode was not enabled in this compile!\n" );
#endif
    return ticks;
}
