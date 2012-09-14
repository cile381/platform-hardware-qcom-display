/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, The Linux Foundation. All rights reserved.
 * Not a Contribution, Apache license notifications and license are retained
 * for attribution purposes only.
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

#define VIDEOPIP_DEBUG 0
#include <overlay.h>
#include "hwc_qbuf.h"
#include "hwc_external.h"
#include "hwc_pip.h"

namespace qhwc {

//Static Members
ovutils::eOverlayState VideoPIP::sState = ovutils::OV_FB;
int VideoPIP::sYuvCount = 0;
int VideoPIP::sYuvLayerIndex = -1;
bool VideoPIP::sIsYuvLayerSkip = false;
int VideoPIP::sPIPLayerIndex = -1;
bool VideoPIP::sIsModeOn = false;

//Cache stats, figure out the state, config overlay
bool VideoPIP::prepare(hwc_context_t *ctx, hwc_layer_list_t *list) {
    sIsModeOn = false;
    if(!ctx->mMDP.hasOverlay) {
       ALOGD_IF(VIDEOPIP_DEBUG,"%s, this hw doesnt support overlay", __FUNCTION__);
       return false;
    }
    if(sYuvLayerIndex == -1 || sPIPLayerIndex == -1) {
        return false;
    }
    chooseState(ctx);
    //if the state chosen above is OV_FB, skip this block.
    if(sState != ovutils::OV_FB) {
        hwc_layer_t *yuvLayer = &list->hwLayers[sYuvLayerIndex];
        hwc_layer_t *pipLayer = NULL;

        if(sPIPLayerIndex != -1) {
           pipLayer = &list->hwLayers[sPIPLayerIndex];
        }
        if(configure(ctx, yuvLayer, pipLayer)) {
            markFlags(&list->hwLayers[sYuvLayerIndex]);
            if(sPIPLayerIndex != -1) {
                //Mark PIP layer as HWC_OVERLAY
                markFlags(&list->hwLayers[sPIPLayerIndex]);
            }
            sIsModeOn = true;
        }
    }

    ALOGD_IF(VIDEOPIP_DEBUG, "%s: stats: yuvCount = %d, yuvIndex = %d,"
            "IsYuvLayerSkip = %d, pipLayerIndex = %d, IsModeOn = %d",
            __FUNCTION__, sYuvCount, sYuvLayerIndex,
            sIsYuvLayerSkip, sPIPLayerIndex, sIsModeOn);

    return sIsModeOn;
}

void VideoPIP::chooseState(hwc_context_t *ctx) {
    ALOGD_IF(VIDEOPIP_DEBUG, "%s: old state = %s", __FUNCTION__,
            ovutils::getStateString(sState));

    ovutils::eOverlayState newState = ovutils::OV_FB;

    //Support 1 video layer
    if(sYuvCount == 2 && !ctx->externalDisplay) {
        /* PIP: Picture in picture
           If HDMI is not connected as secondary and there are two videos
           we can use two VG pipes for video playback. */
        newState = ovutils::OV_2D_PIP_VIDEO_ON_PANEL;
    }
    sState = newState;
    ALOGD_IF(VIDEOPIP_DEBUG, "%s: new chosen state = %s", __FUNCTION__,
            ovutils::getStateString(sState));
}

void VideoPIP::markFlags(hwc_layer_t *layer) {
    switch(sState) {
        case ovutils::OV_2D_PIP_VIDEO_ON_PANEL:
            layer->compositionType = HWC_OVERLAY;
            break;
        default:
            break;
    }
}

/* Helpers */
bool configPrimaryVideo(hwc_context_t *ctx, hwc_layer_t *layer) {
    overlay::Overlay& ov = *(ctx->mOverlay);
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);

    ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
    if (hnd->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
    }

    ovutils::eIsFg isFgFlag = ovutils::IS_FG_OFF;
    if (ctx->numHwLayers == 1) {
        isFgFlag = ovutils::IS_FG_SET;
    }

    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;
    if(metadata && ctx->mPpParams[VIDEO_LAYER_0].isValid){
        /* Preference will be for the HSIC & QSEED values
         * set through binder */
        metadata->operation |= ctx->mPpParams[VIDEO_LAYER_0].ops;
        if(metadata->operation & PP_PARAM_HSIC) {
            metadata->hsicData.hue = ctx->mPpParams[VIDEO_LAYER_0].hue;
            metadata->hsicData.saturation = ctx->mPpParams[VIDEO_LAYER_0].saturation;
            metadata->hsicData.intensity = ctx->mPpParams[VIDEO_LAYER_0].intensity;
            metadata->hsicData.contrast = ctx->mPpParams[VIDEO_LAYER_0].contrast;
        }
        if(metadata->operation & PP_PARAM_SHARPNESS) {
            metadata->sharpness = ctx->mPpParams[VIDEO_LAYER_0].sharpness;
        }
        if(ov.setVisualParams(*metadata, ovutils::OV_PIPE0))
            ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_PP_EN);
    }

    //Ensure that VG pipe is allocated in cases where buffer-type
    //is video while format is RGB
    ovutils::setMdpFlags(mdpFlags,
            ovutils::OV_MDP_PIPE_SHARE);

    ovutils::PipeArgs parg(mdpFlags,
            info,
            ovutils::ZORDER_0,
            isFgFlag,
            ovutils::ROT_FLAGS_NONE);
    ov.setSource(parg, ovutils::OV_PIPE0);

    if(metadata && ctx->mPpParams[VIDEO_LAYER_0].isValid){
        ovutils::clearMdpFlags(mdpFlags, ovutils::OV_MDP_PP_EN);
        /* Done with setting HSIC values. Clear the
         * PP_PARAM_HSIC and PP_PARAM_SHARPNESS flags
         * from metadata operation if present.
         */
        metadata->operation &= ~(ctx->mPpParams[VIDEO_LAYER_0].ops);
    }

    hwc_rect_t sourceCrop = layer->sourceCrop;
    hwc_rect_t displayFrame = layer->displayFrame;
    set_ov_dimensions(ctx,VIDEO_LAYER_0,sourceCrop,displayFrame);
    // x,y,w,h
    ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
            sourceCrop.right - sourceCrop.left,
            sourceCrop.bottom - sourceCrop.top);

    ovutils::Dim dpos;
    dpos.x = displayFrame.left;
    dpos.y = displayFrame.top;
    dpos.w = (displayFrame.right - displayFrame.left);
    dpos.h = (displayFrame.bottom - displayFrame.top);

    //Calculate the rect for primary based on whether the supplied position
    //is within or outside bounds.
    const int fbWidth =
            ovutils::FrameBufferInfo::getInstance()->getWidth();
    const int fbHeight =
            ovutils::FrameBufferInfo::getInstance()->getHeight();

    if( displayFrame.left < 0 ||
            displayFrame.top < 0 ||
            displayFrame.right > fbWidth ||
            displayFrame.bottom > fbHeight) {

        calculate_crop_rects(sourceCrop, displayFrame, fbWidth, fbHeight);

        //Update calculated width and height
        dcrop.w = sourceCrop.right - sourceCrop.left;
        dcrop.h = sourceCrop.bottom - sourceCrop.top;

        dpos.x = displayFrame.left;
        dpos.y = displayFrame.top;
        dpos.w = displayFrame.right - displayFrame.left;
        dpos.h = displayFrame.bottom - displayFrame.top;
    }

    ALOGD_IF(VIDEOPIP_DEBUG,"Crop values set for the main video are %d %d %d %d",
            sourceCrop.left,sourceCrop.top,sourceCrop.right,sourceCrop.bottom);
    ALOGD_IF(VIDEOPIP_DEBUG,
            "Destination position values for the main video are %d %d %d %d",
            displayFrame.left,displayFrame.top,displayFrame.right,displayFrame.bottom);

    //Only for Primary
    ov.setCrop(dcrop, ovutils::OV_PIPE0);

    ovutils::eTransform orient =
            static_cast<ovutils::eTransform>(layer->transform);
    ov.setTransform(orient, ovutils::OV_PIPE0);

    ov.setPosition(dpos, ovutils::OV_PIPE0);

    if (!ov.commit(ovutils::OV_PIPE0)) {
        ALOGE("%s: commit fails", __FUNCTION__);
        return false;
    }
    return true;
}


// Configure the second video in pip scenario
bool configPIPVideo(hwc_context_t *ctx, hwc_layer_t *layer) {
    overlay::Overlay& ov = *(ctx->mOverlay);
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);

    ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
    if (hnd->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
    }

    ovutils::eIsFg isFgFlag = ovutils::IS_FG_OFF;

    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;
    if(metadata && ctx->mPpParams[VIDEO_LAYER_1].isValid){
        /* Preference will be for the HSIC & QSEED values
         * set through binder */
        metadata->operation |= ctx->mPpParams[VIDEO_LAYER_1].ops;
        if(metadata->operation & PP_PARAM_HSIC) {
            metadata->hsicData.hue = ctx->mPpParams[VIDEO_LAYER_1].hue;
            metadata->hsicData.saturation = ctx->mPpParams[VIDEO_LAYER_1].saturation;
            metadata->hsicData.intensity = ctx->mPpParams[VIDEO_LAYER_1].intensity;
            metadata->hsicData.contrast = ctx->mPpParams[VIDEO_LAYER_1].contrast;
        }
        if(metadata->operation & PP_PARAM_SHARPNESS) {
            metadata->sharpness = ctx->mPpParams[VIDEO_LAYER_1].sharpness;
        }
        if(ov.setVisualParams(*metadata, ovutils::OV_PIPE1))
            ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_PP_EN);
    }

    //Ensure that VG pipe is allocated in cases where buffer-type
    //is video while format is RGB
    ovutils::setMdpFlags(mdpFlags,
            ovutils::OV_MDP_PIPE_SHARE);

    //Set z-order 1 since this video is on top of the
    //primary video
    ovutils::PipeArgs parg(mdpFlags,
            info,
            ovutils::ZORDER_1,
            isFgFlag,
            ovutils::ROT_DOWNSCALE_ENABLED);

    // Use pipe 1, pipe 0 is used for primary video
    ov.setSource(parg, ovutils::OV_PIPE1);

    if(metadata && ctx->mPpParams[VIDEO_LAYER_1].isValid){
        /* Done with setting HSIC values. Clear the
         * PP_PARAM_HSIC and PP_PARAM_SHARPNESS flags
         * from metadata operation if present.
         */
        metadata->operation &= ~(ctx->mPpParams[VIDEO_LAYER_1].ops);
    }

    hwc_rect_t sourceCrop = layer->sourceCrop;
    hwc_rect_t displayFrame = layer->displayFrame;
    set_ov_dimensions(ctx,VIDEO_LAYER_1,sourceCrop,displayFrame);
    // x,y,w,h
    ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
            sourceCrop.right - sourceCrop.left,
            sourceCrop.bottom - sourceCrop.top);

    ovutils::Dim dpos;
    dpos.x = displayFrame.left;
    dpos.y = displayFrame.top;
    dpos.w = (displayFrame.right - displayFrame.left);
    dpos.h = (displayFrame.bottom - displayFrame.top);

    //Calculate the rect for primary based on whether the supplied position
    //is within or outside bounds.
    const int fbWidth =
            ovutils::FrameBufferInfo::getInstance()->getWidth();
    const int fbHeight =
            ovutils::FrameBufferInfo::getInstance()->getHeight();

    if( displayFrame.left < 0 ||
            displayFrame.top < 0 ||
            displayFrame.right > fbWidth ||
            displayFrame.bottom > fbHeight) {

        calculate_crop_rects(sourceCrop, displayFrame, fbWidth, fbHeight);

        //Update calculated width and height
        dcrop.w = sourceCrop.right - sourceCrop.left;
        dcrop.h = sourceCrop.bottom - sourceCrop.top;

        dpos.x = displayFrame.left;
        dpos.y = displayFrame.top;
        dpos.w = displayFrame.right - displayFrame.left;
        dpos.h = displayFrame.bottom - displayFrame.top;
    }

    ALOGD_IF(VIDEOPIP_DEBUG,"Crop values set for the pip video are %d %d %d %d",
            sourceCrop.left,sourceCrop.top,sourceCrop.right,sourceCrop.bottom);
    ALOGD_IF(VIDEOPIP_DEBUG,
            "Destination position values for the pip video are %d %d %d %d",
            displayFrame.left,displayFrame.top,displayFrame.right,displayFrame.bottom);
    //Only for Primary
    ov.setCrop(dcrop, ovutils::OV_PIPE1);

    ovutils::eTransform orient =
            static_cast<ovutils::eTransform>(layer->transform);
    ov.setTransform(orient, ovutils::OV_PIPE1);

    ov.setPosition(dpos, ovutils::OV_PIPE1);

    if (!ov.commit(ovutils::OV_PIPE1)) {
        ALOGE("%s: commit fails", __FUNCTION__);
        return false;
    }
    return true;
}

bool VideoPIP::configure(hwc_context_t *ctx, hwc_layer_t *yuvLayer,
        hwc_layer_t *pipLayer) {

    bool ret = true;
    if (LIKELY(ctx->mOverlay)) {
        overlay::Overlay& ov = *(ctx->mOverlay);
        // Set overlay state
        ov.setState(sState);
        switch(sState) {
            case ovutils::OV_2D_PIP_VIDEO_ON_PANEL:
                //Configure the primary or background video
                ret &= configPrimaryVideo(ctx, yuvLayer);
                //Configure the PIP video
                ret &= configPIPVideo(ctx, pipLayer);
                break;
            default:
                return false;
        }
    } else {
        //Ov null
        return false;
    }
    return ret;
}

bool VideoPIP::draw(hwc_context_t *ctx, hwc_layer_list_t *list)
{
    if(!sIsModeOn || sYuvLayerIndex == -1 || sPIPLayerIndex == -1) {
        return true;
    }

    private_handle_t *hnd = (private_handle_t *)
            list->hwLayers[sYuvLayerIndex].handle;
    // Lock this buffer for read.
    ctx->qbuf->lockAndAdd(hnd);

    private_handle_t *piphnd = NULL;
    piphnd = (private_handle_t *)list->hwLayers[sPIPLayerIndex].handle;
        ctx->qbuf->lockAndAdd(piphnd);

    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);
    ovutils::eOverlayState state = ov.getState();

    switch (state) {
        case ovutils::OV_2D_PIP_VIDEO_ON_PANEL:
            // Play first video (background)
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE0)) {
                ALOGE("%s: queueBuffer failed for primary video", __FUNCTION__);
                ret = false;
            }
            //Play pip video
            if (piphnd && !ov.queueBuffer(piphnd->fd, piphnd->offset,
                        ovutils::OV_PIPE1)) {
                ALOGE("%s: queueBuffer failed for pip video", __FUNCTION__);
                ret = false;
            }
            break;

        default:
            ALOGE("%s Unused state %s", __FUNCTION__,
                    ovutils::getStateString(state));
            break;
    }

    return ret;
}

}; //namespace qhwc
