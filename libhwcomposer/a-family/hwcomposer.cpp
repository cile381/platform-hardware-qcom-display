/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>

#include <gralloc_priv.h>
#include <fb_priv.h>
#include <hardware/hwcomposer.h>
#include <overlayLib.h>
#include <overlayLibUI.h>
#include <copybit.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <ui/android_native_buffer.h>
#include <genlock.h>
#include <qcom_ui.h>
#include <utils/comptype.h>
#include <gr.h>
#include <utils/profiler.h>
#include <utils/IdleInvalidator.h>

/*****************************************************************************/
#define ALIGN(x, align) (((x) + ((align)-1)) & ~((align)-1))
#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

#define DEBUG_HWC 0

#define MAX_FRAME_BUFFER_NAME_SIZE 80
#define MAX_DISPLAY_DEVICES 3
#define MAX_DISPLAY_EXTERNAL_DEVICES (MAX_DISPLAY_DEVICES - 1)

enum HWCLayerType{
    HWC_SINGLE_VIDEO           = 0x1,
    HWC_ORIG_RESOLUTION        = 0x2,
    HWC_S3D_LAYER              = 0x4,
    HWC_STOP_UI_MIRRORING_MASK = 0xF
};

enum eHWCOverlayStatus {
    HWC_OVERLAY_OPEN,
    HWC_OVERLAY_PREPARE_TO_CLOSE,
    HWC_OVERLAY_CLOSED
};

struct hwc_context_t {
    hwc_composer_device_t device;
    /* our private state goes below here */
    overlay::Overlay* mOverlayLibObject;
    native_handle_t *previousOverlayHandle;
    native_handle_t *currentOverlayHandle;
    int yuvBufferCount;
    int numLayersNotUpdating;
    int s3dLayerFormat;
#if defined HDMI_DUAL_DISPLAY
    external_display_type mHDMIEnabled; // Type of external display
    bool pendingHDMI;
#endif
    bool forceComposition; //Used to force composition.
    int previousLayerCount;
    eHWCOverlayStatus hwcOverlayStatus;
    int swapInterval;
    bool isPrimary;
    bool premultipliedAlpha;
};


static int extDeviceFbIndex[MAX_DISPLAY_EXTERNAL_DEVICES];

static const char *extFrameBufferName[MAX_DISPLAY_EXTERNAL_DEVICES] =
{
    "dtv panel",
    "writeback panel"
};

static int hwc_device_open(const struct hw_module_t* module,
                           const char* name,
                           struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};


struct private_hwc_module_t {
    hwc_module_t base;
    copybit_device_t *copybitEngine;
    framebuffer_device_t *fbDevice;
    int compositionType;
    bool isBypassEnabled; //from build.prop ro.sf.compbypass.enable
};

struct private_hwc_module_t HAL_MODULE_INFO_SYM = {
    base: {
        common: {
            tag: HARDWARE_MODULE_TAG,
            version_major: 1,
            version_minor: 0,
            id: HWC_HARDWARE_MODULE_ID,
            name: "Hardware Composer Module",
            author: "The Android Open Source Project",
            methods: &hwc_module_methods,
        }
   },
   copybitEngine: NULL,
   fbDevice: NULL,
   compositionType: 0,
   isBypassEnabled: false,
};

//Only at this point would the compiler know all storage class sizes.
//The header has hooks which need to know those beforehand.
#include "external_display_only.h"
#include <bypass.h>


/*****************************************************************************/

static void dump_layer(hwc_layer_t const* l) {
    LOGD("\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, {%d,%d,%d,%d}, {%d,%d,%d,%d}",
            l->compositionType, l->flags, l->handle, l->transform & FINAL_TRANSFORM_MASK, l->blending,
            l->sourceCrop.left,
            l->sourceCrop.top,
            l->sourceCrop.right,
            l->sourceCrop.bottom,
            l->displayFrame.left,
            l->displayFrame.top,
            l->displayFrame.right,
            l->displayFrame.bottom);
}

static inline int min(const int& a, const int& b) {
    return (a < b) ? a : b;
}

static inline int max(const int& a, const int& b) {
    return (a > b) ? a : b;
}
static void swap(int &a, int&b) {
    int c= a;
    a = b;
    b = c;
}

inline void getLayerResolution(const hwc_layer_t* layer, int& width, int& height)
{
   hwc_rect_t displayFrame  = layer->displayFrame;

   width = displayFrame.right - displayFrame.left;
   height = displayFrame.bottom - displayFrame.top;
}

static bool isValidDestination(const framebuffer_device_t* fbDev,
                        const hwc_rect_t& rect)
{
    if (!fbDev) {
        LOGE("%s: fbDev is null", __FUNCTION__);
        return false;
    }

    int dest_width = (rect.right - rect.left);
    int dest_height = (rect.bottom - rect.top);

    if (rect.left < 0 || rect.right < 0 || rect.top < 0 || rect.bottom < 0
        || dest_width <= 0 || dest_height <= 0) {
        LOGE("%s: destination: left=%d right=%d top=%d bottom=%d width=%d"
             "height=%d", __FUNCTION__, rect.left, rect.right, rect.top,
             rect.bottom, dest_width, dest_height);
        return false;
    }

    if ((rect.left+dest_width) > fbDev->width ||
                            (rect.top+dest_height) > fbDev->height) {
        LOGE("%s: destination out of bound params", __FUNCTION__);
        return false;
    }

    return true;
}
//Crops source buffer against destination and FB boundaries
void calculate_crop_rects(hwc_rect_t& crop, hwc_rect_t& dst, int hw_w, int hw_h) {

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
    if(dst_r > hw_w) {
        float scale_x = crop_w * 1.0f / dst_w;
        float diff_factor = scale_x * (dst_r - hw_w);
        crop_r = crop_r - diff_factor;
        crop_w = crop_r - crop_x;

        dst_r = hw_w;
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
    if(dst_b > hw_h) {
        float scale_y = crop_h * 1.0f / dst_h;
        float diff_factor = scale_y * (dst_b - hw_h);
        crop_b = crop_b - diff_factor;
        crop_h = crop_b - crop_y;

        dst_b = hw_h;
        dst_h = dst_b - dst_y;
    }

    LOGD(" crop: [%d,%d,%d,%d] dst:[%d,%d,%d,%d]",
                     crop_x, crop_y, crop_w, crop_h,dst_x, dst_y, dst_w, dst_h);
}

//If displayframe is out of screen bounds, calculates
//valid displayFrame & source crop corresponding to it.
static void correct_crop_rects(const framebuffer_device_t* fbDev,
                        hwc_rect_t& sourceCrop, hwc_rect_t& displayFrame,
                        int transform) {
    if(!isValidDestination(fbDev,displayFrame)) {
        if(transform & HWC_TRANSFORM_ROT_90) {
            swap(sourceCrop.left,sourceCrop.top);
            swap(sourceCrop.right,sourceCrop.bottom);
            calculate_crop_rects(sourceCrop,displayFrame,
                                    fbDev->width,fbDev->height);
            swap(sourceCrop.left,sourceCrop.top);
            swap(sourceCrop.right,sourceCrop.bottom);
        } else {
            calculate_crop_rects(sourceCrop,displayFrame,
                                    fbDev->width,fbDev->height);
        }
    }
}

void calculate_scaled_destination(private_hwc_module_t* hwcModule, int &left,
                                  int &top, int &width, int &height)
{
    if (!hwcModule) {
        LOGE("%s: hwcModule is NULL", __FUNCTION__);
        return;
    }

    // scale the positiion
    framebuffer_device_t *fbDev = hwcModule->fbDevice;
    private_module_t* m = reinterpret_cast<private_module_t*>(
                                           fbDev->common.module);
    if (!m->isDynamicResolutionEnabled)
        return; // Return if we are not in the resolution change mode

    // If dyn. fb resolution is enabled, we need to scale the layer
    // display rectangle to fit into the panel resolution.
    float panel_w = (float)m->info.xres;
    float panel_h = (float)m->info.yres;
    int fb_w      = fbDev->width;
    int fb_h      = fbDev->height;

    float scale_w = panel_w/fb_w;
    float scale_h = panel_h/fb_h;

    left     = (int)(left*scale_w);
    top      = (int)(top*scale_h);
    width    = (int)(width*scale_w);
    height   = (int)(height*scale_h);
}

// Returns true if external panel is connected
static inline bool isExternalConnected(const hwc_context_t* ctx) {
#if defined HDMI_DUAL_DISPLAY
    return (ctx->mHDMIEnabled != EXT_TYPE_NONE);
#endif
    return false;
}

// Returns true if we have a skip layer
static inline bool isSkipLayer(const hwc_layer_t* layer) {
    return (layer && (layer->flags & HWC_SKIP_LAYER));
}

// Returns true if the buffer is yuv
static inline bool isYuvBuffer(const private_handle_t* hnd) {
    return (hnd && (hnd->bufferType == BUFFER_TYPE_VIDEO));
}

//Return true if buffer is marked locked
static inline bool isBufferLocked(const private_handle_t* hnd) {
    return (hnd && (private_handle_t::PRIV_FLAGS_HWC_LOCK & hnd->flags));
}

//Return true if buffer is marked as secure
static inline bool isSecureBuffer(const private_handle_t* hnd) {
    return (hnd && (hnd->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER));
}

static int getLayerS3DFormat (hwc_layer_t &layer) {
    int s3dFormat = 0;
    private_handle_t *hnd = (private_handle_t *)layer.handle;
    if (hnd)
        s3dFormat = FORMAT_3D_INPUT(hnd->format);
    return s3dFormat;
}

//Mark layers for GPU composition but not if it is a 3D layer.
static inline void markForGPUComp(const hwc_context_t *ctx,
    hwc_layer_list_t* list, const int limit) {
    for(int i = 0; i < limit; i++) {
        if( getLayerS3DFormat( list->hwLayers[i] ) ) {
            continue;
        }
        else {
            list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
            list->hwLayers[i].hints &= ~HWC_HINT_CLEAR_FB;
        }
    }
}


static int setVideoOverlayStatusInGralloc(hwc_context_t* ctx, const int value) {
#if defined HDMI_DUAL_DISPLAY
    LOGE_IF(DEBUG_HWC, "%s: value=%d", __FUNCTION__, value);
    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                    ctx->device.common.module);
    if(!hwcModule) {
        LOGE("%s: invalid params", __FUNCTION__);
        return -1;
    }

    framebuffer_device_t *fbDev = hwcModule->fbDevice;
    if (!fbDev) {
        LOGE("%s: fbDev is NULL", __FUNCTION__);
        return -1;
    }

    // Inform the gralloc about the video overlay
    fbDev->perform(fbDev, EVENT_VIDEO_OVERLAY, (void*)&value);
#endif
    return 0;
}

static int hwc_closeOverlayChannels(hwc_context_t* ctx) {
#ifdef USE_OVERLAY
    overlay::Overlay *ovLibObject = ctx->mOverlayLibObject;
    if(!ovLibObject) {
        LOGE("%s: invalid params", __FUNCTION__);
        return -1;
    }

    if (HWC_OVERLAY_PREPARE_TO_CLOSE == ctx->hwcOverlayStatus) {
        // Video mirroring is going on, and we do not have any layers to
        // mirror directly. Close the current video channel and inform the
        // gralloc to start UI mirroring
        ovLibObject->closeChannel();
        // Inform the gralloc that video overlay has stopped.
        setVideoOverlayStatusInGralloc(ctx, VIDEO_OVERLAY_ENDED);
        ctx->hwcOverlayStatus = HWC_OVERLAY_CLOSED;
    }
#endif
    return 0;
}

#ifdef USE_OVERLAY
/*
 * Configures mdp pipes
 */
static int prepareOverlay(hwc_context_t *ctx,
                          hwc_layer_t *layer,
                          int flags) {
    int ret = 0;

     if(ctx && Bypass::get_state() != Bypass::BYPASS_OFF) {
         Bypass::close_pipes(ctx, false);
         Bypass::set_state(Bypass::BYPASS_OFF);
     }

    if (LIKELY(ctx && ctx->mOverlayLibObject)) {
        private_hwc_module_t* hwcModule =
            reinterpret_cast<private_hwc_module_t*>(ctx->device.common.module);
        if (UNLIKELY(!hwcModule)) {
            LOGE("%s: null module", __FUNCTION__);
            return -1;
        }

        private_handle_t *hnd = (private_handle_t *)layer->handle;
        overlay::Overlay *ovLibObject = ctx->mOverlayLibObject;
        overlay_buffer_info info;
        info.width = hnd->width;
        info.height = hnd->height;
        info.format = hnd->format;
        info.size = hnd->size;

        int hdmiConnected = 0;

#if defined HDMI_DUAL_DISPLAY
        if(!ctx->pendingHDMI) //makes sure the UI channel is opened first
            hdmiConnected = (int)ctx->mHDMIEnabled;
#endif
        if(ctx->premultipliedAlpha)
             flags |= OVERLAY_BLENDING_PREMULT;
        else
             flags &= ~OVERLAY_BLENDING_PREMULT;

        ret = ovLibObject->setSource(info, layer->transform,
                            hdmiConnected, flags);
        if (!ret) {
            LOGE("prepareOverlay setSource failed");
            return -1;
        }

        ret = ovLibObject->setTransform(layer->transform);
        if (!ret) {
            LOGE("prepareOverlay setTransform failed transform %x",
                    layer->transform);
            return -1;
        }

        hwc_rect_t sourceCrop = layer->sourceCrop;
        hwc_rect_t displayFrame = layer->displayFrame;

        ret = ovLibObject->setCrop(sourceCrop.left, sourceCrop.top,
                                  (sourceCrop.right - sourceCrop.left),
                                  (sourceCrop.bottom - sourceCrop.top));
        if (!ret) {
            LOGE("prepareOverlay setCrop failed");
            return -1;
        }
#if defined HDMI_DUAL_DISPLAY
        // Send the device orientation to  overlayLib
        if(hwcModule) {
            framebuffer_device_t *fbDev = reinterpret_cast<framebuffer_device_t*>
                                                            (hwcModule->fbDevice);
            if(fbDev) {
                private_module_t* m = reinterpret_cast<private_module_t*>(
                                                         fbDev->common.module);
                if(m) {
                    ovLibObject->setDeviceOrientation(m->orientation);
                }
            }
        }
#endif

        ret = ovLibObject->setPosition(displayFrame.left, displayFrame.top,
                            (displayFrame.right - displayFrame.left),
                            (displayFrame.bottom - displayFrame.top));
        if (!ret) {
            LOGE("prepareOverlay setPosition failed");
            return -1;
        }
    }
    return 0;
}
#endif //USE_OVERLAY

void unlockPreviousOverlayBuffer(hwc_context_t* ctx)
{
    private_handle_t *hnd = (private_handle_t*) ctx->previousOverlayHandle;
    if (hnd) {
        // Validate the handle before attempting to use it.
        if (!private_handle_t::validate(hnd) && isBufferLocked(hnd)) {
            if (GENLOCK_NO_ERROR == genlock_unlock_buffer(hnd)) {
                //If previous is same as current, keep locked.
                if(hnd != ctx->currentOverlayHandle) {
                    hnd->flags &= ~private_handle_t::PRIV_FLAGS_HWC_LOCK;
                }
            } else {
                LOGE("%s: genlock_unlock_buffer failed", __FUNCTION__);
            }
        }
    }
    ctx->previousOverlayHandle = ctx->currentOverlayHandle;
    ctx->currentOverlayHandle = NULL;
}

bool canSkipComposition(hwc_context_t* ctx, int yuvBufferCount, int currentLayerCount,
                        int numLayersNotUpdating)
{
    if (!ctx) {
        LOGE("%s: invalid context",__FUNCTION__);
        return false;
    }

    if(ctx->forceComposition) {
        return false;
    }

    hwc_composer_device_t* dev = (hwc_composer_device_t *)(ctx);
    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);
    if (hwcModule->compositionType == COMPOSITION_TYPE_CPU)
        return false;

    //Video / Camera case
    if (ctx->yuvBufferCount == 1) {
        //If the previousLayerCount is anything other than the current count, it
        //means something changed and we need to compose atleast once to FB.
        if (currentLayerCount != ctx->previousLayerCount) {
            ctx->previousLayerCount = currentLayerCount;
            return false;
        }
        // We either have only one overlay layer or we have
        // all non-updating UI layers.
        // We can skip the composition of the UI layers.
        if ((currentLayerCount == 1) ||
            ((currentLayerCount - 1) == numLayersNotUpdating)) {
            return true;
        }
    } else {
        ctx->previousLayerCount = -1;
    }
    return false;
}

static bool canUseCopybit(const framebuffer_device_t* fbDev, const hwc_layer_list_t* list, const int numYUVBuffers) {

    if(!fbDev) {
       LOGE("ERROR: %s : fb device is invalid",__func__);
       return false;
    }

    if (!list)
        return false;

#ifdef USE_MDP3
    if (numYUVBuffers)
        return true;
#endif

    int fb_w = fbDev->width;
    int fb_h = fbDev->height;

    /*
     * Use copybit only when we need to blit
     * max 2 full screen sized regions
     */

    unsigned int renderArea = 0;

    for(int i = 0; i < list->numHwLayers; i++ ) {
        int w, h;
        getLayerResolution(&list->hwLayers[i], w, h);
        renderArea += w*h;
    }

    return (renderArea <= (2 * fb_w * fb_h));
}

static void handleHDMIStateChange(hwc_composer_device_t *dev, int externaltype) {
#if defined HDMI_DUAL_DISPLAY
    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);
    LOGE_IF(DEBUG_HWC, "%s: externaltype=%d", __FUNCTION__, externaltype);

    //Route the event to fbdev only if we are in default mirror mode
    if(ExtDispOnly::isModeOn() == false) {
        framebuffer_device_t *fbDev = hwcModule->fbDevice;
        if (fbDev) {
            fbDev->perform(fbDev, EVENT_EXTERNAL_DISPLAY, (void*)&externaltype);
        }
        hwc_context_t* ctx = (hwc_context_t*)(dev);
        if(ctx && ctx->mOverlayLibObject) {
            overlay::Overlay *ovLibObject = ctx->mOverlayLibObject;
            if (!externaltype) {
                // Close the external overlay channels if HDMI is disconnected
                ovLibObject->closeExternalChannel();
            }
        }
    }
#endif
}

/*
 * Save callback functions registered to HWC
 */
static void hwc_registerProcs(struct hwc_composer_device* dev, hwc_procs_t const* procs) {
    hwc_context_t* ctx = (hwc_context_t*)(dev);

    if(!ctx) {
        LOGE("%s: Invalid context", __FUNCTION__);
        return;
    }

    ctx->device.reserved_proc[0] = (void*)procs;
}


/*
 * Returns the framebuffer index associated with the external display device
 *
 */
static inline int getExtDeviceFBIndex(int index)
{
   return extDeviceFbIndex[index];
}

/*
 * Updates extDeviceFbIndex Array with the correct frame buffer indices
 * of avaiable external devices
 *
 */
static void updateExtDispDevFbIndex()
{
    FILE *displayDeviceFP = NULL;
    char fbType[MAX_FRAME_BUFFER_NAME_SIZE];
    char msmFbTypePath[MAX_FRAME_BUFFER_NAME_SIZE];
    for(int i = 0, j = 1; j < MAX_DISPLAY_DEVICES; j++) {
        sprintf (msmFbTypePath, "/sys/class/graphics/fb%d/msm_fb_type", j);
        displayDeviceFP = fopen(msmFbTypePath, "r");
        if(displayDeviceFP){
            fread(fbType, sizeof(char), MAX_FRAME_BUFFER_NAME_SIZE,
                  displayDeviceFP);
            if(strncmp(fbType, extFrameBufferName[i],
                       strlen(extFrameBufferName[i])) == 0){
                // this is the framebuffer index that we want to send it further
                extDeviceFbIndex[i++] = j;
            }
            fclose(displayDeviceFP);
        }
    }
}


/*
 * function to set the status of external display in hwc
 * Just mark flags and do stuff after eglSwapBuffers
 * externaltype - can be HDMI, WIFI or OFF
 */
static void hwc_enableHDMIOutput(hwc_composer_device_t *dev, int externaltype) {
#if defined HDMI_DUAL_DISPLAY
    LOGE_IF(DEBUG_HWC, "%s: externaltype=%d", __FUNCTION__, externaltype);
    hwc_context_t* ctx = (hwc_context_t*)(dev);

    if(externaltype)
        externaltype = getExtDeviceFBIndex(externaltype-1);

    if(externaltype && ctx->mHDMIEnabled &&
            (externaltype != ctx->mHDMIEnabled)) {
        // Close the current external display - as the SF will
        // prioritize and send the correct external display HDMI/WFD
        handleHDMIStateChange(dev, 0);
    }
    // Store the external display
    ctx->mHDMIEnabled = (external_display_type)externaltype;
    if(ctx->mHDMIEnabled) { //On connect, allow bypass to draw once to FB
        ctx->pendingHDMI = true;
    } else { //On disconnect, close immediately (there will be no bypass)
        handleHDMIStateChange(dev, ctx->mHDMIEnabled);
    }
#endif
}

/* function to handle the custom events to hwc.
 * event - type of event
 * value - value associated with the event
 */
static void hwc_perform(hwc_composer_device_t *dev, int event, int value) {
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);
    switch(event) {
#if defined HDMI_DUAL_DISPLAY
        case EVENT_EXTERNAL_DISPLAY:
            hwc_enableHDMIOutput(dev, value);
            break;
#endif
        case EVENT_FORCE_COMPOSITION:
            ctx->forceComposition = value;
            break;

        default:
            LOGE("In hwc:perform UNKNOWN EVENT = %d!!", event);
            break;
    }
    return;
}


static bool isS3DCompositionRequired (hwc_context_t *ctx) {
    if (ctx->isPrimary) {
       return overlay::is3DTV();
    }
    return false;
}

static void markUILayerForS3DComposition (hwc_layer_t &layer, hwc_context_t *ctx) {
    if (ctx->isPrimary) {
        layer.compositionType = HWC_FRAMEBUFFER;
        switch(ctx->s3dLayerFormat) {
            case HAL_3D_IN_SIDE_BY_SIDE_L_R:
            case HAL_3D_IN_SIDE_BY_SIDE_R_L:
                layer.hints |= HWC_HINT_DRAW_S3D_SIDE_BY_SIDE;
                break;
            case HAL_3D_IN_TOP_BOTTOM:
                layer.hints |= HWC_HINT_DRAW_S3D_TOP_BOTTOM;
                break;
            default:
                LOGE("%s: Unknown S3D input format 0x%x", __FUNCTION__, ctx->s3dLayerFormat);
                break;
        }
    }
    return;
}

/*
 * This function loops thru the list of hwc layers and caches the
 * layer details - such as yuvBuffer count, secure layer count etc.,(can
 * add more in future)
 * */
static void statCount(hwc_context_t *ctx, hwc_layer_list_t* list) {
    int yuvBufCount = 0;
    int layersNotUpdatingCount = 0;
    int s3dLayerFormat = 0;
    ctx->premultipliedAlpha = false;
    if (list) {
        for (size_t i=0 ; i<list->numHwLayers; i++) {
            private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;
            if (hnd) {
                if(hnd->bufferType == BUFFER_TYPE_VIDEO) {
                    yuvBufCount++;
                } else if (list->hwLayers[i].flags & HWC_LAYER_NOT_UPDATING)
                        layersNotUpdatingCount++;
                s3dLayerFormat = s3dLayerFormat ? s3dLayerFormat : FORMAT_3D_INPUT(hnd->format);
            }
            if(list->hwLayers[i].blending == HWC_BLENDING_PREMULT)
                ctx->premultipliedAlpha = true;
        }
    }
    // Number of video/camera layers drawable with overlay
    ctx->yuvBufferCount = yuvBufCount;
    // S3D layer count
    ctx->s3dLayerFormat = s3dLayerFormat;
    // number of non-updating layers
    ctx->numLayersNotUpdating = layersNotUpdatingCount;
    return;
 }

static int hwc_prepare(hwc_composer_device_t *dev, hwc_layer_list_t* list) {
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    ctx->currentOverlayHandle = NULL;

    if(!ctx) {
        LOGE("hwc_prepare invalid context");
        return -1;
    }

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);
    if (!hwcModule) {
        LOGE("hwc_prepare invalid module");
        Bypass::unlock_prev_frame_buffers();
        unlockPreviousOverlayBuffer(ctx);
        ExtDispOnly::close();
        return -1;
    }

    int  layerType = 0;
    bool isS3DCompositionNeeded = false;
    bool useCopybit = false;
    bool isSkipLayerPresent = false;
    bool skipComposition = false;

    if (list) {
        useCopybit = canUseCopybit(hwcModule->fbDevice, list, ctx->yuvBufferCount);
        // cache the number of layer(like YUV, SecureBuffer, notupdating etc.,)
        statCount(ctx, list);
        skipComposition = canSkipComposition(ctx, ctx->yuvBufferCount,
                                list->numHwLayers, ctx->numLayersNotUpdating);

        /* If video is ending, unlock the previously locked buffer
         * and close the overlay channels if opened
         */
        if (ctx->yuvBufferCount == 0) {
            if (ctx->hwcOverlayStatus == HWC_OVERLAY_OPEN)
                ctx->hwcOverlayStatus = HWC_OVERLAY_PREPARE_TO_CLOSE;
        }

        /* If s3d layer is present, we may need to convert other layers to S3D
         * Check if we need the S3D compostion for other layers
         */
        if (ctx->s3dLayerFormat)
            isS3DCompositionNeeded = isS3DCompositionRequired(ctx);

        for (size_t i=0 ; i<list->numHwLayers ; i++) {
            private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;

            // If there is a single Fullscreen layer, we can bypass it - TBD
            // If there is only one video/camera buffer, we can bypass itn
            if (isSkipLayer(&list->hwLayers[i])) {
                isSkipLayerPresent = true;
                skipComposition = false;
                //Reset count, so that we end up composing once after animation
                //is over, in case of overlay.
                ctx->previousLayerCount = -1;

                //If YUV layer is marked as SKIP, close pipes.
                if(isYuvBuffer(hnd)) {
                    if (ctx->hwcOverlayStatus == HWC_OVERLAY_OPEN)
                        ctx->hwcOverlayStatus = HWC_OVERLAY_PREPARE_TO_CLOSE;
                }
                // During the animaton UI layers are marked as SKIP
                // need to still mark the layer for S3D composition
                if (isS3DCompositionNeeded)
                    markUILayerForS3DComposition(list->hwLayers[i], ctx);

                list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
                list->hwLayers[i].hints &= ~HWC_HINT_CLEAR_FB;
                markForGPUComp(ctx, list, i);
            } else if (hnd && (hnd->bufferType == BUFFER_TYPE_VIDEO) && (ctx->yuvBufferCount == 1)) {
                int flags = skipComposition ? WAIT_FOR_VSYNC : 0;
                flags |= isSecureBuffer(hnd) ? SECURE_OVERLAY_SESSION : 0;
                flags |= (1 == list->numHwLayers) ? DISABLE_FRAMEBUFFER_FETCH : 0;
                int videoStarted = (ctx->s3dLayerFormat && overlay::is3DTV()) ?
                            VIDEO_3D_OVERLAY_STARTED : VIDEO_2D_OVERLAY_STARTED;
                setVideoOverlayStatusInGralloc(ctx, videoStarted);
                correct_crop_rects(hwcModule->fbDevice,
                                        list->hwLayers[i].sourceCrop,
                                        list->hwLayers[i].displayFrame,
                                        list->hwLayers[i].transform);
#ifdef USE_OVERLAY
                if(prepareOverlay(ctx, &(list->hwLayers[i]), flags) == 0) {
                    list->hwLayers[i].compositionType = HWC_USE_OVERLAY;
                    list->hwLayers[i].hints |= HWC_HINT_CLEAR_FB;
                    // We've opened the channel. Set the state to open.
                    ctx->hwcOverlayStatus = HWC_OVERLAY_OPEN;
#else
                if (hwcModule->compositionType & COMPOSITION_TYPE_DYN) {
                    //dynamic composition for non-overlay targets(8x25/7x27a)
                    list->hwLayers[i].compositionType = HWC_USE_COPYBIT;
#endif
                } else if (hwcModule->compositionType & (COMPOSITION_TYPE_C2D |
                                            COMPOSITION_TYPE_MDP)) {
                    //Fail safe path: If drawing with overlay fails,

                    //Use C2D if available.
                    list->hwLayers[i].compositionType = HWC_USE_COPYBIT;
                } else {
                    //If C2D is not enabled fall back to GPU.
                    list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
                }
                if (HWC_USE_OVERLAY != list->hwLayers[i].compositionType) {
                    skipComposition = false;
                }
            } else if (getLayerS3DFormat(list->hwLayers[i])) {
                int flags = skipComposition ? WAIT_FOR_VSYNC : 0;
                flags |= (1 == list->numHwLayers) ? DISABLE_FRAMEBUFFER_FETCH : 0;
                flags |= isSecureBuffer(hnd) ? SECURE_OVERLAY_SESSION : 0;

                int videoStarted = overlay::is3DTV() ? VIDEO_3D_OVERLAY_STARTED
                                                    : VIDEO_2D_OVERLAY_STARTED;
                setVideoOverlayStatusInGralloc(ctx, videoStarted);
#ifdef USE_OVERLAY
                if(prepareOverlay(ctx, &(list->hwLayers[i]), flags) == 0) {
                    list->hwLayers[i].compositionType = HWC_USE_OVERLAY;
                    list->hwLayers[i].hints |= HWC_HINT_CLEAR_FB;
                    // We've opened the channel. Set the state to open.
                    ctx->hwcOverlayStatus = HWC_OVERLAY_OPEN;
                }
#endif
            } else if (isS3DCompositionNeeded) {
                markUILayerForS3DComposition(list->hwLayers[i], ctx);
            } else if (hnd && hnd->flags & private_handle_t::PRIV_FLAGS_EXTERNAL_ONLY) {
                //handle later after other layers are handled
            } else if (hnd && (hwcModule->compositionType &
                    (COMPOSITION_TYPE_C2D|COMPOSITION_TYPE_MDP)) &&
                    (!(hwcModule->compositionType & COMPOSITION_TYPE_DYN))) {
                list->hwLayers[i].compositionType = HWC_USE_COPYBIT;
            } else if ((hwcModule->compositionType & COMPOSITION_TYPE_DYN)
                    && useCopybit) {
                list->hwLayers[i].compositionType = HWC_USE_COPYBIT;
            }
            else {
                list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
            }
        }

        //Update the stats and pipe config for external-only layers
        ExtDispOnly::update(ctx, list);

        if (skipComposition) {
            list->flags |= HWC_SKIP_COMPOSITION;
        } else {
            list->flags &= ~HWC_SKIP_COMPOSITION;
        }

        Bypass::configure(dev, list, ctx->yuvBufferCount);
    } else {
        Bypass::unlock_prev_frame_buffers();
        unlockPreviousOverlayBuffer(ctx);
    }
    ctx->forceComposition = false;
    return 0;
}
// ---------------------------------------------------------------------------
struct range {
    int current;
    int end;
};
struct region_iterator : public copybit_region_t {
    
    region_iterator(hwc_region_t region) {
        mRegion = region;
        r.end = region.numRects;
        r.current = 0;
        this->next = iterate;
    }

private:
    static int iterate(copybit_region_t const * self, copybit_rect_t* rect) {
        if (!self || !rect) {
            LOGE("iterate invalid parameters");
            return 0;
        }

        region_iterator const* me = static_cast<region_iterator const*>(self);
        if (me->r.current != me->r.end) {
            rect->l = me->mRegion.rects[me->r.current].left;
            rect->t = me->mRegion.rects[me->r.current].top;
            rect->r = me->mRegion.rects[me->r.current].right;
            rect->b = me->mRegion.rects[me->r.current].bottom;
            me->r.current++;
            return 1;
        }
        return 0;
    }
    
    hwc_region_t mRegion;
    mutable range r; 
};

static int drawLayerUsingCopybit(hwc_composer_device_t *dev, hwc_layer_t *layer, EGLDisplay dpy,
                                 EGLSurface surface)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(!ctx) {
         LOGE("%s: null context ", __FUNCTION__);
         return -1;
    }

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(dev->common.module);
    if(!hwcModule) {
        LOGE("%s: null module ", __FUNCTION__);
        return -1;
    }

    private_handle_t *hnd = (private_handle_t *)layer->handle;
    if(!hnd) {
        LOGE("%s: invalid handle", __FUNCTION__);
        return -1;
    }

    // Lock this buffer for read.
    genlock_lock_type lockType = GENLOCK_READ_LOCK;
    int err = genlock_lock_buffer(hnd, lockType, GENLOCK_MAX_TIMEOUT);
    if (GENLOCK_FAILURE == err) {
        LOGE("%s: genlock_lock_buffer(READ) failed", __FUNCTION__);
        return -1;
    }
    //render buffer
    android_native_buffer_t *renderBuffer = (android_native_buffer_t *)eglGetRenderBufferANDROID(dpy, surface);
    if (!renderBuffer) {
        LOGE("%s: eglGetRenderBufferANDROID returned NULL buffer", __FUNCTION__);
        genlock_unlock_buffer(hnd);
        return -1;
    }
    private_handle_t *fbHandle = (private_handle_t *)renderBuffer->handle;
    if(!fbHandle) {
        LOGE("%s: Framebuffer handle is NULL", __FUNCTION__);
        genlock_unlock_buffer(hnd);
        return -1;
    }

    // Set the copybit source:
    copybit_image_t src;
    src.w = hnd->width;
    src.h = hnd->height;
    src.format = hnd->format;
    src.base = (void *)hnd->base;
    src.handle = (native_handle_t *)layer->handle;
    src.horiz_padding = src.w - hnd->width;
    // Initialize vertical padding to zero for now,
    // this needs to change to accomodate vertical stride
    // if needed in the future
    src.vert_padding = 0;
    // Remove the srcBufferTransform if any
    layer->transform = (layer->transform & FINAL_TRANSFORM_MASK);

    // Copybit source rect
    hwc_rect_t sourceCrop = layer->sourceCrop;
    copybit_rect_t srcRect = {sourceCrop.left, sourceCrop.top,
                              sourceCrop.right,
                              sourceCrop.bottom};

    // Copybit destination rect
    hwc_rect_t displayFrame = layer->displayFrame;
    copybit_rect_t dstRect = {displayFrame.left, displayFrame.top,
                              displayFrame.right,
                              displayFrame.bottom};

    // Copybit dst
    copybit_image_t dst;
    dst.w = ALIGN(fbHandle->width,32);
    dst.h = fbHandle->height;
    dst.format = fbHandle->format;
    dst.base = (void *)fbHandle->base;
    dst.handle = (native_handle_t *)renderBuffer->handle;

    copybit_device_t *copybit = hwcModule->copybitEngine;

    int32_t screen_w        = displayFrame.right - displayFrame.left;
    int32_t screen_h        = displayFrame.bottom - displayFrame.top;
    int32_t src_crop_width  = sourceCrop.right - sourceCrop.left;
    int32_t src_crop_height = sourceCrop.bottom -sourceCrop.top;

    float copybitsMaxScale = (float)copybit->get(copybit,COPYBIT_MAGNIFICATION_LIMIT);
    float copybitsMinScale = (float)copybit->get(copybit,COPYBIT_MINIFICATION_LIMIT);

    if((layer->transform == HWC_TRANSFORM_ROT_90) ||
                           (layer->transform == HWC_TRANSFORM_ROT_270)) {
        //swap screen width and height
        int tmp = screen_w;
        screen_w  = screen_h;
        screen_h = tmp;
    }
    private_handle_t *tmpHnd = NULL;

    if(screen_w <=0 || screen_h<=0 ||src_crop_width<=0 || src_crop_height<=0 ) {
        LOGE("%s: wrong params for display screen_w=%d src_crop_width=%d screen_w=%d \
                                src_crop_width=%d", __FUNCTION__, screen_w,
                                src_crop_width,screen_w,src_crop_width);
        genlock_unlock_buffer(hnd);
        return -1;
    }

    float dsdx = (float)screen_w/src_crop_width;
    float dtdy = (float)screen_h/src_crop_height;

    float scaleLimitMax = copybitsMaxScale * copybitsMaxScale;
    float scaleLimitMin = copybitsMinScale * copybitsMinScale;
    if(dsdx > scaleLimitMax || dtdy > scaleLimitMax || dsdx < 1/scaleLimitMin || dtdy < 1/scaleLimitMin) {
        LOGE("%s: greater than max supported size dsdx=%f dtdy=%f scaleLimitMax=%f scaleLimitMin=%f", __FUNCTION__,dsdx,dtdy,scaleLimitMax,1/scaleLimitMin);
        genlock_unlock_buffer(hnd);
        return -1;
    }
    if(dsdx > copybitsMaxScale || dtdy > copybitsMaxScale || dsdx < 1/copybitsMinScale || dtdy < 1/copybitsMinScale){
        // The requested scale is out of the range the hardware
        // can support.
       LOGD("%s:%d::Need to scale twice dsdx=%f, dtdy=%f,copybitsMaxScale=%f,copybitsMinScale=%f,screen_w=%d,screen_h=%d \
                  src_crop_width=%d src_crop_height=%d",__FUNCTION__,__LINE__,
                  dsdx,dtdy,copybitsMaxScale,1/copybitsMinScale,screen_w,screen_h,src_crop_width,src_crop_height);

       //Driver makes width and height as even
       //that may cause wrong calculation of the ratio
       //in display and crop.Hence we make
       //crop width and height as even.
       src_crop_width  = (src_crop_width/2)*2;
       src_crop_height = (src_crop_height/2)*2;

       int tmp_w =  src_crop_width;
       int tmp_h =  src_crop_height;

       if (dsdx > copybitsMaxScale || dtdy > copybitsMaxScale ){
         tmp_w = src_crop_width*copybitsMaxScale;
         tmp_h = src_crop_height*copybitsMaxScale;
       }else if (dsdx < 1/copybitsMinScale ||dtdy < 1/copybitsMinScale ){
         tmp_w = src_crop_width/copybitsMinScale;
         tmp_h = src_crop_height/copybitsMinScale;
         tmp_w  = (tmp_w/2)*2;
         tmp_h = (tmp_h/2)*2;
       }
       LOGD("%s:%d::tmp_w = %d,tmp_h = %d",__FUNCTION__,__LINE__,tmp_w,tmp_h);

       int usage = GRALLOC_USAGE_PRIVATE_ADSP_HEAP |
                   GRALLOC_USAGE_PRIVATE_MM_HEAP;

       if (0 == alloc_buffer(&tmpHnd, tmp_w, tmp_h, fbHandle->format, usage)){
            copybit_image_t tmp_dst;
            copybit_rect_t tmp_rect;
            tmp_dst.w = tmp_w;
            tmp_dst.h = tmp_h;
            tmp_dst.format = tmpHnd->format;
            tmp_dst.handle = tmpHnd;
            tmp_dst.horiz_padding = src.horiz_padding;
            tmp_dst.vert_padding = src.vert_padding;
            tmp_rect.l = 0;
            tmp_rect.t = 0;
            tmp_rect.r = tmp_dst.w;
            tmp_rect.b = tmp_dst.h;
            //create one clip region
            hwc_rect tmp_hwc_rect = {0,0,tmp_rect.r,tmp_rect.b};
            hwc_region_t tmp_hwc_reg = {1,(hwc_rect_t const*)&tmp_hwc_rect};
            region_iterator tmp_it(tmp_hwc_reg);
            copybit->set_parameter(copybit,COPYBIT_TRANSFORM,0);
            copybit->set_parameter(copybit, COPYBIT_PLANE_ALPHA,
                        (layer->blending == HWC_BLENDING_NONE) ? -1 : layer->alpha);
            err = copybit->stretch(copybit,&tmp_dst, &src, &tmp_rect, &srcRect, &tmp_it);
            if(err < 0){
                LOGE("%s:%d::tmp copybit stretch failed",__FUNCTION__,__LINE__);
                if(tmpHnd)
                    free_buffer(tmpHnd);
                genlock_unlock_buffer(hnd);
                return err;
            }
            // copy new src and src rect crop
            src = tmp_dst;
            srcRect = tmp_rect;
      }
    }
    // Copybit region
    hwc_region_t region = layer->visibleRegionScreen;
    region_iterator copybitRegion(region);

    copybit->set_parameter(copybit, COPYBIT_FRAMEBUFFER_WIDTH, renderBuffer->width);
    copybit->set_parameter(copybit, COPYBIT_FRAMEBUFFER_HEIGHT, renderBuffer->height);
    copybit->set_parameter(copybit, COPYBIT_TRANSFORM, layer->transform);
    copybit->set_parameter(copybit, COPYBIT_PLANE_ALPHA,
                           (layer->blending == HWC_BLENDING_NONE) ? -1 : layer->alpha);
    copybit->set_parameter(copybit, COPYBIT_PREMULTIPLIED_ALPHA,
                           (layer->blending == HWC_BLENDING_PREMULT)? COPYBIT_ENABLE : COPYBIT_DISABLE);
    copybit->set_parameter(copybit, COPYBIT_DITHER,
                            (dst.format == HAL_PIXEL_FORMAT_RGB_565)? COPYBIT_ENABLE : COPYBIT_DISABLE);
    copybit->set_parameter(copybit, COPYBIT_BLIT_TO_FRAMEBUFFER, COPYBIT_ENABLE);
    err = copybit->stretch(copybit, &dst, &src, &dstRect, &srcRect, &copybitRegion);
    copybit->set_parameter(copybit, COPYBIT_BLIT_TO_FRAMEBUFFER, COPYBIT_DISABLE);

    if(tmpHnd)
        free_buffer(tmpHnd);

    if(err < 0)
        LOGE("%s: copybit stretch failed",__FUNCTION__);

    // Unlock this buffer since copybit is done with it.
    err = genlock_unlock_buffer(hnd);
    if (GENLOCK_FAILURE == err) {
        LOGE("%s: genlock_unlock_buffer failed", __FUNCTION__);
    }

    return err;
}

static int drawLayerUsingOverlay(hwc_context_t *ctx, hwc_layer_t *layer)
{
    if (ctx && ctx->mOverlayLibObject) {
        private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(ctx->device.common.module);
        if (!hwcModule) {
            LOGE("%s: null module", __FUNCTION__);
            return -1;
        }

        private_handle_t *hnd = (private_handle_t *)layer->handle;

        // Lock this buffer for read.
        if (GENLOCK_NO_ERROR != genlock_lock_buffer(hnd, GENLOCK_READ_LOCK,
                                                    GENLOCK_MAX_TIMEOUT)) {
            LOGE("%s: genlock_lock_buffer(READ) failed", __FUNCTION__);
            return -1;
        }

        bool ret = true;

        overlay::Overlay *ovLibObject = ctx->mOverlayLibObject;
        ret = ovLibObject->queueBuffer(hnd);

        if (!ret) {
            LOGE("%s: failed", __FUNCTION__);
            // Unlock the buffer handle
            genlock_unlock_buffer(hnd);
        } else {
            // Store the current buffer handle as the one that is to be unlocked after
            // the next overlay play call.
            hnd->flags |= private_handle_t::PRIV_FLAGS_HWC_LOCK;
            ctx->currentOverlayHandle = hnd;
        }

        // Since ret is a bool and return value is an int
        return !ret;
    }
    return -1;
}

static int hwc_set(hwc_composer_device_t *dev,
        hwc_display_t dpy,
        hwc_surface_t sur,
        hwc_layer_list_t* list)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(!ctx) {
        LOGE("hwc_set invalid context");
        ExtDispOnly::close();
        return -1;
    }

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);

    if (!hwcModule) {
        LOGE("hwc_set invalid module");
        Bypass::unlock_prev_frame_buffers();
        ExtDispOnly::close();
        unlockPreviousOverlayBuffer(ctx);
        return -1;
    }

    framebuffer_device_t *fbDev = hwcModule->fbDevice;

    Bypass::closeExtraPipes();

    int ret = 0;
    if (list) {
        bool bDumpLayers = needToDumpLayers(); // Check need for debugging dumps
        for (size_t i=0; i<list->numHwLayers; i++) {
            if (bDumpLayers)
                dumpLayer(hwcModule->compositionType, list->flags, i, list->hwLayers);
            if (list->hwLayers[i].flags & HWC_SKIP_LAYER) {
                continue;
            } else if(list->hwLayers[i].flags & HWC_USE_EXT_ONLY) {
                continue;
            //Draw after layers for primary are drawn
            } else if (list->hwLayers[i].flags & HWC_COMP_BYPASS) {
                Bypass::drawLayerUsingBypass(ctx, &(list->hwLayers[i]), i);
            } else if (list->hwLayers[i].compositionType == HWC_USE_OVERLAY) {
                drawLayerUsingOverlay(ctx, &(list->hwLayers[i]));
            } else if (list->flags & HWC_SKIP_COMPOSITION) {
                continue;
            } else if (list->hwLayers[i].compositionType == HWC_USE_COPYBIT) {
                drawLayerUsingCopybit(dev, &(list->hwLayers[i]), (EGLDisplay)dpy, (EGLSurface)sur);
            }
        }
    } else {
            if (ctx->hwcOverlayStatus == HWC_OVERLAY_OPEN)
                ctx->hwcOverlayStatus =  HWC_OVERLAY_PREPARE_TO_CLOSE;
            Bypass::close_pipes(ctx, true, false);
    }

    //Draw External-only layers
    if(ExtDispOnly::draw(ctx, list) != overlay::NO_ERROR) {
        ExtDispOnly::close();
    }

    bool canSkipComposition = list && list->flags & HWC_SKIP_COMPOSITION;

    // Do not call eglSwapBuffers if we the skip composition flag is set on the list.
    if (dpy && sur && !canSkipComposition) {
        //Wait for closing pipes and unlocking buffers until FB is done posting
        //buffers, only if MDP pipes are in use. (Video, Comp.Bypass)
        //For future releases we might wait even for UI updates. TBD.
        bool waitForFBPost = false;

        if((ctx->hwcOverlayStatus != HWC_OVERLAY_CLOSED) || 
                      (Bypass::get_state() == Bypass::BYPASS_OFF_PENDING))
            waitForFBPost = true;

        //Reset FB post status before doing eglSwap
        if(waitForFBPost)
            fbDev->perform(fbDev, EVENT_RESET_POSTBUFFER, NULL);

        EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
        if (!sucess) {
            ret = HWC_EGL_ERROR;
        } else {
            //If swap succeeds, wait till FB posts buffer for display.
            if(waitForFBPost)
              fbDev->perform(fbDev, EVENT_WAIT_POSTBUFFER, NULL);

            if(Bypass::get_state() == Bypass::BYPASS_OFF_PENDING)
                Bypass::set_state(Bypass::BYPASS_OFF);
        }
    } else {
        CALC_FPS();
    }

    Bypass::unlock_prev_frame_buffers();

#if defined HDMI_DUAL_DISPLAY
    if(ctx->pendingHDMI) {
        handleHDMIStateChange(dev, ctx->mHDMIEnabled);
        ctx->pendingHDMI = false;
        hwc_procs* proc = (hwc_procs*)ctx->device.reserved_proc[0];
        if(!proc) {
                LOGE("%s: HWC proc not registered", __FUNCTION__);
        } else {
            /* Trigger SF to redraw the current frame
             * Used when the video is paused and external
             * display is connected
             */
            ctx->forceComposition = true;
            proc->invalidate(proc);
        }
    }
#endif

    hwc_closeOverlayChannels(ctx);

    // Unlock the previously locked vdeo buffer, since the overlay has completed
    // reading the buffer. Should be done only after closing channels, if
    // applicable.
    unlockPreviousOverlayBuffer(ctx);

    return ret;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    if(!dev) {
        LOGE("hwc_device_close null device pointer");
        return -1;
    }

    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
            ctx->device.common.module);
    // Close the overlay and copybit modules
    if(hwcModule->copybitEngine) {
        copybit_close(hwcModule->copybitEngine);
        hwcModule->copybitEngine = NULL;
    }
    if(hwcModule->fbDevice) {
        framebuffer_close(hwcModule->fbDevice);
        hwcModule->fbDevice = NULL;
    }

    unlockPreviousOverlayBuffer(ctx);

    if (ctx) {
         delete ctx->mOverlayLibObject;
         ctx->mOverlayLibObject = NULL;
         Bypass::deinit();
         ExtDispOnly::close();
         ExtDispOnly::destroy();
         free(ctx);
    }
    return 0;
}

/*****************************************************************************/
static int hwc_module_initialize(struct private_hwc_module_t* hwcModule)
{

    // Open the overlay and copybit modules
    hw_module_t const *module;
    if (hw_get_module(COPYBIT_HARDWARE_MODULE_ID, &module) == 0) {
        copybit_open(module, &(hwcModule->copybitEngine));
    }
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module) == 0) {
        framebuffer_open(module, &(hwcModule->fbDevice));
    }

    // get the current composition type
    hwcModule->compositionType = QCCompositionType::getInstance().getCompositionType();

    //Check if composition bypass is enabled
    char property[PROPERTY_VALUE_MAX];
    if(property_get("debug.compbypass.enable", property, NULL) > 0) {
        if(atoi(property) == 1) {
            hwcModule->isBypassEnabled = true;
        }
    }

    CALC_INIT();

    return 0;
}


static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    int status = -EINVAL;

    if (!strcmp(name, HWC_HARDWARE_COMPOSER)) {
        private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>
                                        (const_cast<hw_module_t*>(module));
        hwc_module_initialize(hwcModule);
        struct hwc_context_t *dev;
        dev = (hwc_context_t*)malloc(sizeof(*dev));

        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));
#ifdef USE_OVERLAY
        dev->mOverlayLibObject = new overlay::Overlay();
        if(overlay::initOverlay() == -1)
            LOGE("overlay::initOverlay() ERROR!!");
        // check if HDMI is primary or not
        dev->isPrimary = overlay::isHDMIPrimary();
        if (dev->isPrimary) {
           LOGD("%s : HDMI is primary display", __FUNCTION__);
        }
#else
        dev->mOverlayLibObject = NULL;
#endif
        ExtDispOnly::init();
#if defined HDMI_DUAL_DISPLAY
        dev->mHDMIEnabled = EXT_TYPE_NONE;
        dev->pendingHDMI = false;
#endif
        dev->previousOverlayHandle = NULL;
        dev->currentOverlayHandle = NULL;
        dev->hwcOverlayStatus = HWC_OVERLAY_CLOSED;
        dev->previousLayerCount = -1;
        char value[PROPERTY_VALUE_MAX];
        if (property_get("debug.egl.swapinterval", value, "1") > 0) {
            dev->swapInterval = atoi(value);
        }
        dev->premultipliedAlpha = false;
        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = hwc_device_close;

        dev->device.prepare = hwc_prepare;
        dev->device.set = hwc_set;
        dev->device.registerProcs = hwc_registerProcs;
        dev->device.perform = hwc_perform;
        *device = &dev->device.common;

        Bypass::initialize(dev);

        status = 0;
        /* Store framebuffer indices of avaiable external devices*/
        updateExtDispDevFbIndex();
    }
    return status;
}
