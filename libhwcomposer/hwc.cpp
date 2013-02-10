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

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <EGL/egl.h>

#include <sys/resource.h>
#include <sys/prctl.h>

#include <overlay.h>
#include <fb_priv.h>
#include <mdp_version.h>
#include "hwc_utils.h"
#include "hwc_qbuf.h"
#include "hwc_video.h"
#include "hwc_uimirror.h"
#include "hwc_copybit.h"
#include "hwc_external.h"
#include "hwc_mdpcomp.h"
#include "hwc_extonly.h"
#include "qcom_ui.h"

#define VSYNC_DEBUG 0
using namespace qhwc;

static int hwc_device_open(const struct hw_module_t* module,
                           const char* name,
                           struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        version_major: 2,
        version_minor: 0,
        id: HWC_HARDWARE_MODULE_ID,
        name: "Qualcomm Hardware Composer Module",
        author: "CodeAurora Forum",
        methods: &hwc_module_methods,
        dso: 0,
        reserved: {0},
    }
};

/*
 * Save callback functions registered to HWC
 */
static void hwc_registerProcs(struct hwc_composer_device* dev,
                              hwc_procs_t const* procs)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(!ctx) {
        ALOGE("%s: Invalid context", __FUNCTION__);
        return;
    }
    ctx->device.reserved_proc[0] = (void*)procs;

    init_vsync_thread(ctx);
}

/*
 * commitExtDisp thread function.
 * waits for the signal from the hwc_set and commits
 * to the External display
 */
static void *commitExtDisp(void *ptr)
{
    hwc_context_t* ctx = (hwc_context_t*)(ptr);
    char thread_name[64] = "hwcCommitThr";
    prctl(PR_SET_NAME, (unsigned long) &thread_name, 0, 0, 0);
    setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);
    while (1) {
        //wait for the signal from hwc_set
        wait4CommitSignal(ctx);
        // Commit the external display for update
        if(ctx->externalDisplay)
        {
            ctx->mExtDisplay->commit();
            //If video is playing signal the main thread
            if(VideoOverlay::isModeOn()) {
                pthread_mutex_lock(&ctx->mExtCommitDoneLock);
                ctx->mExtCommitDone = true;
                pthread_cond_signal(&ctx->mExtCommitDoneCond);
                pthread_mutex_unlock(&ctx->mExtCommitDoneLock);
            }
        }
    }
    return NULL;
}

static int hwc_prepare(hwc_composer_device_t *dev, hwc_layer_list_t* list)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    ctx->overlayInUse = false;

    if(ctx->mSecureConfig == true) {
        // This will tear down External Display Device.
        ctx->mOverlay->setState(ovutils::OV_FB);
        return 0;
    }

    ctx->externalDisplay = ctx->mExtDisplay->getExternalDisplay();

    if(ctx->externalDisplay)
        ovutils::setExtType(ctx->externalDisplay);
    if (LIKELY(list)) {
        //reset for this draw round
        VideoOverlay::reset();
        ExtOnly::reset();
        UIMirrorOverlay::reset();

        getLayerStats(ctx, list);
        // Mark all layers to COPYBIT initially
        CopyBit::prepare(ctx, list);
        if(VideoOverlay::prepare(ctx, list)) {
            ctx->overlayInUse = true;
            //Nothing here
        } else if(ExtOnly::prepare(ctx, list)) {
            ctx->overlayInUse = true;
        } else if(UIMirrorOverlay::prepare(ctx, list)) {
            ctx->overlayInUse = true;
        } else if(MDPComp::configure(dev, list)) {
            ctx->overlayInUse = true;
        } else if (0) {
            //Other features
            ctx->overlayInUse = true;
        } else { // Else set this flag to false, otherwise video cases
                 // fail in non-overlay targets.
            ctx->overlayInUse = false;
            ctx->mOverlay->setState(ovutils::OV_FB);
        }

        qdutils::CBUtils::checkforGPULayer(list);
    }

    return 0;
}

static int hwc_eventControl(struct hwc_composer_device* dev,
                             int event, int value)
{
    int ret = 0;

    hwc_context_t* ctx = (hwc_context_t*)(dev);
    private_module_t* m = reinterpret_cast<private_module_t*>(
                ctx->mFbDev->common.module);
    switch(event) {
        case HWC_EVENT_VSYNC:
            if (ctx->vstate.enable == value)
                break;

            pthread_mutex_lock(&ctx->vstate.lock);
            ctx->vstate.enable = !!value;
            pthread_cond_signal(&ctx->vstate.cond);

            ALOGD_IF (VSYNC_DEBUG, "VSYNC state changed to %s",
                                           (value)?"ENABLED":"DISABLED");
            pthread_mutex_unlock(&ctx->vstate.lock);
            if(ctx->mExtDisplay->isHDMIConfigured() &&
                    (ctx->externalDisplay == EXTERN_DISPLAY_FB1)) {
                // enableHDMIVsync will return -errno on error
                ret = ctx->mExtDisplay->enableHDMIVsync(value);
            }
            break;
       case HWC_EVENT_ORIENTATION:
             ctx->deviceOrientation = value;
           break;
        default:
            ret = -EINVAL;
    }
    return ret;
}

static int hwc_query(struct hwc_composer_device* dev,
                     int param, int* value)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    private_module_t* m = reinterpret_cast<private_module_t*>(
        ctx->mFbDev->common.module);

    switch (param) {
    case HWC_BACKGROUND_LAYER_SUPPORTED:
        // Not supported for now
        value[0] = 0;
        break;
    case HWC_VSYNC_PERIOD:
        value[0] = 1000000000.0 / m->fps;
        ALOGI("fps: %d", value[0]);
        break;
    default:
        return -EINVAL;
    }
    return 0;

}

static int hwc_set(hwc_composer_device_t *dev,
                   hwc_display_t dpy,
                   hwc_surface_t sur,
                   hwc_layer_list_t* list)
{
    int ret = 0;
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if (dpy && sur) {
        if (LIKELY(list)) {
            VideoOverlay::draw(ctx, list);
            ExtOnly::draw(ctx, list);
            CopyBit::draw(ctx, list, (EGLDisplay)dpy, (EGLSurface)sur);
            MDPComp::draw(ctx, list);
        }
        eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
        if(ctx->mMDP.hasOverlay) {
            if(ctx->externalDisplay) {
                wait4fbPost(ctx);
                //Can draw to Ext Disp only when fb_post is reached
                //Ext Display Rotate + ov_play and primary commit (PAN)
                //happening in parallel
                UIMirrorOverlay::draw(ctx);
                //signal the commitExt thread commit
                pthread_mutex_lock(&ctx->mExtCommitLock);
                ctx->mExtCommit = true;
                pthread_cond_signal(&ctx->mExtCommitCond);
                pthread_mutex_unlock(&ctx->mExtCommitLock);
            }
            //Virtual barrier for threads to finish
            wait4Pan(ctx);
            // wait for video commit on Ext display do finish..
            if(ctx->externalDisplay &&
                                VideoOverlay::isModeOn())
                wait4ExtCommitDone(ctx);
        }
    } else {
        ctx->mOverlay->setState(ovutils::OV_CLOSED);
        ctx->qbuf->unlockAll();
    }
    ctx->qbuf->unlockAllPrevious();
    return ret;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    if(!dev) {
        ALOGE("%s: NULL device pointer", __FUNCTION__);
        return -1;
    }
    closeContext((hwc_context_t*)dev);
    free(dev);

    return 0;
}

static int hwc_device_open(const struct hw_module_t* module, const char* name,
                           struct hw_device_t** device)
{
    int status = -EINVAL;

    if (!strcmp(name, HWC_HARDWARE_COMPOSER)) {
        struct hwc_context_t *dev;
        dev = (hwc_context_t*)malloc(sizeof(*dev));
        memset(dev, 0, sizeof(*dev));

        //Initialize hwc context
        initContext(dev);

        pthread_t extDispCommitThread;
        pthread_create(&extDispCommitThread, NULL, &commitExtDisp, (void *)dev);

        //Setup HWC methods
        hwc_methods_t *methods;
        methods = (hwc_methods_t *)malloc(sizeof(*methods));
        memset(methods, 0, sizeof(*methods));
        methods->eventControl = hwc_eventControl;

        dev->device.common.tag     = HARDWARE_DEVICE_TAG;
        //XXX: This disables hardware vsync on 8x55
        // Fix when HW vsync is available on 8x55
        if(dev->mMDP.version == 400 || (dev->mMDP.version >= 500))
            dev->device.common.version = 0;
        else
            dev->device.common.version = HWC_DEVICE_API_VERSION_0_3;
        dev->device.common.module  = const_cast<hw_module_t*>(module);
        dev->device.common.close   = hwc_device_close;
        dev->device.prepare        = hwc_prepare;
        dev->device.set            = hwc_set;
        dev->device.registerProcs  = hwc_registerProcs;
        dev->device.query          = hwc_query;
        dev->device.methods        = methods;
        *device                    = &dev->device.common;
        status = 0;
    }
    return status;
}
