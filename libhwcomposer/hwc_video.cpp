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

#define VIDEO_DEBUG 0
#include <overlay.h>
#include "hwc_video.h"
#include "hwc_utils.h"
#include "qdMetaData.h"
#include "mdp_version.h"
#include "external.h"

namespace qhwc {

namespace ovutils = overlay::utils;

//Static Members
bool VideoOverlay::sIsModeOn[] = {false};
ovutils::eDest VideoOverlay::sDest[] = {ovutils::OV_INVALID};
ovutils::Dim VideoOverlay::sPrevPosition;
ovutils::Dim VideoOverlay::sPrevCrop;
int VideoOverlay::sPrevTransform = 0;

//Cache stats, figure out the state, config overlay
bool VideoOverlay::prepare(hwc_context_t *ctx, hwc_display_contents_1_t *list,
        int dpy) {

    int yuvIndex =  ctx->listStats[dpy].yuvIndices[0];
    sIsModeOn[dpy] = false;

    if(!ctx->mMDP.hasOverlay) {
       ALOGD_IF(VIDEO_DEBUG,"%s, this hw doesnt support overlay", __FUNCTION__);
       return false;
    }

    if(isSecuring(ctx)) {
       ALOGD_IF(VIDEO_DEBUG,"%s: MDP Secure is active", __FUNCTION__);
       return false;
    }

    if(yuvIndex == -1 || ctx->listStats[dpy].yuvCount != 1) {
        // reset the stale destination values
        ovutils::Dim d(0,0,0,0);
        sPrevCrop = d;
        sPrevPosition = d;
        sPrevTransform = 0;
        return false;
    }

    if(dpy && ctx->mExtResumed &&
           ctx->listStats[dpy].isDisplayAnimating) {
        // This is the case where the external is resumed and display
        // animating, and we dont want to show any video layer on Ext
        return false;
    } else if( dpy && ctx->mExtResumed &&
                 !ctx->listStats[dpy].isDisplayAnimating) {
        // Reset this to false, as we are done handling the above usecase
        ctx->mExtResumed = false;
    }

    if(isSkipLayer(&list->hwLayers[yuvIndex]) &&
                                (dpy == HWC_DISPLAY_PRIMARY)) {
        //Skip layer on primary, return
        return false;
    }

    //index guaranteed to be not -1 at this point
    hwc_layer_1_t *layer = &list->hwLayers[yuvIndex];
    if (isSecureModePolicy(ctx->mMDP.version)) {
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if(ctx->mSecureMode) {
            if (! isSecureBuffer(hnd)) {
                ALOGD_IF(VIDEO_DEBUG, "%s: Handle non-secure video layer"
                         "during secure playback gracefully", __FUNCTION__);
                return false;
            }
        } else {
            if (isSecureBuffer(hnd)) {
                ALOGD_IF(VIDEO_DEBUG, "%s: Handle secure video layer"
                         "during non-secure playback gracefully", __FUNCTION__);
                return false;
            }
        }
    }
    if(configure(ctx, dpy, layer)) {
        markFlags(layer);
        sIsModeOn[dpy] = true;
    }

    return sIsModeOn[dpy];
}

void VideoOverlay::markFlags(hwc_layer_1_t *layer) {
    if(layer) {
        layer->compositionType = HWC_OVERLAY;
        layer->hints |= HWC_HINT_CLEAR_FB;
    }
}

bool VideoOverlay::configure(hwc_context_t *ctx, int dpy,
        hwc_layer_1_t *layer) {
    overlay::Overlay& ov = *(ctx->mOverlay);
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);

    //Request a VG pipe
    ovutils::eDest dest = ov.nextPipe(ovutils::OV_MDP_PIPE_VG, dpy);
    if(dest == ovutils::OV_INVALID) { //None available
        return false;
    }

    sDest[dpy] = dest;

    ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
    if (isSecureBuffer(hnd)) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
    }

    if(layer->blending == HWC_BLENDING_PREMULT) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_BLEND_FG_PREMULT);
    }

    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;
    if (metadata && (metadata->operation & PP_PARAM_INTERLACED) &&
                                             metadata->interlaced) {
        ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_DEINTERLACE);
    }

    ovutils::eIsFg isFgFlag = ovutils::IS_FG_OFF;
    bool setIsFG = false;
    if (ctx->deviceOrientation && ctx->listStats[dpy].isDisplayAnimating && dpy)
        setIsFG = true;

    if (ctx->listStats[dpy].numAppLayers == 1 || setIsFG) {
        isFgFlag = ovutils::IS_FG_SET;
    }

    ovutils::eRotFlags rotFlags = ovutils::ROT_FLAGS_NONE;
    if(ctx->mMDP.version >= qdutils::MDP_V4_2 &&
                ctx->mMDP.version < qdutils::MDSS_V5) {
        rotFlags = ovutils::ROT_DOWNSCALE_ENABLED;
    }

    ovutils::PipeArgs parg(mdpFlags,
            info,
            ovutils::ZORDER_1,
            isFgFlag,
            rotFlags);

    ov.setSource(parg, dest);

    int transform = layer->transform;
    ovutils::eTransform orient =
            static_cast<ovutils::eTransform>(transform);

    hwc_rect_t sourceCrop = layer->sourceCrop;
    hwc_rect_t displayFrame = layer->displayFrame;

    //Calculate the rect for primary based on whether the supplied position
    //is within or outside bounds.
    const int fbWidth = ctx->dpyAttr[dpy].xres;
    const int fbHeight = ctx->dpyAttr[dpy].yres;

    if(dpy == HWC_DISPLAY_PRIMARY) {
        if( displayFrame.left < 0 ||
            displayFrame.top < 0 ||
            displayFrame.right > fbWidth ||
            displayFrame.bottom > fbHeight) {
            calculate_crop_rects(sourceCrop, displayFrame, fbWidth, fbHeight,
                                 transform);
        }

        // source crop x,y,w,h
        ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
                           sourceCrop.right - sourceCrop.left,
                           sourceCrop.bottom - sourceCrop.top);
        //Only for Primary
        ov.setCrop(dcrop, dest);

        ov.setTransform(orient, dest);

        // position x,y,w,h
        ovutils::Dim dpos(displayFrame.left,
                          displayFrame.top,
                          displayFrame.right - displayFrame.left,
                          displayFrame.bottom - displayFrame.top);
        ov.setPosition(dpos, dest);
    } else if(dpy) { // External display
        if(!ctx->listStats[dpy].isDisplayAnimating) {
            if( displayFrame.left < 0 ||
                displayFrame.top < 0 ||
                displayFrame.right > fbWidth ||
                displayFrame.bottom > fbHeight) {
                calculate_crop_rects(sourceCrop, displayFrame, fbWidth,
                                     fbHeight, transform);
            }

            // source crop x,y,w,h
            ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
                               sourceCrop.right - sourceCrop.left,
                               sourceCrop.bottom - sourceCrop.top);

            ov.setCrop(dcrop, dest);
            sPrevCrop = dcrop;
            ov.setTransform(orient, dest);
            sPrevTransform = orient;

            // position x,y,w,h
            ovutils::Dim dpos(displayFrame.left,
                              displayFrame.top,
                              displayFrame.right - displayFrame.left,
                              displayFrame.bottom - displayFrame.top);

            //read wfd property
            char property_value[PROPERTY_VALUE_MAX];
            property_get("hw.wfdON", property_value,"0");
            int wfdEnable = atoi(property_value);
            if(wfdEnable) {
                int width = 0, height = 0;
                //query MDP configured attributes
                ctx->mExtDisplay->getWfdAttr(width, height);
                if(height != (int)ctx->dpyAttr[dpy].yres) {
                    // FB_TARGET is configured for different resolution
                    // and MDP is configured for another
                    // SF calculates dpos with respect to FB_TARGET resolution
                    // Re-calculate dpos with respect to MDP resolution
                    int fbWidth_fbt  = ctx->dpyAttr[dpy].xres;
                    int fbHeight_fbt = ctx->dpyAttr[dpy].yres;

                    // calculate position ratio
                    float xRatio = (float)dpos.x/fbWidth_fbt;
                    float yRatio = (float)dpos.y/fbHeight_fbt;
                    float wRatio = (float)dpos.w/fbWidth_fbt;
                    float hRatio = (float)dpos.h/fbHeight_fbt;

                    // calculate the destination position
                    dpos.x = (xRatio * width);
                    dpos.y = (yRatio * height);
                    dpos.w = (wRatio * width);
                    dpos.h = (hRatio * height);
                }
            }
            ov.setPosition(dpos, dest);
            sPrevPosition = dpos;
        } else {
            ov.setCrop(sPrevCrop, dest);
            ov.setTransform(sPrevTransform, dest);
            ov.setPosition(sPrevPosition, dest);
        }
    }

    if (!ov.commit(dest)) {
        ALOGE("VideoOverlay::%s: commit fails for dpy[%d]", __FUNCTION__, dpy);
        return false;
    }
    return true;
}

bool VideoOverlay::draw(hwc_context_t *ctx, hwc_display_contents_1_t *list,
        int dpy)
{
    if(!sIsModeOn[dpy]) {
        return true;
    }

    int yuvIndex = ctx->listStats[dpy].yuvIndices[0];
    if(yuvIndex == -1) {
        return true;
    }

    private_handle_t *hnd = (private_handle_t *)
            list->hwLayers[yuvIndex].handle;

    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);

    if (!ov.queueBuffer(hnd->fd, hnd->offset,
                sDest[dpy])) {
        ALOGE("%s: queueBuffer failed for dpy=%d", __FUNCTION__, dpy);
        ret = false;
    }

    return ret;
}

}; //namespace qhwc
