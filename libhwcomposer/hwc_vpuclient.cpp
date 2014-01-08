/*
 * Copyright (c) 2013-2014 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name of The Linux Foundation. nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <dlfcn.h>
#include "hwc_vpuclient.h"
#include <vpu/vpu.h>
#include <binder/Parcel.h>
#include "hwc_mdpcomp.h"

using namespace vpu;
using namespace android;
using namespace overlay::utils;
namespace ovutils = overlay::utils;

namespace qhwc {

#define MAX_PIPES_PER_LAYER 2

VPUClient::VPUClient(hwc_context_t *ctx)
{
    mVPULib = dlopen("libvpu.so", RTLD_NOW);
    VPU* (*getObject)();
    *(void **) &getObject =  dlsym(mVPULib, "getObject");

    mVPU = NULL;
    if(getObject) {
        mVPU = getObject();
        ALOGI("Initializing VPU client..");

       // calling vpu init
        if(mVPU->init() == NO_ERROR) {
            // passing display attributes to libvpu
            DispAttr_t attr;
            attr.width = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
            attr.height = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres;
            attr.fp100s =(ctx->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period)?
              1000000000/(ctx->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period/100):0;
            mVPU->setDisplayAttr((DISPLAY_ID)HWC_DISPLAY_PRIMARY, attr);
        }
        else {
            ALOGE("Error: in libvpu init");
            mVPU = NULL;
        }
        // memsetting the pipe structure to 0
        memset(mProp, 0, sizeof(mProp));

        // enable logs
        char property[PROPERTY_VALUE_MAX];
        if( property_get("persist.vpuclient.logs", property, NULL) > 0 )
            mDebugLogs = atoi(property);
    }
}

VPUClient::~VPUClient()
{
    void (*destroy) (VPU*);
    *(void **) &destroy = dlsym(mVPULib, "deleteObject");
    dlclose(mVPULib);
}

void setLayer(hwc_layer_1_t *layer, Layer *vLayer)
{
    // setting handle
    vLayer->handle = (private_handle_t *)(layer->handle);

    //setting source stride
    if(vLayer->handle) {
        vLayer->srcStride.width = getWidth(vLayer->handle);
        vLayer->srcStride.height = getHeight(vLayer->handle);
    }

    // setting source crop
    hwc_rect_t sourceRect = integerizeSourceCrop(layer->sourceCropf);
    vLayer->srcRect.left = sourceRect.left;
    vLayer->srcRect.top  = sourceRect.top;
    vLayer->srcRect.right = sourceRect.right;
    vLayer->srcRect.bottom = sourceRect.bottom;

    // setting destination crop
    vLayer->tgtRect.left = layer->displayFrame.left;
    vLayer->tgtRect.top = layer->displayFrame.top;
    vLayer->tgtRect.right = layer->displayFrame.right;
    vLayer->tgtRect.bottom = layer->displayFrame.bottom;

    if(layer->flags & HWC_GEOMETRY_CHANGED)
        vLayer->inFlags |= GEOMETRY_CHANGED;

    vLayer->acquireFenceFd = layer->acquireFenceFd;

    if(layer->compositionType == HWC_FRAMEBUFFER_TARGET || isSkipLayer(layer))
        vLayer->inFlags |= SKIP_LAYER;
}

int VPUClient::setupVpuSession(hwc_context_t *ctx, int display,
                                            hwc_display_contents_1_t* list)
{
    int err = 0;

    if(!mVPU) {
        err = -1;
        return err;
    }

    ALOGD_IF(isDebug2(), "%s: IN ", __FUNCTION__);
    LayerList vList[HWC_NUM_DISPLAY_TYPES];
    memset(vList, 0, sizeof(vList));
    memset(mProp, 0, sizeof(mProp));

    // setting up the layer
    LayerList *vpuList = &vList[display];
    vpuList->numLayers = list->numHwLayers;
    for(unsigned int i=0; i<(list->numHwLayers); ++i) {
        hwc_layer_1_t *layer = &list->hwLayers[i];
        Layer *vLayer = &vpuList->layers[i];
        VpuLayerProp* prop = &mProp[display][i];

        // Storing the sourceCropf, as it's going to be changed for overlay Set
        // will be restored after MDPComp prepare.
        prop->sourceCropf = layer->sourceCropf;

        // filling up the vpu list
        setLayer(layer, vLayer);
    }

    if(mVPU->setupVpuSession((DISPLAY_ID)display, vpuList) != NO_ERROR) {
        //error in vpu prepare
        err = -1;
        ALOGE("%s: ERROR in VPU::setupVpuSession", __func__);
        return err;
    }

    LayerProp *layerProp = ctx->layerProp[display];

    // check if the pipeID are already set, then will need to ensure that those
    // pipes are reserved in MDP
    for(unsigned int i=0; i<(vpuList->numLayers); ++i) {
        hwc_layer_1_t *layer = &list->hwLayers[i];
        Layer *vLayer = &vpuList->layers[i];
        VpuLayerProp* prop = &mProp[display][i];

        if(vLayer->outFlags & VPU_LAYER) {
            if((vLayer->outFlags & RESERVE_PREV_PIPES) &&
                                            vLayer->sDestPipes.numPipes > 0) {
                prop->pipeCount = vLayer->sDestPipes.numPipes;
                if(prop->pipeCount == 1)
                    setPipeId(display, i, vLayer->sDestPipes.pipe[0]);
                else if(prop->pipeCount == 2)
                    setPipeId(display, i, vLayer->sDestPipes.pipe[0],
                                                    vLayer->sDestPipes.pipe[1]);
                else
                    ALOGE("%s: Invalid num of pipes set reservation", __func__);
            }

            // marking the layer pipes for vpu.
            prop->vpuPipe = true;
            layer->flags |= HWC_VPU_PIPE;

            // getting image width and height
            prop->width = layer->displayFrame.right - layer->displayFrame.left;
            prop->height = layer->displayFrame.bottom - layer->displayFrame.top;

            //setting source crop = dest crop (only for layers drawn by vpu,
            // since we know it will be scaled up/down by vpu)
            layer->sourceCropf.left = 0.0;
            layer->sourceCropf.top = 0.0;
            layer->sourceCropf.right = (float) prop->width;
            layer->sourceCropf.bottom = (float) prop->height;

            // setting the flag so that mdpComp wont recognize it as the MDPCOMP
            layerProp[i].mFlags |= HWC_VPUCOMP;

            // setting up so that mdp draws black color for vpu layers
            // FIX ME: need to get the proper solution for color fill
            /* layer->flags |= HWC_COLOR_FILL; */
            /* layer->transform = 0; */

            // storing locally the vpu supported format from VFM
            prop->format = vLayer->vpuOutPixFmt;

            // Dummy buffer for the first frame of the video
            if(!(vLayer->outFlags & RESERVE_PREV_PIPES )) {
                // debug buffer for the first video frame
                if(mHnd != NULL)
                    free_buffer(mHnd);

                // TO-FIX: out dummy buffer is currently allocated based on
                // RGB888 format
                err = alloc_buffer(&mHnd, prop->width, prop->height,
                    HAL_PIXEL_FORMAT_RGB_888, GRALLOC_USAGE_PRIVATE_IOMMU_HEAP);

                if(err == -1)
                    ALOGE("%s: Debug buffer allocation failed!", __FUNCTION__);

                if(prop->format == HAL_PIXEL_FORMAT_RGB_888)
                    memset((void*)mHnd->base, 0x0, mHnd->size);
                else
                    memset((void*)mHnd->base, 0xaa, mHnd->size);

                prop->firstBuffer = true;
            }
        }
    }

    ALOGD_IF(isDebug2(), "%s: OUT", __FUNCTION__);
    return err;
}

int VPUClient::prepare(hwc_context_t *ctx, int display,
                       hwc_display_contents_1_t* list)
{
    int err = 0;
    if(!mVPU) {
        err = -1;
        return err;
    }

    ALOGD_IF(isDebug2(), "%s: IN", __FUNCTION__);
    LayerList vList[HWC_NUM_DISPLAY_TYPES];
    memset(vList, 0, sizeof(vList));

    LayerProp *layerProp = ctx->layerProp[display];

    // setting up the layer
    LayerList *vpuList = &vList[display];
    vpuList->numLayers = list->numHwLayers;

    // vpu layers marking is done in setVpuSession, but at that point, there is
    // no allocated pipe for the layer. At this point (in prepare, after
    // mdpComp-prepare), the pipe allocation is already done. Therefore, ending
    // all the sessions that were started in the last iteration, so that they
    // could get garbage-collected. If they are still valid, they will be marked
    // again as started in the for-loop below.
    ctx->mOverlay->endAllSessions();

    for(unsigned int i=0; i<(list->numHwLayers); ++i) {
        VpuLayerProp* prop = &mProp[display][i];
        if(!prop->vpuPipe)
            continue;

        hwc_layer_1_t *layer = &list->hwLayers[i];
        Layer *vLayer = &vpuList->layers[i];

        // re-storing the sourceCropf, as it was changed in setVpuSession for
        // overlay set
        layer->sourceCropf = prop->sourceCropf;

        // filling up the vpu list
        setLayer(layer, vLayer);

        // Getting the pipe ids from MDP.
        if( prop->pipeCount > 0  && prop->pipeCount <= MAX_PIPES_PER_LAYER ) {
            vLayer->sDestPipes.numPipes = prop->pipeCount;

            for(int j=0; j < prop->pipeCount; ++j) {
                vLayer->sDestPipes.pipe[j] = prop->pipeID[j];

                ovutils::eDest dest = ctx->mOverlay->getDest(prop->pipeID[j]);
                if(prop->vpuPipe)
                    ctx->mOverlay->startSession(dest);
            }
        }
    }

    if(mVPU->prepare((DISPLAY_ID)display, vpuList) != NO_ERROR) {
        //error in vpu prepare
        err = -1;
        ALOGE("%s: ERROR in VPU::prepare", __func__);
        return err;
    }

    ALOGD_IF(isDebug2(), "%s: OUT", __FUNCTION__);
    return err;
}

int VPUClient::draw(hwc_context_t *ctx, int display,
                                        hwc_display_contents_1_t* list)
{
    int err = 0;
    if(!mVPU) {
        err = -1;
        return err;
    }

    ALOGD_IF(isDebug2(), "%s: IN", __FUNCTION__);
    LayerList vList[HWC_NUM_DISPLAY_TYPES];
    memset(vList, 0, sizeof(vList));
    LayerList *vpuList = &vList[display];
    vpuList->numLayers = list->numHwLayers;

    for(unsigned int i=0; i<(list->numHwLayers); ++i) {
        hwc_layer_1_t *layer = &list->hwLayers[i];
        Layer *vLayer = &vpuList->layers[i];

        // filling up the vpu list
        setLayer(layer, vLayer);
    }

    if(mVPU->draw((DISPLAY_ID)display, vpuList) != NO_ERROR) {
        //error in vpu draw
        err = -1;
        ALOGE("%s: ERROR in VPU::draw", __func__);
        return err;
    }

    LayerProp *layerProp = ctx->layerProp[display];
    // setting releaseFenceFd for the vpu layer
    for(unsigned int i=0; i<(vpuList->numLayers); ++i) {

        VpuLayerProp* prop = &mProp[display][i];
        if(!prop->vpuPipe)
            continue;

        hwc_layer_1_t *layer = &list->hwLayers[i];
        Layer *vLayer = &vpuList->layers[i];

        /* // if not VPU layer then not setting releaseFenceFd */
        /* if(!(layerProp[i].mFlags & HWC_VPUCOMP)) */
        /*     continue; */

        layer->releaseFenceFd = vLayer->releaseFenceFd;
    }

    ALOGD_IF(isDebug2(), "%s: OUT", __FUNCTION__);
    return err;
}

int VPUClient::getPipeId(int dpy, int layer, int pipenum)
{
    int err = 0;
    if(!mVPU) {
        err = -1;
        return err;
    }

    VpuLayerProp* prop = &mProp[dpy][layer];
    return (prop->pipeCount > 0)?(prop->pipeID[pipenum]):-1;
}

int VPUClient::getLayerFormat(int dpy, int layer)
{
    int err = 0;
    if(!mVPU) {
        err = -1;
        return err;
    }

    VpuLayerProp* prop = &mProp[dpy][layer];
    return prop->format;
}

int VPUClient::getWidth(int dpy, int layer)
{
    int err = 0;
    if(!mVPU) {
        err = -1;
        return err;
    }

    VpuLayerProp* prop = &mProp[dpy][layer];
    return prop->width;
}

int VPUClient::getHeight(int dpy, int layer)
{
    int err = 0;
    if(!mVPU) {
        err = -1;
        return err;
    }

    VpuLayerProp* prop = &mProp[dpy][layer];
    return prop->height;
}

int VPUClient::setPipeId(int dpy, int layer, int lPipe, int rPipe)
{
    int err = 0;
    if(!mVPU) {
        err =-1;
        return err;
    }

    VpuLayerProp* prop = &mProp[dpy][layer];

    prop->pipeCount = 2;
    prop->pipeID[0] = lPipe;
    prop->pipeID[1] = rPipe;

    return err;
}

int VPUClient::setPipeId(int dpy, int layer, int pipeId)
{
    int err = 0;
    if(!mVPU) {
        err =-1;
        return err;
    }

    VpuLayerProp* prop = &mProp[dpy][layer];

    prop->pipeCount = 1;
    prop->pipeID[0] = pipeId;

    return err;
}

bool VPUClient::supportedVPULayer(hwc_context_t* ctx, hwc_layer_1_t* layer,
                                                            int dpy, int idx)
{
    if(!mVPU)
        return false;

    VpuLayerProp* prop = &mProp[dpy][idx];
    if( !prop->vpuPipe )
        return false;

    return true;
}

int VPUClient::processCommand(uint32_t command,
                              const Parcel* inParcel, Parcel* outParcel)
{
    if(!mVPU)
        return 0;
    return mVPU->processCommand(command, inParcel, outParcel);
}

}; // namespace qhwc
