/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, Code Aurora Forum. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are
 * retained for attribution purposes only.
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

#define HWC_UI_MIRROR 0
#include <gralloc_priv.h>
#include <fb_priv.h>
#include "hwc_uimirror.h"
#include "hwc_external.h"
#include "hwc_video.h"


namespace qhwc {


//Static Members
ovutils::eOverlayState UIMirrorOverlay::sState = ovutils::OV_FB;
bool UIMirrorOverlay::sIsUiMirroringOn = false;


//Prepare the overlay for the UI mirroring
bool UIMirrorOverlay::prepare(hwc_context_t *ctx, hwc_layer_list_t *list) {
    sState = ovutils::OV_FB;
    sIsUiMirroringOn = false;

    if(!ctx->mMDP.hasOverlay) {
       ALOGD_IF(HWC_UI_MIRROR, "%s, this hw doesnt support mirroring",
                                                               __FUNCTION__);
       return false;
    }
    // If external display is connected
    if(ctx->mExtDisplay->getExternalDisplay()) {
        if(VideoOverlay::getYuvCount() == 1) {
            ALOGD_IF(HWC_UI_MIRROR, "In %s: setting the state to"
                                         "2D_TRUE_OV_UI_MIRROR", __FUNCTION__);
            sState = ovutils::OV_2D_TRUE_UI_MIRROR;
        } else {
            ALOGD_IF(HWC_UI_MIRROR, "In %s: setting the state to"
                                         "2D_UI_MIRROR", __FUNCTION__);
            sState = ovutils::OV_UI_MIRROR;
        }
        if(configure(ctx, list))
            sIsUiMirroringOn = true;
    }
    return sIsUiMirroringOn;
}

// Configure
bool UIMirrorOverlay::configure(hwc_context_t *ctx, hwc_layer_list_t *list)
{
    bool ret = false;
    if (LIKELY(ctx->mOverlay)) {
        overlay::Overlay& ov = *(ctx->mOverlay);
        // Set overlay state
        ov.setState(sState);
        framebuffer_device_t *fbDev = ctx->mFbDev;
        if(fbDev) {
            private_module_t* m = reinterpret_cast<private_module_t*>(
                    fbDev->common.module);
            int alignedW = ALIGN_TO(m->info.xres, 32);

            private_handle_t const* hnd =
                    reinterpret_cast<private_handle_t const*>(m->framebuffer);
            unsigned int size = hnd->size/m->numBuffers;
            ovutils::Whf info(alignedW, hnd->height, hnd->format, size);
            //Destination pipe for UI channel
            ovutils::eDest dest = ovutils::OV_PIPE0;
            switch (sState) {
                case ovutils::OV_2D_TRUE_UI_MIRROR:
                case ovutils::OV_UI_MIRROR:
                    // UIMirror pipe is OV_PIPE2
                    dest = ovutils::OV_PIPE2;
                    break;
                default:
                    ALOGE("Incorrect state in UIMirrorOverlay::Configure!!!");
                    return false;
            }
            ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
            /* - TODO: Secure content
               if (hnd->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
               ovutils::setMdpFlags(mdpFlags,
               ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
               }
             */

            // Use VG pipe if target does not support true mirroring
            if(!overlay::utils::FrameBufferInfo::
                                        getInstance()->supportTrueMirroring()) {
                ovutils::setMdpFlags(mdpFlags,
                                     ovutils::OV_MDP_PIPE_SHARE);
            }

            ovutils::PipeArgs parg(mdpFlags,
                    info,
                    ovutils::ZORDER_0,
                    ovutils::IS_FG_OFF,
                    ovutils::ROT_0_ENABLED);
            ov.setSource(parg, dest);

            // x,y,w,h
            ovutils::Dim dcrop(0, 0, m->info.xres, m->info.yres);
            ov.setCrop(dcrop, dest);
            ovutils::eTransform orient =
                    static_cast<ovutils::eTransform>(ctx->deviceOrientation);
            ov.setTransform(orient, dest);

            ovutils::Dim dim;
            dim.x = 0;
            dim.y = 0;
            dim.w = m->info.xres;
            dim.h = m->info.yres;
            ov.setPosition(dim, dest);
            if (!ov.commit(dest)) {
                ALOGE("%s: commit fails", __FUNCTION__);
                return false;
            }
            ret = true;
        }
    }
    return ret;
}

bool UIMirrorOverlay::draw(hwc_context_t *ctx)
{
    if(!sIsUiMirroringOn) {
        return true;
    }
    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);
    ovutils::eOverlayState state = ov.getState();
    ovutils::eDest dest = ovutils::OV_PIPE_ALL;
    framebuffer_device_t *fbDev = ctx->mFbDev;
    if(fbDev) {
        private_module_t* m = reinterpret_cast<private_module_t*>(
                              fbDev->common.module);
        switch (state) {
            case ovutils::OV_UI_MIRROR:
            case ovutils::OV_2D_TRUE_UI_MIRROR:
                if (!ov.queueBuffer(m->framebuffer->fd, m->currentOffset,
                                                           ovutils::OV_PIPE2)) {
                    ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                    ret = false;
                }
                break;
        default:
            break;
        }
    }
    return ret;
}

//---------------------------------------------------------------------
}; //namespace qhwc
