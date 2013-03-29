/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, The Linux Foundation. All rights reserved.
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

#ifndef HWC_UTILS_H
#define HWC_UTILS_H

#include <hardware/hwcomposer.h>
#include <gralloc_priv.h>
#include <hwc_ppmetadata.h>
#ifdef USES_PLL_CALCULATION
#include "pll_calc.h"
#endif

#define ALIGN_TO(x, align)     (((x) + ((align)-1)) & ~((align)-1))
#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))
#define PP_MAX_VG_PIPES 2

#define LVDS_PLL_UPDATE "/sys/devices/virtual/graphics/fb0/lvds_pll_update"
#define ref_pixclock 27000000
#define NUM_HDMI_PRIMARY_PANEL_NAMES 2
#define HDMI_PANEL "dtv panel"
#define LVDS_TV_PANEL "lvds panel"

//Fwrd decls
struct hwc_context_t;
struct framebuffer_device_t;

namespace hwcService {
class HWComposerService;
}

namespace overlay {
class Overlay;
}

enum {
    MAX_FRAME_BUFFER_NAME_SIZE = 80
};

static const char *HDMIPrimaryPanelName[NUM_HDMI_PRIMARY_PANEL_NAMES] = {
    HDMI_PANEL,
    LVDS_TV_PANEL
};

namespace qhwc {
//fwrd decl
class QueuedBufferStore;
class ExternalDisplay;
class CopybitEngine;

struct MDPInfo {
    int version;
    char panel;
    bool hasOverlay;
};

enum external_display_type {
    EXT_TYPE_NONE,
    EXT_TYPE_HDMI,
    EXT_TYPE_WIFI
};
enum HWCCompositionType {
    HWC_USE_GPU = HWC_FRAMEBUFFER, // This layer is to be handled by
                                   // Surfaceflinger
    HWC_USE_OVERLAY = HWC_OVERLAY, // This layer is to be handled by the overlay
    HWC_USE_BACKGROUND
                = HWC_BACKGROUND,  // This layer is to be handled by TBD
    HWC_USE_COPYBIT                // This layer is to be handled by copybit
};

enum {
    HWC_MDPCOMP = 0x00000002,
    HWC_LAYER_RESERVED_0 = 0x00000004,
    HWC_LAYER_RESERVED_1 = 0x00000008
};

/* PictureQualityControlStatus */
enum PQCStatus {
    PQC_START = 0,
    PQC_STOP = 1,
    PQC_CLOSEPIPES = 2,
    PQC_INPROGRESS = 3
};

// Utility functions - implemented in hwc_utils.cpp
void dumpLayer(hwc_layer_t const* l);
void getLayerStats(hwc_context_t *ctx, const hwc_layer_list_t *list);
void initContext(hwc_context_t *ctx);
void closeContext(hwc_context_t *ctx);

//Sets the overscan crop and dst values
void set_ov_dimensions(hwc_context_t *ctx, int numVideoLayer,
        hwc_rect& crop, hwc_rect_t& dst);

//Crops source buffer against destination and FB boundaries
void calculate_crop_rects(hwc_rect_t& crop, hwc_rect_t& dst,
        const int fbWidth, const int fbHeight);

// Waits for the fb_post to be called
void wait4fbPost(hwc_context_t* ctx);

// Waits for the fb_post to finish PAN (primary commit)
void wait4Pan(hwc_context_t* ctx);

// Waits for signal from hwc_set to commit on ext display
void wait4CommitSignal(hwc_context_t* ctx);

// Waits for the commit to finish on ext display
void wait4ExtCommitDone(hwc_context_t* ctx);

//commit on primary
int commitOnPrimary(hwc_context_t* ctx);

//Is target HDMIPrimary
static bool isPanelLVDS(int fb){
    bool configured = false;
    FILE *displayDeviceFP = NULL;
    char fbType[MAX_FRAME_BUFFER_NAME_SIZE];
    char name[64];
    char const device_node[64] = "/sys/class/graphics/fb%u/msm_fb_type";
    snprintf(name, 64, device_node,fb);
    displayDeviceFP = fopen(name, "r");

    if(displayDeviceFP) {
        fread(fbType, sizeof(char), MAX_FRAME_BUFFER_NAME_SIZE,
                displayDeviceFP);

        if(!strncmp(fbType, HDMIPrimaryPanelName[1],
                        sizeof(HDMIPrimaryPanelName[1]))) {
            configured  = true;
        }
    }
    fclose(displayDeviceFP);
    return configured;
}

//Is panel pointed by fb LVDS
static bool isHDMIPrimary(){
    bool configured = false;
    FILE *displayDeviceFP = NULL;
    char fbType[MAX_FRAME_BUFFER_NAME_SIZE];
    displayDeviceFP = fopen("/sys/class/graphics/fb0/msm_fb_type", "r");

    if(displayDeviceFP) {
        fread(fbType, sizeof(char), MAX_FRAME_BUFFER_NAME_SIZE,
                displayDeviceFP);

        for(int i = 0; i < NUM_HDMI_PRIMARY_PANEL_NAMES; i++) {
            if(!strncmp(fbType, HDMIPrimaryPanelName[i],
                        sizeof(HDMIPrimaryPanelName[i]))) {
                configured  = true;
                break;
            }
        }
        fclose(displayDeviceFP);
    }
    return configured;
}

static const char* getConfigTypeString(qhwc::CONFIG_CHANGE_TYPE configType) {
    switch(configType) {
        case qhwc::NO_CORRECTION:
            return "NO_CORRECTION";
        case qhwc::PIXEL_CLOCK_CORRECTION:
            return "PIXEL_CLOCK_CORRECTION";
        case qhwc::PICTURE_QUALITY_CORRECTION:
            return "PICTURE_QUALITY_CORRECTION";
        default:
            return "UNKNOWN_CORRECTION_STATE";
    }
}

//Change PLL settings on the external;
bool changePLLSettings(hwc_context_t* ctx);

bool canChangePLLSettings(hwc_context_t* ctx);

bool set_vsync(hwc_context_t* ctx, bool enable);

// Inline utility functions
static inline bool isSkipLayer(const hwc_layer_t* l) {
    return (UNLIKELY(l && (l->flags & HWC_SKIP_LAYER)));
}

// Returns true if the buffer is yuv
static inline bool isYuvBuffer(const private_handle_t* hnd) {
    return (hnd && (hnd->bufferType == BUFFER_TYPE_VIDEO));
}

// Returns true if the buffer is secure
static inline bool isSecureBuffer(const private_handle_t* hnd) {
    return (hnd && (private_handle_t::PRIV_FLAGS_SECURE_BUFFER & hnd->flags));
}
//Return true if buffer is marked locked
static inline bool isBufferLocked(const private_handle_t* hnd) {
    return (hnd && (private_handle_t::PRIV_FLAGS_HWC_LOCK & hnd->flags));
}

//Return true if buffer is for external display only
static inline bool isExtOnly(const private_handle_t* hnd) {
    return (hnd && (hnd->flags & private_handle_t::PRIV_FLAGS_EXTERNAL_ONLY));
}

//Return true if buffer is for external display only with a BLOCK flag.
static inline bool isExtBlock(const private_handle_t* hnd) {
    return (hnd && (hnd->flags & private_handle_t::PRIV_FLAGS_EXTERNAL_BLOCK));
}

//Return true if buffer is for external display only with a Close Caption flag.
static inline bool isExtCC(const private_handle_t* hnd) {
    return (hnd && (hnd->flags & private_handle_t::PRIV_FLAGS_EXTERNAL_CC));
}

static inline bool hasMetaData(const private_handle_t* hnd) {
    if(hnd) {
        MetaData_t *data = (MetaData_t *) hnd->base_metadata;
        return (data && data->operation);
    }
    return false;
}

// Initialize uevent thread
void init_uevent_thread(hwc_context_t* ctx);

// Initialize vsync thread
void init_vsync_thread(hwc_context_t* ctx);

inline void getLayerResolution(const hwc_layer_t* layer,
                                         int& width, int& height)
{
    hwc_rect_t displayFrame  = layer->displayFrame;
    width = displayFrame.right - displayFrame.left;
    height = displayFrame.bottom - displayFrame.top;
}

}; //qhwc namespace

struct vsync_state {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    bool enable;
};

// -----------------------------------------------------------------------------
// HWC context
// This structure contains overall state
struct hwc_context_t {
    hwc_composer_device_t device;
    int numHwLayers;
    int overlayInUse;
    int deviceOrientation;
    int swapInterval;
    double dynThreshold;
    int externalDisplay;
    //Framebuffer device
    framebuffer_device_t *mFbDev;

    //Copybit Engine
    qhwc::CopybitEngine* mCopybitEngine;

    //Overlay object - NULL for non overlay devices
    overlay::Overlay *mOverlay;

    //QueuedBufferStore to hold buffers for overlay
    qhwc::QueuedBufferStore *qbuf;

    //HWComposerService object
    hwcService::HWComposerService *mHwcService;

    // External display related information
    qhwc::ExternalDisplay *mExtDisplay;

    qhwc::MDPInfo mMDP;

    // flag that indicate secure session status
    bool mSecure;

    // flag that indicate whether secure/desecure session in progress
    bool mSecureConfig;

    bool hdmi_pending;
    char  mHDMIEvent[512];

    // Post-processing parameters
    qhwc::VideoPPData mPpParams[PP_MAX_VG_PIPES];

    // To Stop and Start FrameWork Updates on Display
    int mPQCState;

    //OverScanCompensation parameters
    qhwc::OSRectDimensions oscparams;

    //OverScan parameters
    qhwc::OSRectDimensions ossrcparams[PP_MAX_VG_PIPES];
    qhwc::OSRectDimensions osdstparams[PP_MAX_VG_PIPES];

    /* Indicates the configuration change happening
     * either Pixel Clock Correction or Picture Quality
     * Correction */

    qhwc::CONFIG_CHANGE_TYPE mConfigChangeType;

    /* Indicates the state at which the configuration
     * change is in */

    qhwc::ConfigChangeState* mConfigChangeState;

    qhwc::ConfigChangeParams mConfigChangeParams;

    pthread_mutex_t mConfigChangeLock;
    pthread_cond_t mConfigChangeCond;

    // used for signalling the commit Ext Disp thread
    bool mExtCommit;
    pthread_mutex_t mExtCommitLock;
    pthread_cond_t mExtCommitCond;

    // used for signalling the composition thread
    // from the extDispCommit thread
    bool mExtCommitDone;
    pthread_mutex_t mExtCommitDoneLock;
    pthread_cond_t mExtCommitDoneCond;

    //Vsync
    struct vsync_state vstate;

};

#endif //HWC_UTILS_H
