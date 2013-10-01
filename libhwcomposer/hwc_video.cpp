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
#include "hwc_qbuf.h"
#include "hwc_video.h"
#include "hwc_external.h"
#include "hwc_uimirror.h"
#include "qdMetaData.h"

namespace qhwc {

//Static Members
ovutils::eOverlayState VideoOverlay::sState = ovutils::OV_FB;
int VideoOverlay::sYuvCount = 0;
int VideoOverlay::sYuvLayerIndex = -1;
bool VideoOverlay::sIsYuvLayerSkip = false;
int VideoOverlay::sCCLayerIndex = -1;
bool VideoOverlay::sIsModeOn = false;

//Cache stats, figure out the state, config overlay
bool VideoOverlay::prepare(hwc_context_t *ctx, hwc_layer_list_t *list) {
    sIsModeOn = false;
    if(!ctx->mMDP.hasOverlay) {
       ALOGD_IF(VIDEO_DEBUG,"%s, this hw doesnt support overlay", __FUNCTION__);
       return false;
    }
    if(sYuvLayerIndex == -1) {
        return false;
    }
    hwc_layer_t *yuvLayer = &list->hwLayers[sYuvLayerIndex];
    private_handle_t *hnd = (private_handle_t *)yuvLayer->handle;
    if(ctx->mSecure) {
        if (! (hnd->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER)) {
            ALOGD_IF(VIDEO_DEBUG, "%s, non-secure video layer during secure"
                    "playback gracefully", __FUNCTION__);
            return false;
        }
    } else {
        if ((hnd->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER)) {
            ALOGD_IF(VIDEO_DEBUG, "%s, secure video layer during"
                    "non-secure playback gracefully",__FUNCTION__);
            return false;
        }
    }
    chooseState(ctx);
    //if the state chosen above is OV_FB, skip this block.
    if(sState != ovutils::OV_FB) {
        hwc_layer_t *yuvLayer = &list->hwLayers[sYuvLayerIndex];
        hwc_layer_t *ccLayer = NULL;
        if(sCCLayerIndex != -1)
            ccLayer = &list->hwLayers[sCCLayerIndex];

        if(configure(ctx, yuvLayer, ccLayer)) {
            markFlags(&list->hwLayers[sYuvLayerIndex]);
            sIsModeOn = true;
        }
    }

    ALOGD_IF(VIDEO_DEBUG, "%s: stats: yuvCount = %d, yuvIndex = %d,"
            "IsYuvLayerSkip = %d, ccLayerIndex = %d, IsModeOn = %d",
            __FUNCTION__, sYuvCount, sYuvLayerIndex,
            sIsYuvLayerSkip, sCCLayerIndex, sIsModeOn);

    return sIsModeOn;
}

void VideoOverlay::chooseState(hwc_context_t *ctx) {
    ALOGD_IF(VIDEO_DEBUG, "%s: old state = %s", __FUNCTION__,
            ovutils::getStateString(sState));

    ovutils::eOverlayState newState = ovutils::OV_FB;

    //Support 1 video layer
    if(sYuvCount == 1) {
        bool trueMirrorSupported = overlay::utils::FrameBufferInfo::
                                          getInstance()->supportTrueMirroring();
        //Skip on primary, display on ext.
        if(sIsYuvLayerSkip && ctx->externalDisplay) {
            if(trueMirrorSupported && sCCLayerIndex == -1)
                newState = ovutils::OV_UI_MIRROR;
            else
                newState = ovutils::OV_2D_VIDEO_ON_TV;
        } else if(sIsYuvLayerSkip) { //skip on primary, no ext
            newState = ovutils::OV_FB;
        } else if(ctx->externalDisplay) {
            if(trueMirrorSupported && (sCCLayerIndex == -1)) {
                ALOGD_IF(VIDEO_DEBUG,"In %s: setting state to "
                                          "OV_2D_TRUE_UI_MIRROR", __FUNCTION__);
                newState = ovutils::OV_2D_TRUE_UI_MIRROR;
            } else {
                ALOGD_IF(VIDEO_DEBUG,"In %s: setting state to "
                                      "OV_2D_VIDEO_ON_PANEL_TV", __FUNCTION__);
                //display on both primary and secondary
                newState = ovutils::OV_2D_VIDEO_ON_PANEL_TV;
            }
        } else { //display on primary only
            newState = ovutils::OV_2D_VIDEO_ON_PANEL;
        }
    }
    sState = newState;
    ALOGD_IF(VIDEO_DEBUG, "%s: new chosen state = %s", __FUNCTION__,
            ovutils::getStateString(sState));
}

void VideoOverlay::markFlags(hwc_layer_t *layer) {
    switch(sState) {
        case ovutils::OV_2D_VIDEO_ON_PANEL:
        case ovutils::OV_2D_VIDEO_ON_PANEL_TV:
        case ovutils::OV_2D_TRUE_UI_MIRROR:
            layer->compositionType = HWC_OVERLAY;
            layer->hints |= HWC_HINT_CLEAR_FB;
            break;
        case ovutils::OV_2D_VIDEO_ON_TV:
            break; //dont update Flags.
        default:
            break;
    }
}

/* Helpers */
bool configPrimVid(hwc_context_t *ctx, hwc_layer_t *layer) {
    private_handle_t *hnd = (private_handle_t *)layer->handle;

    bool isSecured = hnd->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER;

    if(!isSecured  && !ctx->externalDisplay) {
        //MDP comp handles this case and when not possible
        //we should let GPU handle it
        return false;
    }

    overlay::Overlay& ov = *(ctx->mOverlay);
    ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);

    ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
    if (hnd->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
    }
    if(layer->blending == HWC_BLENDING_PREMULT) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_BLEND_FG_PREMULT);
    }
    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;
    if ((metadata->operation & PP_PARAM_INTERLACED) && metadata->interlaced) {
        ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_DEINTERLACE);
    }

    ovutils::eIsFg isFgFlag = ovutils::IS_FG_OFF;

    if (ctx->numHwLayers == 1) {
        isFgFlag = ovutils::IS_FG_SET;
    }

    ovutils::PipeArgs parg(mdpFlags,
            info,
            ovutils::ZORDER_1,
            isFgFlag,
            ovutils::ROT_DOWNSCALE_ENABLED);
    ov.setSource(parg, ovutils::OV_PIPE0);

    hwc_rect_t sourceCrop = layer->sourceCrop;
    hwc_rect_t displayFrame = layer->displayFrame;

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
        calculate_crop_rects(sourceCrop, displayFrame, fbWidth, fbHeight,
                layer->transform);
    }

    // source crop x,y,w,h
    ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
            sourceCrop.right - sourceCrop.left,
            sourceCrop.bottom - sourceCrop.top);
    //Only for Primary
    ov.setCrop(dcrop, ovutils::OV_PIPE0);

    ovutils::eTransform orient =
            static_cast<ovutils::eTransform>(layer->transform);
    ov.setTransform(orient, ovutils::OV_PIPE0);

    // position x,y,w,h
    ovutils::Dim dpos(displayFrame.left,
            displayFrame.top,
            displayFrame.right - displayFrame.left,
            displayFrame.bottom - displayFrame.top);
    ov.setPosition(dpos, ovutils::OV_PIPE0);

    if (!ov.commit(ovutils::OV_PIPE0)) {
        ALOGE("%s: commit fails", __FUNCTION__);
        return false;
    }
    return true;
}

bool configExtVid(hwc_context_t *ctx, hwc_layer_t *layer) {
    overlay::Overlay& ov = *(ctx->mOverlay);
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);

    ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
    if (hnd->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
    }
    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;
    if ((metadata->operation & PP_PARAM_INTERLACED) && metadata->interlaced) {
        ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_DEINTERLACE);
    }
    ovutils::eIsFg isFgFlag = ovutils::IS_FG_OFF;
    if (ctx->numHwLayers == 1) {
        isFgFlag = ovutils::IS_FG_SET;
    }

    // z_order should be 1 for TRUE MIRRORING
    ovutils::eZorder zorder = (overlay::utils::FrameBufferInfo::
                                  getInstance()->supportTrueMirroring()) ?
                                  ovutils::ZORDER_1: ovutils::ZORDER_0;

    ovutils::PipeArgs parg(mdpFlags,
            info,
            zorder,
            isFgFlag,
            ovutils::ROT_DOWNSCALE_ENABLED);
    ov.setSource(parg, ovutils::OV_PIPE1);

    hwc_rect_t sourceCrop = layer->sourceCrop;
    // x,y,w,h
    ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
            sourceCrop.right - sourceCrop.left,
            sourceCrop.bottom - sourceCrop.top);
    //Only for External
    ov.setCrop(dcrop, ovutils::OV_PIPE1);

    //use sourceTransform only for External
    ov.setTransform(layer->sourceTransform, ovutils::OV_PIPE1);

    ovutils::Dim dpos;
    hwc_rect_t displayFrame = layer->displayFrame;
    dpos.x = displayFrame.left;
    dpos.y = displayFrame.top;
    dpos.w = (displayFrame.right - displayFrame.left);
    dpos.h = (displayFrame.bottom - displayFrame.top);
    dpos.o = ctx->deviceOrientation;

    //Only for External
    ov.setPosition(dpos, ovutils::OV_PIPE1);

    if (!ov.commit(ovutils::OV_PIPE1)) {
        ALOGE("%s: commit fails", __FUNCTION__);
        return false;
    }
    return true;
}

bool configExtCC(hwc_context_t *ctx, hwc_layer_t *layer) {
    if(layer == NULL)
        return true;

    overlay::Overlay& ov = *(ctx->mOverlay);
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);
    ovutils::eIsFg isFgFlag = ovutils::IS_FG_OFF;
    ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
    ovutils::PipeArgs parg(mdpFlags,
            info,
            ovutils::ZORDER_1,
            isFgFlag,
            ovutils::ROT_FLAGS_NONE);
    ov.setSource(parg, ovutils::OV_PIPE2);

    hwc_rect_t sourceCrop = layer->sourceCrop;
    // x,y,w,h
    ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
            sourceCrop.right - sourceCrop.left,
            sourceCrop.bottom - sourceCrop.top);
    //Only for External
    ov.setCrop(dcrop, ovutils::OV_PIPE2);

    // FIXME: Use source orientation for TV when source is portrait
    //Only for External
    ov.setTransform(0, ovutils::OV_PIPE2);

    //Setting position same as crop
    //FIXME stretch to full screen
    ov.setPosition(dcrop, ovutils::OV_PIPE2);

    if (!ov.commit(ovutils::OV_PIPE2)) {
        ALOGE("%s: commit fails", __FUNCTION__);
        return false;
    }
    return true;
}

bool VideoOverlay::configure(hwc_context_t *ctx, hwc_layer_t *yuvLayer,
        hwc_layer_t *ccLayer) {

    bool ret = true;
    if (LIKELY(ctx->mOverlay)) {
        overlay::Overlay& ov = *(ctx->mOverlay);
        // Set overlay state
        ov.setState(sState);
        switch(sState) {
            case ovutils::OV_2D_VIDEO_ON_PANEL:
                ret &= configPrimVid(ctx, yuvLayer);
                break;
            case ovutils::OV_2D_VIDEO_ON_PANEL_TV:
                ret &= configExtVid(ctx, yuvLayer);
                ret &= configExtCC(ctx, ccLayer);
                ret &= configPrimVid(ctx, yuvLayer);
                break;
            case ovutils::OV_2D_VIDEO_ON_TV:
                ret &= configExtVid(ctx, yuvLayer);
                ret &= configExtCC(ctx, ccLayer);
                break;
            case ovutils::OV_2D_TRUE_UI_MIRROR:
                UIMirrorOverlay::prepare(ctx, NULL);
                ret &= configExtVid(ctx, yuvLayer);
                ret &= configPrimVid(ctx, yuvLayer);
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

bool VideoOverlay::draw(hwc_context_t *ctx, hwc_layer_list_t *list)
{
    if(!sIsModeOn || sYuvLayerIndex == -1) {
        return true;
    }

    private_handle_t *hnd = (private_handle_t *)
            list->hwLayers[sYuvLayerIndex].handle;

    private_handle_t *cchnd = NULL;
    if(sCCLayerIndex != -1) {
        cchnd = (private_handle_t *)list->hwLayers[sCCLayerIndex].handle;
        ctx->qbuf->lockAndAdd(cchnd);
    }

    // Lock this buffer for read.
    ctx->qbuf->lockAndAdd(hnd);

    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);
    ovutils::eOverlayState state = ov.getState();

    switch (state) {
        case ovutils::OV_2D_VIDEO_ON_PANEL_TV:
            // Play external
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE1)) {
                ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                ret = false;
            }
            //Play CC on external
            if (cchnd && !ov.queueBuffer(cchnd->fd, cchnd->offset,
                        ovutils::OV_PIPE2)) {
                ALOGE("%s: queueBuffer failed for cc external", __FUNCTION__);
                ret = false;
            }
            // Play primary
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE0)) {
                ALOGE("%s: queueBuffer failed for primary", __FUNCTION__);
                ret = false;
            }
            break;
        case ovutils::OV_2D_VIDEO_ON_PANEL:
            // Play primary
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE0)) {
                ALOGE("%s: queueBuffer failed for primary", __FUNCTION__);
                ret = false;
            }
            break;
        case ovutils::OV_2D_VIDEO_ON_TV:
            // Play external
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE1)) {
                ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                ret = false;
            }
            //Play CC on external
            if (cchnd && !ov.queueBuffer(cchnd->fd, cchnd->offset,
                        ovutils::OV_PIPE2)) {
                ALOGE("%s: queueBuffer failed for cc external", __FUNCTION__);
                ret = false;
            }
            break;
        case ovutils::OV_2D_TRUE_UI_MIRROR:
            // Play external
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE1)) {
                ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                ret = false;
            }
            // Play primary
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE0)) {
                ALOGE("%s: queueBuffer failed for primary", __FUNCTION__);
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
