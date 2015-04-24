/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010-2013, 2015 The Linux Foundation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/mman.h>

#include <cutils/log.h>
#include <cutils/properties.h>
#include <dlfcn.h>

#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <cutils/atomic.h>

#include <linux/fb.h>
#include <linux/msm_mdp.h>

#include <GLES/gl.h>

#include "gralloc_priv.h"
#include "fb_priv.h"
#include "gr.h"
#include <cutils/properties.h>
#include <profiler.h>

#define EVEN_OUT(x) if (x & 0x0001) {x--;}
#define MDP_ARB_SYS_PATH "/dev/mdp_arb"
/** min of int a, b */
static inline int min(int a, int b) {
    return (a<b) ? a : b;
}
/** max of int a, b */
static inline int max(int a, int b) {
    return (a>b) ? a : b;
}

enum {
    PAGE_FLIP = 0x00000001,
};

struct fb_context_t {
    framebuffer_device_t  device;
};

static struct fb_display_mapping mFbDisplayMapping[] = {
    { GRALLOC_HARDWARE_FB_PRIMARY,   "PRIMARY" },
    { GRALLOC_HARDWARE_FB_SECONDARY, "SECONDARY" },
    { GRALLOC_HARDWARE_FB_TERTIARY,  "TERTIARY" },
};
static const int mFbDisplayMappingLen =
    sizeof(mFbDisplayMapping)/sizeof(struct fb_display_mapping);

#define NUMBER_OF_FB_MAX     (4)
#define LENGTH_OF_NAME_MAX   64
#define FB_ATTRIBUTE_DISPLAY_ID_PATH "/sys/class/graphics/fb%u/msm_fb_disp_id"
#define KPI_LOG_MESSAGE      "fb_post[%s]"

static void place_marker(const char* str)
{
    FILE *fp = NULL;

    fp = fopen( "/proc/bootkpi/marker_entry" , "w" );
    if (fp) {
        fwrite(str , 1 , strlen(str) , fp );
        fclose(fp);
    }
}

static uint32_t get_mdp_pixel_format(uint32_t hal_format)
{
    uint32_t mdp_format = MDP_RGB_888;
    switch(hal_format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            mdp_format = MDP_RGBA_8888;
            break;
        case HAL_PIXEL_FORMAT_RGBX_8888:
            mdp_format = MDP_RGBX_8888;
            break;
        case HAL_PIXEL_FORMAT_RGB_888:
            mdp_format = MDP_RGB_888;
            break;
        case HAL_PIXEL_FORMAT_BGR_888:
            mdp_format = MDP_BGR_888;
            break;
        case HAL_PIXEL_FORMAT_RGB_565:
            mdp_format = MDP_RGB_565;
            break;
    }
    return mdp_format;
}

static int fb_setSwapInterval(struct framebuffer_device_t* dev,
                              int interval)
{
    //XXX: Get the value here and implement along with
    //single vsync in HWC
    char pval[PROPERTY_VALUE_MAX];
    property_get("debug.egl.swapinterval", pval, "-1");
    int property_interval = atoi(pval);
    if (property_interval >= 0)
        interval = property_interval;

    fb_context_t* ctx = (fb_context_t*)dev;
    private_module_t* m = reinterpret_cast<private_module_t*>(
        dev->common.module);
    if (interval < dev->minSwapInterval || interval > dev->maxSwapInterval)
        return -EINVAL;

    m->swapInterval = interval;
    return 0;
}

static int fb_post(struct framebuffer_device_t* dev, buffer_handle_t buffer)
{
    private_module_t* m =
        reinterpret_cast<private_module_t*>(dev->common.module);
    private_handle_t *hnd = static_cast<private_handle_t*>
        (const_cast<native_handle_t*>(buffer));
    size_t offset = 0;
    msmfb_overlay_data overlay_data;
    char log_msg[LENGTH_OF_NAME_MAX];

    memset(&overlay_data, 0x00, sizeof(overlay_data));

    if (!m->automotive)
        offset = hnd->base - m->framebuffer->base;

    /* Set overlay first*/
    if(!m->setOverlay) {
        memset(&(m->overlay), 0x00, sizeof(m->overlay));
        m->overlay.id          = MSMFB_NEW_REQUEST;
        m->overlay.src.width   = hnd->width;
        m->overlay.src.height  = hnd->height;
        m->overlay.src.format  = get_mdp_pixel_format(hnd->format);
        m->overlay.src_rect.x  = 0;
        m->overlay.src_rect.y  = 0;
        m->overlay.src_rect.w  = m->info.xres;
        m->overlay.src_rect.h  = m->info.yres;
        m->overlay.dst_rect.x  = 0;
        m->overlay.dst_rect.y  = 0;
        m->overlay.dst_rect.w  = m->info.xres;
        m->overlay.dst_rect.h  = m->info.yres;
        m->overlay.z_order     = 3;
        m->overlay.alpha       = MDP_ALPHA_NOP;
        m->overlay.transp_mask = MDP_TRANSP_NOP;
        m->overlay.flags       = 0;
        m->overlay.is_fg       = 0;
        if(ioctl(m->mdpArbFd, MSMFB_OVERLAY_SET, &(m->overlay))) {
            ALOGE("%s: MSMFB_OVERLAY_SET for display=%s failed, str=%s",
                    __FUNCTION__, (m->display_id)?(m->display_id):"NULL",
                    strerror(errno));
            return -errno;
        }
        m->setOverlay = 1;
    }

    overlay_data.id = m->overlay.id;
    overlay_data.data.memory_id = hnd->fd;
    overlay_data.data.offset = hnd->offset;
    if(ioctl(m->mdpArbFd, MSMFB_OVERLAY_PLAY, &overlay_data)) {
        ALOGE("%s: MSMFB_OVERLAY_PLAY for display=%s failed, str: %s",
                __FUNCTION__, (m->display_id)?(m->display_id):"NULL",
                strerror(errno));
        return -errno;
    }

    mdp_display_commit commit;
    memset(&commit, 0x00, sizeof(commit));
    commit.flags = MDP_DISPLAY_COMMIT_OVERLAY;
    commit.wait_for_finish = true;
    if(ioctl(m->mdpArbFd, MSMFB_DISPLAY_COMMIT, &commit)) {
        ALOGE("%s: MSMFB_DISPLAY_COMMIT for display=%s failed, str: %s",
                __FUNCTION__, (m->display_id)?(m->display_id):"NULL",
                strerror(errno));
        return -errno;
    } else if(m->automotive) {
        /* Log KPI message for first fb post */
        if (!m->logKPI) {
            memset(log_msg, 0x00, LENGTH_OF_NAME_MAX);
            snprintf(log_msg, LENGTH_OF_NAME_MAX, KPI_LOG_MESSAGE,
                     (m->display_id==NULL) ? "NULL" : m->display_id);
            place_marker(log_msg);
            m->logKPI = 1;
        }
    }

    if(!m->automotive) {
        m->info.activate = FB_ACTIVATE_VBL;
        m->info.yoffset = offset / m->finfo.line_length;
        if (ioctl(m->framebuffer->fd, FBIOPUT_VSCREENINFO, &m->info) == -1) {
            ALOGE("%s: FBIOPUT_VSCREENINFO for primary failed, str: %s",
                    __FUNCTION__, strerror(errno));
            return -errno;
        }
    }
    return 0;
}

static int fb_compositionComplete(struct framebuffer_device_t* dev)
{
    // TODO: Properly implement composition complete callback
    glFinish();

    return 0;
}

static const char *getDisplayId(int idx)
{
    int status = 0;
    int fd = -1;
    int i = 0;
    char fd_name[LENGTH_OF_NAME_MAX];
    const char *display_id = NULL;
    FILE *fp = NULL;
    char fb_display_id[LENGTH_OF_NAME_MAX];
    int fb_id = -1;

    memset(fd_name, 0x00, LENGTH_OF_NAME_MAX);
    snprintf(fd_name, LENGTH_OF_NAME_MAX, FB_ATTRIBUTE_DISPLAY_ID_PATH, idx);
    fp = fopen(fd_name, "r");
    if (!fp) {
        ALOGW("%s can't open display id fb path=%s", __func__, fd_name);
        return display_id;
    } else {
        memset(fb_display_id, 0x00, LENGTH_OF_NAME_MAX);
        fread(fb_display_id, sizeof(char), LENGTH_OF_NAME_MAX, fp);
        fclose(fp);
    }

    for (i = 0; i < mFbDisplayMappingLen; i++) {
        if (!strncmp(fb_display_id, mFbDisplayMapping[i].display_id,
                    strlen(mFbDisplayMapping[i].display_id))) {
            display_id = mFbDisplayMapping[i].display_id;
            break;
        }
    }

    if (i == mFbDisplayMappingLen) {
        ALOGE("%s can't find display_id for fb_idx=%d", __func__, idx);
        return NULL;
    }

    return display_id;
}

static int getFbIdx(struct private_module_t* module, const char* name, int *idx)
{
    int status = 0;
    int fd = -1;
    int i = 0;
    char fd_name[LENGTH_OF_NAME_MAX];
    const char *display_id = NULL;
    FILE *fp = NULL;
    char fb_display_id[LENGTH_OF_NAME_MAX];
    int fb_id = -1;

    for (i = 0; i < mFbDisplayMappingLen; i++) {
        if (!strcmp(name, mFbDisplayMapping[i].fb_name)) {
            display_id = mFbDisplayMapping[i].display_id;
            fb_id = i;
            break;
        }
    }

    if (!display_id) {
        ALOGE("%s can't find display_id for fb=%s", __func__, name);
        return -ENOENT;
    }

    /* Search and match with the new msm_fb_disp_id from the driver*/
    for (i = 0; i < NUMBER_OF_FB_MAX; i++) {
        memset(fd_name, 0x00, LENGTH_OF_NAME_MAX);
        snprintf(fd_name, LENGTH_OF_NAME_MAX, FB_ATTRIBUTE_DISPLAY_ID_PATH, i);
        fp = fopen(fd_name, "r");
        if (!fp) {
            ALOGW("%s can't open display id fb path=%s", __func__, fd_name);
            /* Try next one */
            continue;
        } else {
            memset(fb_display_id, 0x00, LENGTH_OF_NAME_MAX);
            fread(fb_display_id, sizeof(char), LENGTH_OF_NAME_MAX, fp);
            if (!strncmp(display_id, fb_display_id, strlen(display_id))) {
                fb_id = i;
                if (module) {
                    module->display_id = display_id;
                }
                fclose(fp);
                break;
            }
            fclose(fp);
        }
    }
    *idx = fb_id;
    return status;
}

int mapFrameBufferLocked(struct private_module_t* module, int fb_idx)
{
    // already initialized...
    if (module->framebuffer) {
        return 0;
    }
    char const * const device_template[] = {
        "/dev/graphics/fb%u",
        "/dev/fb%u",
        0 };

    int fd = -1;
    int i = 0;
    char fd_name[LENGTH_OF_NAME_MAX];
    char property[PROPERTY_VALUE_MAX];

    i = 0;
    while ((fd < 0) && device_template[i]) {
        snprintf(fd_name, LENGTH_OF_NAME_MAX, device_template[i], fb_idx);
        fd = open(fd_name, O_RDWR, 0);
        i++;
    }
    if (fd < 0) {
        ALOGE("%s can't open fb, name=%s", __func__, fd_name);
        return -errno;
    }

    if (module->mdpArbFd < 0) {
        module->mdpArbFd = fd;
    }

    memset(&module->commit, 0, sizeof(struct mdp_display_commit));

    struct fb_fix_screeninfo finfo;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        ALOGE("%s can't get FSCREENINFO for fb name=%s", __func__, fd_name);
        return -errno;
    }

    struct fb_var_screeninfo info;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("%s can't get VSCREENINFO for fb name=%s", __func__, fd_name);
        return -errno;
    }

    info.reserved[0] = 0;
    info.reserved[1] = 0;
    info.reserved[2] = 0;
    info.xoffset = 0;
    info.yoffset = 0;
    info.activate = FB_ACTIVATE_NOW;

    /* Interpretation of offset for color fields: All offsets are from the
     * right, inside a "pixel" value, which is exactly 'bits_per_pixel' wide
     * (means: you can use the offset as right argument to <<). A pixel
     * afterwards is a bit stream and is written to video memory as that
     * unmodified. This implies big-endian byte order if bits_per_pixel is
     * greater than 8.
     */

    if(info.bits_per_pixel == 32) {
        /*
         * Explicitly request RGBA_8888
         */
        info.bits_per_pixel = 32;
        info.red.offset     = 24;
        info.red.length     = 8;
        info.green.offset   = 16;
        info.green.length   = 8;
        info.blue.offset    = 8;
        info.blue.length    = 8;
        info.transp.offset  = 0;
        info.transp.length  = 8;

        /* Note: the GL driver does not have a r=8 g=8 b=8 a=0 config, so if we
         * do not use the MDP for composition (i.e. hw composition == 0), ask
         * for RGBA instead of RGBX. */
        if (property_get("debug.sf.hw", property, NULL) > 0 &&
                                                           atoi(property) == 0)
            module->fbFormat = HAL_PIXEL_FORMAT_RGBX_8888;
        else if(property_get("debug.composition.type", property, NULL) > 0 &&
                (strncmp(property, "mdp", 3) == 0))
            module->fbFormat = HAL_PIXEL_FORMAT_RGBX_8888;
        else
            module->fbFormat = HAL_PIXEL_FORMAT_RGBA_8888;
    } else if (info.bits_per_pixel == 24) {
        /*
         * Explicitly request RGB_888
         */
        info.bits_per_pixel = 24;
        info.red.offset     = 16;
        info.red.length     = 8;
        info.green.offset   = 8;
        info.green.length   = 8;
        info.blue.offset    = 0;
        info.blue.length    = 8;
        info.transp.offset  = 0;
        info.transp.length  = 0;
        module->fbFormat    = HAL_PIXEL_FORMAT_RGB_888;
    } else {
        /*
         * Explicitly request 5/6/5
         */
        info.bits_per_pixel = 16;
        info.red.offset     = 11;
        info.red.length     = 5;
        info.green.offset   = 5;
        info.green.length   = 6;
        info.blue.offset    = 0;
        info.blue.length    = 5;
        info.transp.offset  = 0;
        info.transp.length  = 0;
        module->fbFormat    = HAL_PIXEL_FORMAT_RGB_565;
    }

    //adreno needs 4k aligned offsets. Max hole size is 4096-1
    int  size = roundUpToPageSize(info.yres * info.xres *
                                                       (info.bits_per_pixel/8));

    /*
     * Request NUM_BUFFERS screens (at least 2 for page flipping)
     */
    int numberOfBuffers = (int)(finfo.smem_len/size);
    ALOGV("num supported framebuffers in kernel = %d", numberOfBuffers);

    if (property_get("debug.gr.numframebuffers", property, NULL) > 0) {
        int num = atoi(property);
        if ((num >= NUM_FRAMEBUFFERS_MIN) && (num <= NUM_FRAMEBUFFERS_MAX)) {
            numberOfBuffers = num;
        }
    }
    if (numberOfBuffers > NUM_FRAMEBUFFERS_MAX)
        numberOfBuffers = NUM_FRAMEBUFFERS_MAX;

    ALOGV("We support %d buffers", numberOfBuffers);

    //consider the included hole by 4k alignment
    uint32_t line_length = (info.xres * info.bits_per_pixel / 8);
    info.yres_virtual = (size * numberOfBuffers) / line_length;

    uint32_t flags = PAGE_FLIP;

    if (info.yres_virtual < ((size * 2) / line_length) ) {
        // we need at least 2 for page-flipping
        info.yres_virtual = size / line_length;
        flags &= ~PAGE_FLIP;
        ALOGW("page flipping not supported (yres_virtual=%d, requested=%d)",
              info.yres_virtual, info.yres*2);
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("%s can't get VSCREENINFO for fb name=%s", __func__, fd_name);
        return -errno;
    }

    if (int(info.width) <= 0 || int(info.height) <= 0) {
        // the driver doesn't return that information
        // default to 160 dpi
        info.width  = ((info.xres * 25.4f)/160.0f + 0.5f);
        info.height = ((info.yres * 25.4f)/160.0f + 0.5f);
    }

    float xdpi = (info.xres * 25.4f) / info.width;
    float ydpi = (info.yres * 25.4f) / info.height;
#ifdef MSMFB_METADATA_GET
    struct msmfb_metadata metadata;
    memset(&metadata, 0 , sizeof(metadata));
    metadata.op = metadata_op_frame_rate;
    if (ioctl(fd, MSMFB_METADATA_GET, &metadata) == -1) {
        ALOGE("Error retrieving panel frame rate");
        return -errno;
    }
    float fps  = metadata.data.panel_frame_rate;
#else
    //XXX: Remove reserved field usage on all baselines
    //The reserved[3] field is used to store FPS by the driver.
    float fps  = info.reserved[3] & 0xFF;
#endif
    ALOGI("using (fd=%d)\n"
          "id           = %s\n"
          "xres         = %d px\n"
          "yres         = %d px\n"
          "xres_virtual = %d px\n"
          "yres_virtual = %d px\n"
          "bpp          = %d\n"
          "r            = %2u:%u\n"
          "g            = %2u:%u\n"
          "b            = %2u:%u\n",
          fd,
          finfo.id,
          info.xres,
          info.yres,
          info.xres_virtual,
          info.yres_virtual,
          info.bits_per_pixel,
          info.red.offset, info.red.length,
          info.green.offset, info.green.length,
          info.blue.offset, info.blue.length
         );

    ALOGI("width        = %d mm (%f dpi)\n"
          "height       = %d mm (%f dpi)\n"
          "refresh rate = %.2f Hz\n",
          info.width,  xdpi,
          info.height, ydpi,
          fps
         );


    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        ALOGE("%s can't get FSCREENINFO for fb name=%s", __func__, fd_name);
        return -errno;
    }

    if (finfo.smem_len <= 0 && !module->automotive) {
        ALOGE("%s smem_len is 0 fb name=%s", __func__, fd_name);
        return -errno;
    }

    module->flags = flags;
    module->info = info;
    module->finfo = finfo;
    module->xdpi = xdpi;
    module->ydpi = ydpi;
    module->fps = fps;
    module->swapInterval = 1;

    CALC_INIT();

    /*
     * map the framebuffer
     */

    int err;
    module->numBuffers = info.yres_virtual / info.yres;
    module->bufferMask = 0;
    //adreno needs page aligned offsets. Align the fbsize to pagesize.
    size_t fbSize = roundUpToPageSize(finfo.line_length * info.yres)*
                    module->numBuffers;
    module->framebuffer = new private_handle_t(fd, fbSize,
                                    private_handle_t::PRIV_FLAGS_USES_ION,
                                    BUFFER_TYPE_UI,
                                    module->fbFormat, info.xres, info.yres);
    /* Don't map framebuffer since driver will use overlay APIs.*/
    if (!module->automotive) {
        void* vaddr = mmap(0, fbSize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        if (vaddr == MAP_FAILED) {
            ALOGE("Error mapping the framebuffer (%s)", strerror(errno));
            return -errno;
        }
        module->framebuffer->base = intptr_t(vaddr);
        memset(vaddr, 0, fbSize);
        module->currentOffset = 0;
        //Enable vsync
        int enable = 1;
        ioctl(module->framebuffer->fd, MSMFB_OVERLAY_VSYNC_CTRL, &enable);
    }
    return 0;
}

static int mapFrameBuffer(struct private_module_t* module, int fb_idx)
{
    int err = -1;
    char property[PROPERTY_VALUE_MAX];
    if(property_get("sys.hwc.automotive_mode_enabled", property, "false")
            && !strcmp(property, "true"))
        module->automotive = 1;

    if(((property_get("debug.gralloc.map_fb_memory", property, NULL) > 0) &&
       (!strncmp(property, "1", PROPERTY_VALUE_MAX ) ||
        (!strncasecmp(property,"true", PROPERTY_VALUE_MAX )))) ||
       module->automotive) {
        pthread_mutex_lock(&module->lock);
        err = mapFrameBufferLocked(module, fb_idx);
        if (err != 0)
            ALOGE("mapFrameBufferLocked error=%d, fb_idx=%d", err, fb_idx);
        pthread_mutex_unlock(&module->lock);
    }
    return err;
}

static int bindMdpArb(struct private_module_t* module, const char *name,
                      int fb_idx)
{
    int status = -EINVAL;
    struct mdp_arb_bind bind;
    int fd = -1;

    fd = open(MDP_ARB_SYS_PATH, O_RDWR);
    if (fd < 0) {
        ALOGE("%s can't open mdp_arb", __FUNCTION__);
        return status;
    }

    memset(&bind, 0x00, sizeof(bind));
    strlcpy(bind.name, name, MDP_ARB_NAME_LEN);
    bind.fb_index = fb_idx;
    status = ioctl(fd, MSMFB_ARB_BIND, &bind);
    if (status < 0) {
        ALOGE("%s Can't bind mdp arb to client hwc, error=%d",
              __FUNCTION__, status);
        close(fd);
    } else {
        module->mdpArbFd = fd;
    }

    return status;
}

static int unbindMdpArb(struct private_module_t* module)
{
    int status = -EINVAL;

    if ((module->mdpArbFd >= 0) &&
        ((!module->framebuffer) ||
         (module->mdpArbFd != module->framebuffer->fd))) {
        status = ioctl(module->mdpArbFd, MSMFB_ARB_UNBIND, NULL);
        if (status < 0) {
            ALOGE("%s Can't unbind mdp arb to client hwc, error=%d",
                  __FUNCTION__, status);
        }
        close(module->mdpArbFd);
        module->mdpArbFd = -1;
    }

    return status;
}

/*****************************************************************************/

static int fb_close(struct hw_device_t *dev)
{
    fb_context_t* ctx = (fb_context_t*)dev;
    mdp_display_commit commit;
    if (ctx) {
        private_module_t* m = (private_module_t*)ctx->device.common.module;
        int overlay_id = m->overlay.id;
        if(m && m->framebuffer && (m->mdpArbFd >= 0) &&
            (overlay_id >= 0)) {
            if(ioctl(m->mdpArbFd, MSMFB_OVERLAY_UNSET,
                &(m->overlay.id))) {
                ALOGE("Error unset overlay");
            }
            memset(&commit, 0x00, sizeof(commit));
            commit.flags = MDP_DISPLAY_COMMIT_OVERLAY;
            commit.wait_for_finish = true;
            if(ioctl(m->mdpArbFd, MSMFB_DISPLAY_COMMIT, &commit)) {
                ALOGE("%s: MSMFB_DISPLAY_COMMIT for display=%s failed, str: %s",
                        __FUNCTION__, (m->display_id)?(m->display_id):"NULL",
                        strerror(errno));
            }
        }
        if (m) {
            unbindMdpArb(m);
            if (m->framebuffer) {
                delete m->framebuffer;
                m->framebuffer = NULL;
            }
            free(m);
        }
        free(ctx);
    }
    return 0;
}

int framebuffer_getDisplayFbIdx(const char *fb_name, int *fb_idx)
{
    int status = -EINVAL;
    if ((fb_name == NULL) || (fb_idx == NULL)) {
        ALOGE("%s fb_name=0x%08x or fb_idx=0x%08x is NULL", __FUNCTION__,
            (int)fb_name, (int)fb_idx);
        status = -EINVAL;
    } else if (strcmp(fb_name, GRALLOC_HARDWARE_FB_PRIMARY) &&
               strcmp(fb_name, GRALLOC_HARDWARE_FB_SECONDARY) &&
               strcmp(fb_name, GRALLOC_HARDWARE_FB_TERTIARY)) {
        ALOGE("%s not support fb device=%s", __func__, fb_name);
        status = -ENOENT;
    } else {
        status = getFbIdx(NULL, fb_name, fb_idx);
        if (status) {
            ALOGE("%s getFbIdx return error=%d, fb_name=%s",
                  __FUNCTION__, status, fb_name);
        }
    }
    return status;
}

int fb_device_open(hw_module_t const* module, const char* name,
                   hw_device_t** device)
{
    int status = -EINVAL;
    if (strcmp(name, GRALLOC_HARDWARE_FB_PRIMARY) &&
        strcmp(name, GRALLOC_HARDWARE_FB_SECONDARY) &&
        strcmp(name, GRALLOC_HARDWARE_FB_TERTIARY)) {
        ALOGE("%s not support fb device=%s", __func__, name);
        status = -ENOENT;
    } else {
        alloc_device_t* gralloc_device = NULL;
        fb_context_t *dev = NULL;
        private_module_t *m = NULL;
        int fb_idx = 0;
        status = gralloc_open(module, &gralloc_device);
        if (status < 0) {
            ALOGE("%s gralloc_open fails, status=%d", __FUNCTION__, status);
            return status;
        }

        /* initialize our state here */
        dev = (fb_context_t*)malloc(sizeof(*dev));
        if (!dev) {
            ALOGE("%s out of memory, dev is NULL", __FUNCTION__);
            goto err;
        }
        memset(dev, 0, sizeof(*dev));

        m = (private_module_t *)malloc(sizeof(*m));
        if (!m) {
            ALOGE("%s out of memory, m is NULL", __FUNCTION__);
            goto err;
        }
        memset(m, 0x00, sizeof(*m));
        memcpy(&m->base.common, module, sizeof(hw_module_t));
        /* initialize the procs */
        dev->device.common.tag      = HARDWARE_DEVICE_TAG;
        dev->device.common.version  = 0;
        dev->device.common.module   = reinterpret_cast<hw_module_t*>(m);
        dev->device.common.close    = fb_close;
        dev->device.setSwapInterval = fb_setSwapInterval;
        dev->device.post            = fb_post;
        dev->device.setUpdateRect   = 0;
        dev->device.compositionComplete = fb_compositionComplete;
        m->mdpArbFd = -1;

        status = getFbIdx(m, name, &fb_idx);
        if (status) {
            ALOGE("%s getFbIdx returns error=%d, name=%s", __FUNCTION__,
                  status, name);
            goto err;
        }
        status = mapFrameBuffer(m, fb_idx);
        if (status == 0) {
            int stride = m->finfo.line_length / (m->info.bits_per_pixel >> 3);
            const_cast<uint32_t&>(dev->device.flags) = 0;
            const_cast<uint32_t&>(dev->device.width) = m->info.xres;
            const_cast<uint32_t&>(dev->device.height) = m->info.yres;
            const_cast<int&>(dev->device.stride) = stride;
            const_cast<int&>(dev->device.format) = m->fbFormat;
            const_cast<float&>(dev->device.xdpi) = m->xdpi;
            const_cast<float&>(dev->device.ydpi) = m->ydpi;
            const_cast<float&>(dev->device.fps) = m->fps;
            const_cast<int&>(dev->device.minSwapInterval) =
                                                        PRIV_MIN_SWAP_INTERVAL;
            const_cast<int&>(dev->device.maxSwapInterval) =
                                                        PRIV_MAX_SWAP_INTERVAL;
            const_cast<int&>(dev->device.numFramebuffers) = m->numBuffers;
            dev->device.setUpdateRect = 0;

            *device = &dev->device.common;
            gralloc_close(gralloc_device);
            return status;
        } else {
            ALOGE("%s mapFrameBuffer error=%d for fb=%s",
                    __func__, status, name);
        }

err:
        if (m)
            free(m);
        if (dev)
            free(dev);
        // Close the gralloc module
        gralloc_close(gralloc_device);
    }
    return status;
}

int framebuffer_open_ex(const struct hw_module_t* module, const char *name,
                        int fb_idx, struct framebuffer_device_t** device)
{
    int status = -EINVAL;
    alloc_device_t* gralloc_device = NULL;
    fb_context_t *dev = NULL;
    private_module_t *m = NULL;
    status = gralloc_open(module, &gralloc_device);
    if (status < 0) {
        ALOGE("%s gralloc_open fails, status=%d", __FUNCTION__, status);
        return status;
    }

    /* initialize our state here */
    dev = (fb_context_t*)malloc(sizeof(*dev));
    if (!dev) {
        ALOGE("%s out of memory, dev is NULL", __FUNCTION__);
        goto err;
    }
    memset(dev, 0, sizeof(*dev));

    m = (private_module_t *)malloc(sizeof(*m));
    if (!m) {
        ALOGE("%s out of memory, m is NULL", __FUNCTION__);
        goto err;
    }
    memset(m, 0x00, sizeof(*m));
    memcpy(&m->base.common, module, sizeof(hw_module_t));
    /* initialize the procs */
    dev->device.common.tag      = HARDWARE_DEVICE_TAG;
    dev->device.common.version  = 0;
    dev->device.common.module   = reinterpret_cast<hw_module_t*>(m);
    dev->device.common.close    = fb_close;
    dev->device.setSwapInterval = fb_setSwapInterval;
    dev->device.post            = fb_post;
    dev->device.setUpdateRect   = 0;
    dev->device.compositionComplete = fb_compositionComplete;
    m->display_id = getDisplayId(fb_idx);
    m->mdpArbFd = -1;

    status = bindMdpArb(m, name, fb_idx);
    if (status) {
        ALOGE("%s bindMdpArb fails=%d, name=%s, fb_idx=%d", __FUNCTION__,
              status, name, fb_idx);
        /* Don't exit, just fallback to legacy framebuffer when MDP arb is not
         * there*/
    }
    status = mapFrameBuffer(m, fb_idx);
    if (status == 0) {
        int stride = m->finfo.line_length / (m->info.bits_per_pixel >> 3);
        const_cast<uint32_t&>(dev->device.flags) = 0;
        const_cast<uint32_t&>(dev->device.width) = m->info.xres;
        const_cast<uint32_t&>(dev->device.height) = m->info.yres;
        const_cast<int&>(dev->device.stride) = stride;
        const_cast<int&>(dev->device.format) = m->fbFormat;
        const_cast<float&>(dev->device.xdpi) = m->xdpi;
        const_cast<float&>(dev->device.ydpi) = m->ydpi;
        const_cast<float&>(dev->device.fps) = m->fps;
        const_cast<int&>(dev->device.minSwapInterval) =
                                                    PRIV_MIN_SWAP_INTERVAL;
        const_cast<int&>(dev->device.maxSwapInterval) =
                                                    PRIV_MAX_SWAP_INTERVAL;
        const_cast<int&>(dev->device.numFramebuffers) = m->numBuffers;
        dev->device.setUpdateRect = 0;

        *device = (struct framebuffer_device_t*)&dev->device.common;
        gralloc_close(gralloc_device);
        return status;
    } else {
        ALOGE("%s mapFrameBuffer error=%d for fb=%s",
                __func__, status, name);
    }

err:
    if (m) {
        unbindMdpArb(m);
        free(m);
    }
    if (dev)
        free(dev);
    // Close the gralloc module
    gralloc_close(gralloc_device);
    return status;
}

