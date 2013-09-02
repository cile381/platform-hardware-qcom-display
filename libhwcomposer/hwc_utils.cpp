/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2013, The Linux Foundation. All rights reserved.
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

#include <overlay.h>
#include <cutils/properties.h>
#include <gralloc_priv.h>
#include <fb_priv.h>
#include "hwc_utils.h"
#include "mdp_version.h"
#include "hwc_video.h"
#include "hwc_pip.h"
#include "hwc_qbuf.h"
#include "hwc_copybit.h"
#include "hwc_external.h"
#include "hwc_mdpcomp.h"
#include "hwc_extonly.h"
#include "hwc_service.h"
#include "qdMetaData.h"

#include <math.h>

namespace qhwc {

ConfigChangeState* ConfigChangeState::sConfigChangeState = NULL;
// Opens Framebuffer device
static void openFramebufferDevice(hwc_context_t *ctx)
{
    hw_module_t const *module;
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module) == 0) {
        framebuffer_open(module, &(ctx->mFbDev));
    }
}

void initContext(hwc_context_t *ctx)
{
    openFramebufferDevice(ctx);
    ctx->mOverlay = overlay::Overlay::getInstance();
    ctx->mHwcService = hwcService::HWComposerService::getInstance();
    ctx->mHwcService->setHwcContext(ctx);
    ctx->qbuf = new QueuedBufferStore();
    ctx->mMDP.version = qdutils::MDPVersion::getInstance().getMDPVersion();
    ctx->mMDP.hasOverlay = qdutils::MDPVersion::getInstance().hasOverlay();
    ctx->mMDP.panel = qdutils::MDPVersion::getInstance().getPanelType();

    char value[PROPERTY_VALUE_MAX];
    // Check if the target has the C2D API Support
    ctx->mCopybitEngine = NULL;
    property_get("c2d.api.supported", value, "0");
    if(atoi(value)) {
        ctx->mCopybitEngine = CopybitEngine::getInstance();
    }

    ctx->mExtDisplay = new ExternalDisplay(ctx);
    MDPComp::init(ctx);

    property_get("debug.egl.swapinterval", value, "1");
    ctx->swapInterval = atoi(value);

    //Initialize dyn threshold to 2.0
    //system property can override this value
    ctx->dynThreshold = 2.0;

    property_get("debug.hwc.dynThreshold", value, "2");
    ctx->dynThreshold = atof(value);

    ctx->hdmi_pending = false;

    ctx->oscparams.set(0);
    for(int i=0; i<PP_MAX_VG_PIPES; i++){
        ctx->ossrcparams[i].set(0);
        ctx->osdstparams[i].set(0);
    }

    ctx->mConfigChangeType = qhwc::NO_CORRECTION;
    ctx->mConfigChangeState = qhwc::ConfigChangeState::getInstance();

    pthread_mutex_init(&(ctx->mConfigChangeLock), NULL);
    pthread_cond_init(&(ctx->mConfigChangeCond), NULL);

    ctx->mExtCommit = false;
    pthread_mutex_init(&(ctx->mExtCommitLock), NULL);
    pthread_cond_init(&(ctx->mExtCommitCond), NULL);

    ctx->mExtCommitDone = false;
    pthread_mutex_init(&(ctx->mExtCommitDoneLock), NULL);
    pthread_cond_init(&(ctx->mExtCommitDoneCond), NULL);

    pthread_mutex_init(&(ctx->vstate.lock), NULL);
    pthread_cond_init(&(ctx->vstate.cond), NULL);
    ctx->vstate.enable = false;

    ctx->mPQCState = PQC_STOP;

    ALOGI("Initializing Qualcomm Hardware Composer");
    ALOGI("MDP version: %d", ctx->mMDP.version);
    ALOGI("DYN composition threshold : %f", ctx->dynThreshold);
}

void closeContext(hwc_context_t *ctx)
{
    if(ctx->mOverlay) {
        delete ctx->mOverlay;
        ctx->mOverlay = NULL;
    }

    if(ctx->mCopybitEngine) {
        delete ctx->mCopybitEngine;
        ctx->mCopybitEngine = NULL;
    }

    if(ctx->mFbDev) {
        framebuffer_close(ctx->mFbDev);
        ctx->mFbDev = NULL;
    }

    if(ctx->qbuf) {
        delete ctx->qbuf;
        ctx->qbuf = NULL;
    }

    if(ctx->mExtDisplay) {
        delete ctx->mExtDisplay;
        ctx->mExtDisplay = NULL;
    }

    pthread_mutex_destroy(&(ctx->vstate.lock));
    pthread_cond_destroy(&(ctx->vstate.cond));

    free(const_cast<hwc_methods_t *>(ctx->device.methods));

}

void dumpLayer(hwc_layer_t const* l)
{
    ALOGD("\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, {%d,%d,%d,%d}"
          ", {%d,%d,%d,%d}",
          l->compositionType, l->flags, l->handle, l->transform, l->blending,
          l->sourceCrop.left,
          l->sourceCrop.top,
          l->sourceCrop.right,
          l->sourceCrop.bottom,
          l->displayFrame.left,
          l->displayFrame.top,
          l->displayFrame.right,
          l->displayFrame.bottom);
}

void dumpMetaData(const private_handle_t* hnd) {
    if (hnd) {
        MetaData_t *data = (MetaData_t *) hnd->base_metadata;
        if(!data)
            return;
        ALOGW("Metadata: operation: 0x%x", data->operation);
        ALOGW("Metadata: hsic data: %d %f %d %f", data->hsicData.hue,
                data->hsicData.saturation, data->hsicData.intensity,
                data->hsicData.contrast);
        ALOGW("Metadata: sharpness: %d", data->sharpness);
        ALOGW("Metadata: interlaced: %d", data->interlaced);
        ALOGW("Metadata: video interface: %d", data->video_interface);
    }
}

void getLayerStats(hwc_context_t *ctx, const hwc_layer_list_t *list)
{
    //Video specific stats
    int yuvCount = 0;
    int yuvLayerIndex = -1;
    int pipLayerIndex = -1; //2nd video in pip scenario
    bool isYuvLayerSkip = false;
    int skipCount = 0;
    int ccLayerIndex = -1; //closed caption
    int extLayerIndex = -1; //ext-only or block except closed caption
    int extCount = 0; //ext-only except closed caption
    bool isExtBlockPresent = false; //is BLOCK layer present
    bool yuvSecure = false;
    int layerS3DFormat = 0; // is the layer format 3D

    for (size_t i = 0; i < list->numHwLayers; i++) {
        private_handle_t *hnd =
            (private_handle_t *)list->hwLayers[i].handle;

        if (UNLIKELY(isYuvBuffer(hnd))) {
            MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;
            yuvCount++;
            if(yuvCount==1) {
                //Set the primary video to the video layer in
                //lower z-order
                yuvLayerIndex = i;
            }
            if(yuvCount == 2) {
                //In case of two videos, set the pipLayerIndex to the
                //second video
                pipLayerIndex = i;
            }
            yuvSecure = isSecureBuffer(hnd);
            //Animating
            //Do not mark as SKIP if it is secure buffer
            if (isSkipLayer(&list->hwLayers[i]) && !yuvSecure) {
                isYuvLayerSkip = true;
            }
            if (metadata->operation & PP_PARAM_S3D_VIDEO) {
                layerS3DFormat = metadata->s3d_format;
            }
            // if YUV layer is skip layer, then the layer will be composed using
            // GPU. since GPU does not support 3D format mark the skip layer
            // flag as false.
            if(layerS3DFormat) {
                isYuvLayerSkip = false;
            }
        } else if(UNLIKELY(isExtCC(hnd))) {
            ccLayerIndex = i;
        } else if(UNLIKELY(isExtBlock(hnd))) {
            extCount++;
            extLayerIndex = i;
            isExtBlockPresent = true;
        } else if(UNLIKELY(isExtOnly(hnd))) {
            extCount++;
            //If BLOCK layer present, dont cache index, display BLOCK only.
            if(isExtBlockPresent == false) extLayerIndex = i;
        } else if (isSkipLayer(&list->hwLayers[i])) { //Popups
            skipCount++;
        }
    }
    VideoOverlay::setStats(yuvCount, yuvLayerIndex, isYuvLayerSkip,
            ccLayerIndex, layerS3DFormat);
    VideoPIP::setStats(yuvCount, yuvLayerIndex, isYuvLayerSkip,
            pipLayerIndex);
    ExtOnly::setStats(extCount, extLayerIndex, isExtBlockPresent);
    CopyBit::setStats(skipCount);
    MDPComp::setStats(skipCount, extCount, (ccLayerIndex != -1),
            yuvLayerIndex,yuvSecure,pipLayerIndex,layerS3DFormat);

    ctx->numHwLayers = list->numHwLayers;
    return;
}

void set_ov_dimensions(hwc_context_t *ctx, int numVideoLayer,
        hwc_rect& crop, hwc_rect_t& dst) {
    if(numVideoLayer < PP_MAX_VG_PIPES) {
        if(ctx->ossrcparams[numVideoLayer].isValid) {
            crop.left = ctx->ossrcparams[numVideoLayer].left;
            crop.top = ctx->ossrcparams[numVideoLayer].top;
            crop.right = ctx->ossrcparams[numVideoLayer].right;
            crop.bottom = ctx->ossrcparams[numVideoLayer].bottom;
            ALOGD("Overscan crop values are %d %d %d %d",crop.left,
                    crop.top,crop.right,crop.bottom);
        }

        if(ctx->osdstparams[numVideoLayer].isValid) {
            dst.left = ctx->osdstparams[numVideoLayer].left;
            dst.top = ctx->osdstparams[numVideoLayer].top;
            dst.right = ctx->osdstparams[numVideoLayer].right;
            dst.bottom = ctx->osdstparams[numVideoLayer].bottom;
            ALOGD("Overscan destination values are %d %d %d %d",dst.left,
                    dst.top,dst.right,dst.bottom);
        }
    }
}

//Crops source buffer against destination and FB boundaries
void calculate_crop_rects(hwc_rect_t& crop, hwc_rect_t& dst,
        const int fbWidth, const int fbHeight) {

    int& crop_x = crop.left;
    int& crop_y = crop.top;
    int& crop_r = crop.right;
    int& crop_b = crop.bottom;
    int crop_w = crop.right - crop.left;
    int crop_h = crop.bottom - crop.top;

    int& dst_x = dst.left;
    int& dst_y = dst.top;
    int& dst_r = dst.right;
    int& dst_b = dst.bottom;
    int dst_w = dst.right - dst.left;
    int dst_h = dst.bottom - dst.top;

    if(dst_x < 0) {
        float scale_x =  crop_w * 1.0f / dst_w;
        float diff_factor = (scale_x * abs(dst_x));
        crop_x = crop_x + (int)diff_factor;
        crop_w = crop_r - crop_x;

        dst_x = 0;
        dst_w = dst_r - dst_x;;
    }
    if(dst_r > fbWidth) {
        float scale_x = crop_w * 1.0f / dst_w;
        float diff_factor = scale_x * (dst_r - fbWidth);
        crop_r = crop_r - diff_factor;
        crop_w = crop_r - crop_x;

        dst_r = fbWidth;
        dst_w = dst_r - dst_x;
    }
    if(dst_y < 0) {
        float scale_y = crop_h * 1.0f / dst_h;
        float diff_factor = scale_y * abs(dst_y);
        crop_y = crop_y + diff_factor;
        crop_h = crop_b - crop_y;

        dst_y = 0;
        dst_h = dst_b - dst_y;
    }
    if(dst_b > fbHeight) {
        float scale_y = crop_h * 1.0f / dst_h;
        float diff_factor = scale_y * (dst_b - fbHeight);
        crop_b = crop_b - diff_factor;
        crop_h = crop_b - crop_y;

        dst_b = fbHeight;
        dst_h = dst_b - dst_y;
    }
}

void wait4fbPost(hwc_context_t* ctx) {
    framebuffer_device_t *fbDev = ctx->mFbDev;
    if(fbDev) {
        private_module_t* m = reinterpret_cast<private_module_t*>(
                              fbDev->common.module);
        //wait for the fb_post to be called
        pthread_mutex_lock(&m->fbPostLock);
        while(m->fbPostDone == false) {
            pthread_cond_wait(&(m->fbPostCond), &(m->fbPostLock));
        }
        m->fbPostDone = false;
        pthread_mutex_unlock(&m->fbPostLock);
    }
}

void wait4Pan(hwc_context_t* ctx) {
    framebuffer_device_t *fbDev = ctx->mFbDev;
    if(fbDev) {
        private_module_t* m = reinterpret_cast<private_module_t*>(
                              fbDev->common.module);
        //wait for the fb_post's PAN to finish
        pthread_mutex_lock(&m->fbPanLock);
        while(m->fbPanDone == false) {
            pthread_cond_wait(&(m->fbPanCond), &(m->fbPanLock));
        }
        m->fbPanDone = false;
        pthread_mutex_unlock(&m->fbPanLock);
    }
}

// Used in ExtCommit thread to wait for the signal from hwc_set
void wait4CommitSignal(hwc_context_t* ctx) {
    if(ctx) {
        pthread_mutex_lock(&ctx->mExtCommitLock);
        while(ctx->mExtCommit == false) {
            pthread_cond_wait(&(ctx->mExtCommitCond), &(ctx->mExtCommitLock));
        }
        ctx->mExtCommit = false;
        pthread_mutex_unlock(&ctx->mExtCommitLock);
    }
}

// Used in hwc_set to wait for External commit to finish
void wait4ExtCommitDone(hwc_context_t* ctx) {
    if(ctx) {
        pthread_mutex_lock(&ctx->mExtCommitDoneLock);
        while(ctx->mExtCommitDone  == false) {
            pthread_cond_wait(&(ctx->mExtCommitDoneCond),
                              &(ctx->mExtCommitDoneLock));
        }
        ctx->mExtCommitDone = false;
        pthread_mutex_unlock(&ctx->mExtCommitDoneLock);
    }
}

int commitOnPrimary(hwc_context_t* ctx) {
    framebuffer_device_t *fbDev = ctx->mFbDev;
    struct fb_var_screeninfo info;
    if(fbDev) {
        private_module_t* m = reinterpret_cast<private_module_t*>(
                fbDev->common.module);
        if (ioctl(m->framebuffer->fd, FBIOPUT_VSCREENINFO, &m->info) == -1) {
            ALOGE("In %s: FBIOPUT_VSCREENINFO failed Err Str = %s", __FUNCTION__,
                    strerror(errno));
            return -errno;
        }
        if (ioctl(m->framebuffer->fd,FBIOGET_VSCREENINFO,&info) < 0) {
            ALOGE("In %s: FBIOGET_VSCREENINFO failed Err Str = %s", __FUNCTION__,
                    strerror(errno));
            return -errno;
        }
        m->info.pixclock = info.pixclock;
        m->info.reserved[4] = info.reserved[4];
    }
    return 0;
}

#ifdef USES_PLL_CALCULATION

struct min_display_resolution_info {
    int video_format;
    uint32_t xres;
    uint32_t yres;
    int framerate;
};

static struct min_display_resolution_info supported_video_mode_lut[] = {
    {m640x480p60_4_3,     640,  480,  60},
    {m720x480p60_4_3,     720,  480,  60},
    {m720x480p60_16_9,    720,  480,  60},
    {m1280x720p60_16_9,  1280,  720,  60},
    {m1920x1080i60_16_9, 1920,  540,  60},
    {m1440x480i60_4_3,   1440,  240,  60},
    {m1440x480i60_16_9,  1440,  240,  60},
    {m1920x1080p60_16_9, 1920, 1080,  60},
    {m720x576p50_4_3,     720,  576,  50},
    {m720x576p50_16_9,    720,  576,  50},
    {m1280x720p50_16_9,  1280,  720,  50},
    {m1440x576i50_4_3,   1440,  288,  50},
    {m1440x576i50_16_9,  1440,  288,  50},
    {m1920x1080p50_16_9, 1920, 1080,  50},
    {m1920x1080p24_16_9, 1920, 1080,  24},
    {m1920x1080p25_16_9, 1920, 1080,  25},
    {m1920x1080p30_16_9, 1920, 1080,  30},
};

typedef struct {
    int req_ppm;
    int pll_ppm;
    int frame_rate;
} CHANGE_DATA;

#define CD_LIMIT 35
typedef struct {
    int fps;
    CHANGE_DATA cd[CD_LIMIT];
} PPM_DB;

#define PPM_LOW_LIMIT -6000
#define PPM_HIGH_LIMIT 6000

static PPM_DB ppm_check_point[] = {
        {60000,
            {
                {PPM_LOW_LIMIT},
                {-5565, -5565, 16759953},
                {-4329, -4329, 16739124},
                {-2597, -2597, 16710069},
                {-1443,  -673, 16690766},
                {    0,     0, 16666667},
                { 1856,  1925, 16635810},
                { 2598,  3780, 16623497},
                { 4330,  4330, 16594827},
                { 5904,  6254, 16568848},
                {PPM_HIGH_LIMIT}
            }
        },
        {50000,
            {
                {PPM_LOW_LIMIT},
                {-5878, -3751, 20118270},
                {-5714, -3569, 20114945},
                {-5565, -3405, 20111946},
                {-5430, -3257, 20109205},
                {-5307, -3122, 20106737},
                {-5194, -2998, 20104449},
                {-4995, -2782, 20100417},
                {-4906, -2686, 20098608},
                {-2597, -2597, 20052086},
                { -288,  -288, 20005775},
                { -199,  -199, 20004000},
                { -103,  -103, 20002081},
                {    0,     0, 20000000},
                {  113,   113, 19997744},
                {  237,   237, 19995278},
                {  372,   372, 19992582},
                {  520,   520, 19989615},
                {  684,   684, 19986340},
                {  866,   866, 19982700},
                { 1070,  1070, 19978635},
//              { 1299,  1299, 0},
                { 1559,  1559, 19968875},
                { 1856,  1856, 19962965},
                { 2198,  2198, 19956144},
                { 2598,  2598, 19948188},
                { 3070,  3070, 19938796},
                { 3637,  3637, 19927539},
                { 4330,  4330, 19913795},
                { 5195,  5195, 19896632},
                {PPM_HIGH_LIMIT}
            }
        },
        {30000,
            {
                {PPM_LOW_LIMIT},
                {-4329, -3366, 33478263},
                {-2597, -2597, 33420141},
                { -618,  1251, 33353958},
                {  482,  3230, 33317299},
                { 4330,  4330, 33189658},
                {PPM_HIGH_LIMIT}
            }
        },
        {25000,
            {
                {PPM_LOW_LIMIT},
                {-2597, -2597, 40104163},
                { 2021,  2021, 39919350},
                { 2292,  2292, 39908525},
                { 2598,  2598, 39896363},
                { 2944,  2944, 39882608},
                { 3340,  3340, 39866855},
                { 3797,  3797, 39848717},
                { 4330,  4330, 39827589},
                { 4959,  4959, 39802637},
                { 5715,  5715, 39772715},
                {PPM_HIGH_LIMIT}
            }
        },
        {24000,
            {
                {PPM_LOW_LIMIT},
                {-4329, -4329, 42050974},
                {  482,   482, 41646635},
                { 1856,  1856, 41589499},
                { 4330,  4330, 41487072},
                {PPM_HIGH_LIMIT}
            }
        }
};

int getChangeData(int fps, int req_ppm, int offset, CHANGE_DATA **set_cd)
{
    int ppm_check_count = sizeof(ppm_check_point)/sizeof(ppm_check_point[0]);
    int ret             = -1;
    CHANGE_DATA *cd     = NULL;
    int cd_size         = 0;
    int set             = 0;
    int i;

    for (i = 0; i < ppm_check_count; i++) {
        if (ppm_check_point[i].fps == fps) {
            cd = ppm_check_point[i].cd;
            break;
        }
    }

    if (NULL == cd)
        return ret;

    for (i = 0; cd[i].req_ppm < PPM_HIGH_LIMIT; i++);

    cd_size = i;

    for (i = 0; cd[i].req_ppm < PPM_HIGH_LIMIT; i++) {

        if (cd[i].req_ppm == req_ppm) {
            if ((i + offset >= 0) && (i + offset < cd_size)) {
                *set_cd = cd + i + offset;
                set = 1;
                ret = 0;
                break;
            }
        }
    }

    if (!set)
        return ret;

    if ((*set_cd)->req_ppm == PPM_LOW_LIMIT)
            *set_cd = cd + i + 1;
        else if ((*set_cd)->req_ppm == PPM_HIGH_LIMIT)
            *set_cd = cd + i;

    return ret;
}

int validatePPM(int fps, int ppm, CHANGE_DATA **set_cd)
{
    int ret = -1;
    int ppm_check_count = sizeof(ppm_check_point)/sizeof(ppm_check_point[0]);
    CHANGE_DATA *cd = NULL;
    int i;

    if (ppm < PPM_LOW_LIMIT || ppm > PPM_HIGH_LIMIT)
        return ret;

    for (i = 0; i < ppm_check_count; i++) {
        if (ppm_check_point[i].fps == fps) {
            cd = ppm_check_point[i].cd;
            break;
        }
    }

    if (NULL == cd)
        return ret;

    for (i = 0; cd[i].req_ppm < PPM_HIGH_LIMIT; i++) {
        int set = 0;

        if (cd[i].req_ppm == ppm) {
            *set_cd = cd + i;
            set = 1;
        } else if (ppm > cd[i].req_ppm && ppm <= cd[i + 1].req_ppm) {
            if (ppm <= (int)floor((cd[i].req_ppm + cd[i + 1].req_ppm)/2))
                *set_cd = cd + i;
            else
                *set_cd = cd + i + 1;

            set = 1;
        }

        if (!set)
            continue;

        if ((*set_cd)->req_ppm == PPM_LOW_LIMIT)
            *set_cd = cd + i + 1;
        else if ((*set_cd)->req_ppm == PPM_HIGH_LIMIT)
            *set_cd = cd + i;

        if (set) {
            ret = 0;
            break;
        }
    }
    return ret;
}
#endif

bool changePLLSettings(hwc_context_t* ctx) {
    bool ret = true;
#ifdef USES_PLL_CALCULATION

    ALOGE("<<<<<< %s >>>>>>", __FUNCTION__);

    int pll_file = -1;
    int err = -1;
    int ctrl_reg[8];
    int numchannels = 1;

    float framerate          = (ctx->mConfigChangeParams).param1;
    float ppm                = (ctx->mConfigChangeParams).param2;
    uint32_t base_pixelclock = 0;
    int req_bitclk           = 0;
    int pll_bitclk           = 0;
    int bit_clk_match        = 0;
    float change_pt_ppm;
    CHANGE_DATA *set_cd, *set_cd1, *set_cd2;

    qhwc::ExternalDisplay *externalDisplay = ctx->mExtDisplay;
    framebuffer_device_t *fbDev = ctx->mFbDev;
    private_module_t* m = reinterpret_cast<private_module_t*>(
                            fbDev->common.module);

    if(isPanelHDMI(0)) {
        /* Panel is HDMI. Update pixclock and return */

        const struct min_display_resolution_info *mode =
            &supported_video_mode_lut[0];
        unsigned count =  sizeof(supported_video_mode_lut)/sizeof
            (*supported_video_mode_lut);

        for (unsigned int i = 0; i < count; ++i) {

            const struct min_display_resolution_info *cur =
                                &supported_video_mode_lut[i];

            if( (cur->xres == m->info.xres) &&
                    (cur->yres == m->info.yres) &&
                    (cur->framerate*1000 == framerate) ) {

                if(externalDisplay) {
                    externalDisplay->changeHDMIPrimaryResolution(
                                            cur->video_format);
                    break;
                }
            }
        }
        return ret;
    }

    /* Panel is LVDS. numchannels is 2 */
    numchannels = 2;

    /* Check if framerate also has to be changed */
    if(framerate)
        base_pixelclock = (uint32_t)floor(((float)m->default_pixclock /
                            (float)m->default_framerate) *
                                (framerate / (float)numchannels));
    else
        base_pixelclock = m->default_pixclock;

    /* The panel is LVDS. Proceed accordingly */

    pll_file = open(LVDS_PLL_UPDATE,O_WRONLY,0);

    if(pll_file < 0) {
        ALOGE("%s: LVDS PLL file %s not found: ret:%d err str: %s",
                __FUNCTION__,LVDS_PLL_UPDATE,pll_file,strerror(errno));
        ret = false;
        return ret;
    }

    if (validatePPM(framerate ? (int)floor(framerate) : 60000,
                (int)floor(ppm), &set_cd))
        return err;

    change_pt_ppm = set_cd->pll_ppm * 1.0;

    req_bitclk  = base_pixelclock * 7;
    req_bitclk += (int)floor((float)req_bitclk * (ppm / 1000000.0));

    pll_bitclk  = base_pixelclock * 7;
    pll_bitclk += (int)floor((float)pll_bitclk * (change_pt_ppm / 1000000.0));

    int h_bk_porch  = 0;
    int h_bk_porch1 = 0;
    int h_bk_porch2 = 0;
    int h_total     = 2200;
    int v_total     = 1125;
    float req_fps   = ((((float)req_bitclk / 7) *
                        ((float) m->default_framerate / 1000.0)) /
                            (float)m->default_pixclock) * 2;

    if (req_bitclk != pll_bitclk) {
        bit_clk_match = 1;
        h_bk_porch = (int)floor((((float)h_total * 1000) /
                        ((float)set_cd->frame_rate * req_fps)) * 1000000) -
                        h_total;
    }

    if (abs(h_bk_porch) % 2 != 0) {
        if (h_bk_porch < 0)
            h_bk_porch += 1;
        else
            h_bk_porch -= 1;
    }

    if (bit_clk_match) {
        float pll_porch_fps  = (((float)pll_bitclk / 7 ) /
                                (float)((h_total + h_bk_porch) * v_total)) * 2;
        float pll_porch_fps1 = 0;
        float pll_porch_fps2 = 0;

        int pll_bitclk1 = 0;
        int pll_bitclk2 = 0;

        if (getChangeData(framerate ? (int)floor(framerate) : 60000,
            set_cd->req_ppm, -1, &set_cd1))
                set_cd1 = NULL;

        if (getChangeData(framerate ? (int)floor(framerate) : 60000,
            set_cd->req_ppm, 1, &set_cd2))
                set_cd2 = NULL;

        if (set_cd1) {
            h_bk_porch1 = (int)floor((((float)h_total * 1000) /
                ((float)set_cd1->frame_rate * req_fps)) * 1000000) - h_total;

            if (abs(h_bk_porch1) % 2 != 0) {
                if (h_bk_porch1 < 0)
                    h_bk_porch1 += 1;
                else
                    h_bk_porch1 -= 1;
            }

            pll_bitclk1  = base_pixelclock * 7;
            pll_bitclk1 += (int)floor((float)pll_bitclk1 *
                                ((float)set_cd1->pll_ppm / 1000000.0));

            pll_porch_fps1 = (((float)pll_bitclk1 / 7 ) /
                            (float)((h_total + h_bk_porch1) * v_total)) * 2;
        }

        if (set_cd2) {
            h_bk_porch2 = (int)floor((((float)h_total * 1000) /
                ((float)set_cd2->frame_rate * req_fps)) * 1000000) - h_total;

            if (abs(h_bk_porch2) % 2 != 0) {
                if (h_bk_porch2 < 0)
                    h_bk_porch2 += 1;
                else
                    h_bk_porch2 -= 1;
            }

            pll_bitclk2  = base_pixelclock * 7;
            pll_bitclk2 += (int)floor((float)pll_bitclk2 *
                               ((float)set_cd2->pll_ppm / 1000000.0));

            pll_porch_fps2 = (((float)pll_bitclk2 / 7 ) /
                                (float)((h_total + h_bk_porch2) * v_total)) * 2;
        }

        ALOGE("%s: Req FPS is \t\t%f",    __FUNCTION__, req_fps);
        ALOGE("%s: PLL FPS is \t\t%f",    __FUNCTION__, pll_porch_fps);
        ALOGE("%s: PLL -1 FPS is \t\t%f", __FUNCTION__, pll_porch_fps1);
        ALOGE("%s: PLL +1 FPS is \t\t%f", __FUNCTION__, pll_porch_fps2);

        float val  = abs((int)((req_fps * 1000) - (pll_porch_fps  * 1000)));
        float val1 = abs((int)((req_fps * 1000) - (pll_porch_fps1 * 1000)));
        float val2 = abs((int)((req_fps * 1000) - (pll_porch_fps2 * 1000)));
        float val3;

        if (val < val1) {
            val3 = val;
        } else {
            pll_bitclk = pll_bitclk1;
            h_bk_porch = h_bk_porch1;
            val3       = val1;
            set_cd     = set_cd1;
        }

        if (val3 > val2) {
            pll_bitclk = pll_bitclk2;
            h_bk_porch = h_bk_porch2;
            set_cd     = set_cd2;
        }
    }

    ALOGE("%s: Req bit clock is \t%d",    __FUNCTION__, req_bitclk);
    ALOGE("%s: PLL bit clock is \t%d",    __FUNCTION__, pll_bitclk);
    ALOGE("%s: Req PPM is \t\t%f",        __FUNCTION__, ppm);
    ALOGE("%s: PLL PPM at CD is \t%f",    __FUNCTION__, change_pt_ppm);
    ALOGE("%s: PLL PPM selected is \t%f", __FUNCTION__, (float)set_cd->pll_ppm);
    ALOGE("%s: Porch Value is \t\t%d",    __FUNCTION__, h_bk_porch);

    pll_calculate(pll_bitclk, ref_pixclock, ctrl_reg);

    err = write(pll_file, ctrl_reg, sizeof(ctrl_reg));
    if(err <= 0) {
        ALOGE("%s: file write failed '%s'", __FUNCTION__, LVDS_PLL_UPDATE);
        ret = false;
    } else {
        m->info.pixclock = req_bitclk / 7;
    }
    close(pll_file);

    struct msmfb_metadata metadata;
    metadata.op    = metadata_op_panel_tune;
    metadata.flags = 0;
    metadata.data.panel_cfg.h_back_porch = h_bk_porch;

    if(fbDev)
        if(ioctl(m->framebuffer->fd, MSMFB_METADATA_SET, &metadata) == -1)
            ALOGW("%s: MSMFB_METADATA_SET failed to configure panel bk porch\n",
                    __FUNCTION__);

#endif
    return ret;
}

bool canChangePLLSettings(hwc_context_t* ctx) {
#ifdef USES_PLL_CALCULATION
    if(not isHDMIPrimary()) {
        return false;
    }
    framebuffer_device_t *fbDev = ctx->mFbDev;
    if(fbDev) {
        private_module_t* m = reinterpret_cast<private_module_t*>(
                fbDev->common.module);
        int framerate = (ctx->mConfigChangeParams).param1;
        int ppm = (ctx->mConfigChangeParams).param2;
        if(framerate < 0) {
            ALOGE("%s: Framerate %d is -ve",__FUNCTION__,framerate);
            return false;
        }
        return true;
    }
#endif
    return false;
}

bool set_vsync(hwc_context_t* ctx, bool disable) {
    framebuffer_device_t *fbDev = ctx->mFbDev;
    if(fbDev) {
        private_module_t* m = reinterpret_cast<private_module_t*>(
                ctx->mFbDev->common.module);
        int vsync_enable = 0;
        if(not disable)
            vsync_enable = 1;
        if(ioctl(m->framebuffer->fd, MSMFB_OVERLAY_VSYNC_CTRL,
                    &vsync_enable) < 0) {
            ALOGE("%s: vsync control failed for fb0 : %s",
                    __FUNCTION__, strerror(errno));
            return false;
        }
        return true;
    }
    return false;
}

};//namespace
