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
int VideoOverlay::sLayerS3DFormat = 0;

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
        if(sState == ovutils::OV_3D_VIDEO_ON_3D_PANEL) {
            //Need to mark UI layers for S3D composition
            markUILayersforS3DComposition(list);
        }
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

void VideoOverlay::markUILayersforS3DComposition(hwc_layer_list_t *list) {
    for (size_t i = 0; i < list->numHwLayers; i++) {

        if((int)i == (int)sYuvLayerIndex)
            continue;

        hwc_layer_t* layer = &(list->hwLayers[i]);
        if( (sLayerS3DFormat & HAL_3D_IN_SIDE_BY_SIDE_L_R) or
                (sLayerS3DFormat & HAL_3D_IN_SIDE_BY_SIDE_R_L) ) {
            ALOGD_IF(VIDEO_DEBUG,
            "Setting the hint HWC_HINT_DRAW_S3D_SIDE_BY_SIDE to SF");
            layer->hints |= HWC_HINT_DRAW_S3D_SIDE_BY_SIDE;
        } else if (sLayerS3DFormat & HAL_3D_IN_TOP_BOTTOM) {
            ALOGD_IF(VIDEO_DEBUG,
            "Setting the hint HWC_HINT_DRAW_S3D_TOP_BOTTOM to SF");
            layer->hints |= HWC_HINT_DRAW_S3D_TOP_BOTTOM;
        } else if (sLayerS3DFormat & HAL_3D_IN_FRAME_PACKING) {
            ALOGD_IF(VIDEO_DEBUG,
            "Setting the hint HWC_HINT_DRAW_S3D_TOP_BOTTOM to SF");
            layer->hints |= HWC_HINT_DRAW_S3D_TOP_BOTTOM;
        } else {
            ALOGE("Invalid 3D format %x",sLayerS3DFormat);
        }
    }
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
            if(sLayerS3DFormat) {
                // Initialize to the default state
                newState = ovutils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV;
                if(ovutils::is3DTV()) {
                    // Change the state if the device is connected to 3DTV.
                    newState = ovutils::OV_3D_VIDEO_ON_3D_TV;
                }
            } else if(trueMirrorSupported && (sCCLayerIndex == -1)) {
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
            if(sLayerS3DFormat) {
                // Initialize to the default state
                newState = ovutils::OV_3D_VIDEO_ON_2D_PANEL;
                if(ovutils::isPanel3D()) {
                    // Change the state if the Panel is 3D
                    newState = ovutils::OV_3D_VIDEO_ON_3D_PANEL;
                }
            }
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
        case ovutils::OV_3D_VIDEO_ON_3D_TV:
        case ovutils::OV_3D_VIDEO_ON_3D_PANEL:
        case ovutils::OV_3D_VIDEO_ON_2D_PANEL:
        case ovutils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
        case ovutils::OV_2D_TRUE_UI_MIRROR:
            layer->compositionType = HWC_OVERLAY;
            layer->hints |= HWC_HINT_CLEAR_FB;
            break;
        case ovutils::OV_2D_VIDEO_ON_TV:
            break; //dont update flags.
        default:
            break;
    }
}

bool VideoOverlay::configPrimVid(hwc_context_t *ctx, hwc_layer_t *layer) {
    private_handle_t *hnd = (private_handle_t *)layer->handle;

    bool isSecured = hnd->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER;

    overlay::Overlay& ov = *(ctx->mOverlay);
    ovutils::Whf info(hnd->width, hnd->height,
                      hnd->format, sLayerS3DFormat, hnd->size);

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
    if (metadata && (metadata->operation & PP_PARAM_INTERLACED) && metadata->interlaced) {
        ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_DEINTERLACE);
    }

    ovutils::eIsFg isFgFlag = ovutils::IS_FG_OFF;

    if (ctx->numHwLayers == 1) {
        isFgFlag = ovutils::IS_FG_SET;
    }

    if(metadata && (ctx->mPpParams[VIDEO_LAYER_0].isValid ||
        (metadata->operation & PP_PARAM_VID_INTFC))) {
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
        ov.setVisualParams(*metadata, ovutils::OV_PIPE0);
    }

    ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_PP_EN);
    //Ensure that VG pipe is allocated in cases where buffer-type
    //is video while format is RGB
    ovutils::setMdpFlags(mdpFlags,
            ovutils::OV_MDP_PIPE_SHARE);

    ovutils::PipeArgs parg(mdpFlags,
            info,
            ovutils::ZORDER_0,
            isFgFlag,
            ovutils::ROT_DOWNSCALE_ENABLED);
    ov.setSource(parg, ovutils::OV_PIPE0);

    if(metadata && ctx->mPpParams[VIDEO_LAYER_0].isValid){
        ovutils::clearMdpFlags(mdpFlags, ovutils::OV_MDP_PP_EN);
        /* Done with setting HSIC values. Clear the
         * PP_PARAM_HSIC and PP_PARAM_SHARPNESS flags
         * from metadata operation
         */
        metadata->operation &= ~(ctx->mPpParams[VIDEO_LAYER_0].ops);
    }

    hwc_rect_t sourceCrop = layer->sourceCrop;
    hwc_rect_t displayFrame = layer->displayFrame;
    set_ov_dimensions(ctx,VIDEO_LAYER_0,sourceCrop,displayFrame);

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
    }

    // source crop x,y,w,h
    ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
            sourceCrop.right - sourceCrop.left,
            sourceCrop.bottom - sourceCrop.top);
    //Only for Primary
    ov.setCrop(dcrop, ovutils::OV_PIPE0);
    ALOGD_IF(VIDEO_DEBUG,"Crop values set for the main video are %d %d %d %d",
            sourceCrop.left,sourceCrop.top,sourceCrop.right,sourceCrop.bottom);
    ALOGD_IF(VIDEO_DEBUG,
            "Destination position values for the main video are %d %d %d %d",
            displayFrame.left,displayFrame.top,displayFrame.right,displayFrame.bottom);

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

bool VideoOverlay::configExtVid(hwc_context_t *ctx, hwc_layer_t *layer) {
    overlay::Overlay& ov = *(ctx->mOverlay);
    private_handle_t *hnd = (private_handle_t *)layer->handle;

    ovutils::Whf info(hnd->width, hnd->height,
                      hnd->format, sLayerS3DFormat, hnd->size);

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

    //Ensure that VG pipe is allocated in cases where buffer-type
    //is video while format is RGB
    ovutils::setMdpFlags(mdpFlags,
            ovutils::OV_MDP_PIPE_SHARE);

    /* Set the metaData if any */
    if(!metadata)
        ALOGE("%s:NULL metadata!", __FUNCTION__);
    else
        ov.setVisualParams(*metadata, ovutils::OV_PIPE1);

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

bool VideoOverlay::configPrimVidS3D(hwc_context_t *ctx, hwc_layer_t *layer) {
    overlay::Overlay& ov = *(ctx->mOverlay);
    private_handle_t *hnd = (private_handle_t *)layer->handle;

    ovutils::Whf info(hnd->width, hnd->height,
                      hnd->format, sLayerS3DFormat, hnd->size);

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
    // Configure overlay channel 0 for left view
    ovutils::PipeArgs parg0(mdpFlags,
            info,
            ovutils::ZORDER_1,
            isFgFlag,
            ovutils::ROT_FLAGS_NONE);
    // Configure overlay channel 1 for right view
    ovutils::PipeArgs parg1(mdpFlags,
            info,
            ovutils::ZORDER_2,
            isFgFlag,
            ovutils::ROT_FLAGS_NONE);

    ov.setSource(parg0, ovutils::OV_PIPE0);
    ov.setSource(parg1, ovutils::OV_PIPE1);

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
        calculate_crop_rects(sourceCrop, displayFrame, fbWidth, fbHeight);
    }

    // source crop x,y,w,h
    ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
            sourceCrop.right - sourceCrop.left,
            sourceCrop.bottom - sourceCrop.top);
    //Only for Primary
    ov.setCrop(dcrop, ovutils::OV_PIPE0);
    ov.setCrop(dcrop, ovutils::OV_PIPE1);

    ovutils::eTransform orient =
            static_cast<ovutils::eTransform>(layer->transform);
    ov.setTransform(orient, ovutils::OV_PIPE0);
    ov.setTransform(orient, ovutils::OV_PIPE1);

    // position x,y,w,h
    ovutils::Dim dpos(displayFrame.left,
            displayFrame.top,
            displayFrame.right - displayFrame.left,
            displayFrame.bottom - displayFrame.top);
    ov.setPosition(dpos, ovutils::OV_PIPE0);
    ov.setPosition(dpos, ovutils::OV_PIPE1);

    if ((!ov.commit(ovutils::OV_PIPE0)) || (!ov.commit(ovutils::OV_PIPE1))) {
        ALOGE("%s: commit fails", __FUNCTION__);
        return false;
    }
    return true;
}

bool VideoOverlay::configExtVidS3D(hwc_context_t *ctx, hwc_layer_t *layer) {
    overlay::Overlay& ov = *(ctx->mOverlay);
    private_handle_t *hnd = (private_handle_t *)layer->handle;

    ovutils::Whf info(hnd->width, hnd->height,
                      hnd->format, sLayerS3DFormat, hnd->size);

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
    // Configure overlay channel 0 for left view
    ovutils::PipeArgs parg0(mdpFlags,
            info,
            ovutils::ZORDER_0,
            isFgFlag,
            ovutils::ROT_FLAGS_NONE);
    ovutils::PipeArgs parg1(mdpFlags,
            info,
            ovutils::ZORDER_1,
            isFgFlag,
            ovutils::ROT_FLAGS_NONE);

    ov.setSource(parg0, ovutils::OV_PIPE0);
    ov.setSource(parg1, ovutils::OV_PIPE1);

    hwc_rect_t sourceCrop = layer->sourceCrop;
    // x,y,w,h
    ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
            sourceCrop.right - sourceCrop.left,
            sourceCrop.bottom - sourceCrop.top);
    //Only for External
    ov.setCrop(dcrop, ovutils::OV_PIPE0);
    ov.setCrop(dcrop, ovutils::OV_PIPE1);

    // FIXME: Use source orientation for TV when source is portrait
    //Only for External
    ov.setTransform(layer->sourceTransform, ovutils::OV_PIPE0);
    ov.setTransform(layer->sourceTransform, ovutils::OV_PIPE1);

    ovutils::Dim dpos;
    hwc_rect_t displayFrame = layer->displayFrame;
    dpos.x = displayFrame.left;
    dpos.y = displayFrame.top;
    dpos.w = (displayFrame.right - displayFrame.left);
    dpos.h = (displayFrame.bottom - displayFrame.top);

    //Only for External
    ov.setPosition(dpos, ovutils::OV_PIPE0);
    ov.setPosition(dpos, ovutils::OV_PIPE1);

    if ((!ov.commit(ovutils::OV_PIPE0)) || (!ov.commit(ovutils::OV_PIPE1))) {
        ALOGE("%s: commit fails", __FUNCTION__);
        return false;
    }
    return true;
}

bool VideoOverlay::configExtCC(hwc_context_t *ctx, hwc_layer_t *layer) {
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
            case ovutils::OV_3D_VIDEO_ON_2D_PANEL:
                ret &= configPrimVid(ctx, yuvLayer);
                break;
            case ovutils::OV_2D_VIDEO_ON_PANEL_TV:
            case ovutils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
                ret &= configExtVid(ctx, yuvLayer);
                ret &= configExtCC(ctx, ccLayer);
                ret &= configPrimVid(ctx, yuvLayer);
                break;
            case ovutils::OV_2D_VIDEO_ON_TV:
                ret &= configExtVid(ctx, yuvLayer);
                ret &= configExtCC(ctx, ccLayer);
                break;
            case ovutils::OV_3D_VIDEO_ON_3D_PANEL:
                ret &= configPrimVidS3D(ctx,yuvLayer);
                break;
            case ovutils::OV_3D_VIDEO_ON_3D_TV:
                ret &= configExtVidS3D(ctx, yuvLayer);
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

    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;
    VideoFrame_t *frc;
    if (metadata && (metadata->operation & PP_PARAM_VIDEO_FRAME)) {
        ALOGD("VideoOverlay %s %lld %d", __FUNCTION__, metadata->videoFrame.timestamp, metadata->videoFrame.counter);
        frc = &metadata->videoFrame;
        frc->hwc_play_time = ns2us(systemTime());
    } else {
        frc = NULL;
    }

    switch (state) {
        case ovutils::OV_2D_VIDEO_ON_PANEL_TV:
        case ovutils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            // Play external
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE1, frc)) {
                ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                ret = false;
            }
            //Play CC on external
            if (cchnd && !ov.queueBuffer(cchnd->fd, cchnd->offset,
                        ovutils::OV_PIPE2, frc)) {
                ALOGE("%s: queueBuffer failed for cc external", __FUNCTION__);
                ret = false;
            }
            // Play primary
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE0, frc)) {
                ALOGE("%s: queueBuffer failed for primary", __FUNCTION__);
                ret = false;
            }
            break;
        case ovutils::OV_2D_VIDEO_ON_PANEL:
        case ovutils::OV_3D_VIDEO_ON_2D_PANEL:
            // Play primary
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE0, frc)) {
                ALOGE("%s: queueBuffer failed for primary", __FUNCTION__);
                ret = false;
            }
            break;
        case ovutils::OV_2D_VIDEO_ON_TV:
            // Play external
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE1, frc)) {
                ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                ret = false;
            }
            //Play CC on external
            if (cchnd && !ov.queueBuffer(cchnd->fd, cchnd->offset,
                        ovutils::OV_PIPE2, frc)) {
                ALOGE("%s: queueBuffer failed for cc external", __FUNCTION__);
                ret = false;
            }
            break;
        case ovutils::OV_3D_VIDEO_ON_3D_PANEL:
            // Play left view on primary
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE0, frc)) {
                ALOGE("%s: queueBuffer failed for left view external",
                        __FUNCTION__);
                ret = false;
            }
            // Play right view on primary
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE1, frc)) {
                ALOGE("%s: queueBuffer failed for right view external",
                                                            __FUNCTION__);
                ret = false;
            }
            break;

        case ovutils::OV_3D_VIDEO_ON_3D_TV:
            // Play left view on external
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE0, frc)) {
                ALOGE("%s: queueBuffer failed for left view external",
                                                            __FUNCTION__);
                ret = false;
            }
            // Play right view on external
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE1, frc)) {
                ALOGE("%s: queueBuffer failed for right view external",
                                                            __FUNCTION__);
            }
            break;
        case ovutils::OV_2D_TRUE_UI_MIRROR:
            // Play external
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE1, frc)) {
                ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                ret = false;
            }
            // Play primary
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE0, frc)) {
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
