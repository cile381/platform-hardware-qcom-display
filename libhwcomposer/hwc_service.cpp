/*
 *  Copyright (c) 2012, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
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

#include <hwc_service.h>
#include <hwc_utils.h>
#include <fb_priv.h>
#include <cutils/properties.h>

#define HWC_SERVICE_DEBUG 0

using namespace android;

namespace hwcService {

HWComposerService* HWComposerService::sHwcService = NULL;
// ----------------------------------------------------------------------------
HWComposerService::HWComposerService():mHwcContext(0),
                                       mMaxActionSafeWidth(0),
                                       mMaxActionSafeHeight(0)
{
    ALOGD_IF(HWC_SERVICE_DEBUG, "HWComposerService Constructor invoked");
    // Get ActionSafe Maximum Width and Height
    char property[PROPERTY_VALUE_MAX];
    if(property_get("persist.actionsafe.maxwidth", property, NULL) > 0) {
        mMaxActionSafeWidth = atoi(property);
    }
    if(property_get("persist.actionsafe.maxheight", property, NULL) > 0) {
        mMaxActionSafeHeight = atoi(property);
    }
}

HWComposerService::~HWComposerService()
{
    ALOGD_IF(HWC_SERVICE_DEBUG,"HWComposerService Destructor invoked");
}

status_t HWComposerService::setHPDStatus(int hpdStatus) {
    ALOGD_IF(HWC_SERVICE_DEBUG, "hpdStatus=%d", hpdStatus);
    qhwc::ExternalDisplay *externalDisplay = mHwcContext->mExtDisplay;
    externalDisplay->setHPDStatus(hpdStatus);
    return NO_ERROR;
}

status_t HWComposerService::setResolutionMode(int resMode) {
    ALOGD_IF(HWC_SERVICE_DEBUG, "resMode=%d", resMode);
    qhwc::ExternalDisplay *externalDisplay = mHwcContext->mExtDisplay;
    if(mHwcContext->externalDisplay) {
        externalDisplay->setEDIDMode(resMode);
    } else {
        ALOGE("External Display not connected");
    }
    return NO_ERROR;
}

status_t HWComposerService::setActionSafeDimension(int w, int h) {
    ALOGD_IF(HWC_SERVICE_DEBUG, "w=%d h=%d", w, h);
    qhwc::ExternalDisplay *externalDisplay = mHwcContext->mExtDisplay;
    if((w > mMaxActionSafeWidth) && (h > mMaxActionSafeHeight)) {
        ALOGE_IF(HWC_SERVICE_DEBUG,
            "ActionSafe Width and Height exceeded the limit! w=%d h=%d", w, h);
        return NO_ERROR;
    }
    if(mHwcContext->externalDisplay) {
        externalDisplay->setActionSafeDimension(w, h);
    } else {
        ALOGE("External Display not connected");
    }
    return NO_ERROR;
}
status_t HWComposerService::setOpenSecureStart( ) {
    mHwcContext->mSecureConfig = true;
    inValidate();
    return NO_ERROR;
}

status_t HWComposerService::setOpenSecureEnd( ) {
    mHwcContext->mSecure = true;
    mHwcContext->mSecureConfig = false;
    inValidate();
    return NO_ERROR;
}

status_t HWComposerService::setCloseSecureStart( ) {
    mHwcContext->mSecureConfig = true;
    inValidate();
    return NO_ERROR;
}

status_t HWComposerService::setCloseSecureEnd( ) {
    mHwcContext->mSecure = false;
    mHwcContext->mSecureConfig = false;
    inValidate();
    return NO_ERROR;
}

status_t HWComposerService::getResolutionModeCount(int *resModeCount) {
    qhwc::ExternalDisplay *externalDisplay = mHwcContext->mExtDisplay;
     if(mHwcContext->externalDisplay) {
        *resModeCount = externalDisplay->getModeCount();
    } else {
        ALOGE("External Display not connected");
    }
    ALOGD_IF(HWC_SERVICE_DEBUG, "resModeCount=%d", *resModeCount);
    return NO_ERROR;
}

status_t HWComposerService::getResolutionModes(int *resModes, int count) {
    qhwc::ExternalDisplay *externalDisplay = mHwcContext->mExtDisplay;
    if(mHwcContext->externalDisplay) {
        externalDisplay->getEDIDModes(resModes);
    } else {
        ALOGE("External Display not connected");
    }
    return NO_ERROR;
}

status_t HWComposerService::getExternalDisplay(int *dispType) {
    qhwc::ExternalDisplay *externalDisplay = mHwcContext->mExtDisplay;
    *dispType = mHwcContext->externalDisplay;
    ALOGD_IF(HWC_SERVICE_DEBUG, "dispType=%d", *dispType);
    return NO_ERROR;
}

status_t HWComposerService::setPPParams(qhwc::VideoPPData pParams,
        qhwc::PP_Video_Layer_Type numVideoLayer){
    if(numVideoLayer < PP_MAX_VG_PIPES) {
        pParams.isValid = true;
        mHwcContext->mPpParams[numVideoLayer]= pParams;
    }
    else{
        ALOGE("invalid layer type : %d",numVideoLayer);
        return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t HWComposerService::setPQCState(int value) {
     if((value == qhwc::PQC_START) || (value == qhwc::PQC_STOP)) {
        mHwcContext->mPQCState = value;
        //Invalidate
        hwc_procs* proc = (hwc_procs*)mHwcContext->device.reserved_proc[0];
        if(!proc) {
            ALOGE("%s: HWC proc not registered", __FUNCTION__);
            return false;
        } else {
            /* Trigger redraw */
            ALOGE("%s: HWC Invalidate!!", __FUNCTION__);
            proc->invalidate(proc);
            return true;
        }
    }
    else{
        ALOGE("invalid Value : %d",value);
        return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t HWComposerService::setOverScanCompensationParams(
        qhwc::OSRectDimensions oscparams) {
    mHwcContext->oscparams.set(oscparams);
    if(mHwcContext->oscparams.isValid)
        ALOGE_IF(HWC_SERVICE_DEBUG,"overscancompensation values are now set");
    else
        ALOGE_IF(HWC_SERVICE_DEBUG,"overscancompensation values are now unset");
    ALOGE_IF(HWC_SERVICE_DEBUG,
    "Setting the overscancompensation values as"
    "left %d, top %d, right %d, bottom %d",
            oscparams.left,oscparams.top,oscparams.right,oscparams.bottom);
    return NO_ERROR;
}

status_t HWComposerService::setOverScanParams(
        qhwc::PP_Video_Layer_Type numVideoLayer,
        qhwc::OSRectDimensions ossrcparams,
        qhwc::OSRectDimensions osdstparams) {

    if(numVideoLayer < PP_MAX_VG_PIPES) {
        mHwcContext->ossrcparams[numVideoLayer].set(ossrcparams);
        mHwcContext->osdstparams[numVideoLayer].set(osdstparams);
        if(mHwcContext->ossrcparams[numVideoLayer].isValid)
            ALOGE_IF(HWC_SERVICE_DEBUG,"overscan crop values are now set");
        else
            ALOGE_IF(HWC_SERVICE_DEBUG,"overscan crop values are now unset");

        if(mHwcContext->osdstparams[numVideoLayer].isValid)
            ALOGE_IF(HWC_SERVICE_DEBUG,"overscan dst values are now set");
        else
            ALOGE_IF(HWC_SERVICE_DEBUG,"overscan dst values are now unset");

        ALOGE_IF(HWC_SERVICE_DEBUG,
        "Setting the overscan values for video-layer %d",numVideoLayer);
        ALOGE_IF(HWC_SERVICE_DEBUG,
        "Setting the overscan crop values as %d %d %d %d",
                ossrcparams.left,ossrcparams.top,ossrcparams.right,
                ossrcparams.bottom);
        ALOGE_IF(HWC_SERVICE_DEBUG,
        "Setting the overscan dst values as %d %d %d %d",
                osdstparams.left,osdstparams.top,osdstparams.right,
                osdstparams.bottom);
    }
    else{
        ALOGE("invalid layer type : %d",numVideoLayer);
        return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t HWComposerService::startConfigChange(
        qhwc::CONFIG_CHANGE_TYPE configChangeType){

    ALOGE_IF(HWC_SERVICE_DEBUG," %s configChangeType %s",__FUNCTION__,
            qhwc::getConfigTypeString(configChangeType));

    switch(mHwcContext->mConfigChangeType) {
        case qhwc::NO_CORRECTION:
            /* No ConfigChanges are in progress. Start new ConfigChange */
            switch(configChangeType) {
                case qhwc::PIXEL_CLOCK_CORRECTION:
                case qhwc::PICTURE_QUALITY_CORRECTION:
                    ALOGE_IF(HWC_SERVICE_DEBUG,"%s: %s start", __FUNCTION__,
                            qhwc::getConfigTypeString(configChangeType));
                    mHwcContext->mConfigChangeType = configChangeType;
                    mHwcContext->mConfigChangeState->setState(
                            qhwc::CONFIG_CHANGE_START_BEGIN);
                    inValidate();
                    pthread_mutex_lock(&(mHwcContext->mConfigChangeLock));
                    while(mHwcContext->mConfigChangeState->getState() !=
                            qhwc::CONFIG_CHANGE_START_FINISH) {
                         pthread_cond_wait(&(mHwcContext->mConfigChangeCond),
                                 &(mHwcContext->mConfigChangeLock));
                    }
                    pthread_mutex_unlock(&(mHwcContext->mConfigChangeLock));
                    break;
                default:
                    ALOGE("%s: InValid configChangeType %s",__FUNCTION__,
                            qhwc::getConfigTypeString(configChangeType));
                    return BAD_VALUE;
            }
            break;
        case qhwc::PIXEL_CLOCK_CORRECTION:
        case qhwc::PICTURE_QUALITY_CORRECTION:
            ALOGE("%s: ConfigChange %s in progress."
                " Aborting new ConfigChange %s",__FUNCTION__,
                qhwc::getConfigTypeString(mHwcContext->mConfigChangeType),
                qhwc::getConfigTypeString(configChangeType));
            return BAD_VALUE;
        default:
            ALOGE("%s: unknown mconfigChangeType %s", __FUNCTION__,
                    qhwc::getConfigTypeString(mHwcContext->mConfigChangeType));
            return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t HWComposerService::ConfigChange(
        qhwc::CONFIG_CHANGE_TYPE configChangeType,
        qhwc::ConfigChangeParams params) {
    status_t ret = NO_ERROR;
    
    pthread_mutex_lock(&(mHwcContext->mConfigLock));
    ret = startConfigChange(configChangeType);
    ret = doConfigChange(configChangeType, params);
    ret = stopConfigChange(configChangeType);
    pthread_mutex_unlock(&(mHwcContext->mConfigLock));

    return ret;
}

status_t HWComposerService::doConfigChange(
        qhwc::CONFIG_CHANGE_TYPE configChangeType,
        qhwc::ConfigChangeParams params) {

    ALOGE_IF(HWC_SERVICE_DEBUG," %s configChangeType %s params are %f %f",
            __FUNCTION__, qhwc::getConfigTypeString(configChangeType),
            params.param1,params.param2);

    switch(mHwcContext->mConfigChangeType) {
        case qhwc::NO_CORRECTION:
            ALOGE("%s: No configChange in progress. Cannot do %s",
                    __FUNCTION__,qhwc::getConfigTypeString(configChangeType));
            return BAD_VALUE;
        case qhwc::PIXEL_CLOCK_CORRECTION:
        case qhwc::PICTURE_QUALITY_CORRECTION:
            if(mHwcContext->mConfigChangeType == configChangeType){
                ALOGE_IF(HWC_SERVICE_DEBUG,"%s: %s do", __FUNCTION__,
                        qhwc::getConfigTypeString(configChangeType));
                mHwcContext->mConfigChangeParams = params;
                mHwcContext->mConfigChangeState->setState(
                        qhwc::CONFIG_CHANGE_DO_BEGIN);
                inValidate();
                pthread_mutex_lock(&(mHwcContext->mConfigChangeLock));
                while(mHwcContext->mConfigChangeState->getState() !=
                        qhwc::CONFIG_CHANGE_DO_FINISH) {
                    pthread_cond_wait(&(mHwcContext->mConfigChangeCond),
                            &(mHwcContext->mConfigChangeLock));
                }
                pthread_mutex_unlock(&(mHwcContext->mConfigChangeLock));
            } else {
                ALOGE("%s: ConfigChange %s in progress."
                        " Aborting new ConfigChange %s",__FUNCTION__,
                        qhwc::getConfigTypeString(mHwcContext->mConfigChangeType),
                        qhwc::getConfigTypeString(configChangeType));
                return BAD_VALUE;
            }
            break;
        default:
            ALOGE("%s: unknown mconfigChangeType %s", __FUNCTION__,
                    qhwc::getConfigTypeString(mHwcContext->mConfigChangeType));
            return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t HWComposerService::stopConfigChange(
        qhwc::CONFIG_CHANGE_TYPE configChangeType) {

    ALOGE_IF(HWC_SERVICE_DEBUG," %s configChangeType %s",__FUNCTION__,
            qhwc::getConfigTypeString(configChangeType));

    switch(mHwcContext->mConfigChangeType) {
        case qhwc::NO_CORRECTION:
            ALOGE("%s: No configChange in progress. Cannot stop %s",
                    __FUNCTION__,qhwc::getConfigTypeString(configChangeType));
            return BAD_VALUE;
        case qhwc::PIXEL_CLOCK_CORRECTION:
        case qhwc::PICTURE_QUALITY_CORRECTION:
            switch(configChangeType) {
                case qhwc::PIXEL_CLOCK_CORRECTION:
                case qhwc::PICTURE_QUALITY_CORRECTION:
                    ALOGE_IF(HWC_SERVICE_DEBUG,"%s: %s stop", __FUNCTION__,
                            qhwc::getConfigTypeString(configChangeType));
                    mHwcContext->mConfigChangeType = qhwc::NO_CORRECTION;
                    mHwcContext->mConfigChangeState->setState(
                             qhwc::CONFIG_CHANGE_STOP_BEGIN);
                    inValidate();
                    pthread_mutex_lock(&(mHwcContext->mConfigChangeLock));
                    while(mHwcContext->mConfigChangeState->getState() !=
                            qhwc::CONFIG_CHANGE_STOP_FINISH) {
                        pthread_cond_wait(&(mHwcContext->mConfigChangeCond),
                                &(mHwcContext->mConfigChangeLock));
                    }
                    pthread_mutex_unlock(&(mHwcContext->mConfigChangeLock));
                    break;
                default:
                    ALOGE("%s: InValid configChangeType %s",__FUNCTION__,
                            qhwc::getConfigTypeString(configChangeType));
                    return BAD_VALUE;
            }
            break;
        default:
            ALOGE("%s: unknown mconfigChangeType %s", __FUNCTION__,
                    qhwc::getConfigTypeString(mHwcContext->mConfigChangeType));
            return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t HWComposerService::getStdFrameratePixclock(
        qhwc::ConfigChangeParams *params) {
    framebuffer_device_t *fbDev = mHwcContext->mFbDev;
    if(fbDev) {
        int num_channels = 1;
        private_module_t* m = reinterpret_cast<private_module_t*>(
                mHwcContext->mFbDev->common.module);
        if(qhwc::isPanelLVDS(0)){
            num_channels = 2;
        }
        params->param1 = m->default_framerate * num_channels;
        params->param2 = m->default_pixclock * num_channels;
        ALOGE_IF(HWC_SERVICE_DEBUG,"%s: Std framerate is %f and "
                "Std pixclock is %f",__FUNCTION__,params->param1,
                params->param2);
        return NO_ERROR;
    }
    return BAD_VALUE;
}

status_t HWComposerService::getCurrentFrameratePixclock(
        qhwc::ConfigChangeParams *params) {
    framebuffer_device_t *fbDev = mHwcContext->mFbDev;
    if(fbDev) {
        int num_channels = 1;
        private_module_t* m = reinterpret_cast<private_module_t*>(
                mHwcContext->mFbDev->common.module);
        struct fb_var_screeninfo info;
        int ret = 0;
        if(qhwc::isPanelLVDS(0)){
            num_channels = 2;
        }
        ret = ioctl(m->framebuffer->fd, FBIOGET_VSCREENINFO, &info);
        if(ret < 0) {
            ALOGE("In %s: FBIOGET_VSCREENINFO failed Err Str = %s", __FUNCTION__,
                    strerror(errno));
            return BAD_VALUE;
        }
        params->param1 = info.reserved[4] * num_channels;
        params->param2 = info.pixclock * num_channels;
        ALOGE_IF(HWC_SERVICE_DEBUG,"%s: Current framerate is %f and"
                "Current pixclock is %f",__FUNCTION__,params->param1,
                params->param2);
        return NO_ERROR;
    }
    return BAD_VALUE;
}

HWComposerService* HWComposerService::getInstance()
{
    if(!sHwcService) {
        sHwcService = new HWComposerService();
        sp<IServiceManager> sm = defaultServiceManager();
        sm->addService(String16("display.hwcservice"), sHwcService);
        if(sm->checkService(String16("display.hwcservice")) != NULL)
            ALOGD_IF(HWC_SERVICE_DEBUG, "adding display.hwcservice succeeded");
        else
            ALOGD_IF(HWC_SERVICE_DEBUG, "adding display.hwcservice failed");
    }
    return sHwcService;
}

void HWComposerService::inValidate() {
    //Invalidate
    hwc_procs* proc = (hwc_procs*)mHwcContext->device.reserved_proc[0];
    if(!proc) {
        ALOGE("%s: HWC proc not registered", __FUNCTION__);
    } else {
        /* Trigger redraw */
        ALOGD_IF(HWC_SERVICE_DEBUG, "%s: Invalidate !!", __FUNCTION__);
        proc->invalidate(proc);
    }
}

void HWComposerService::setHwcContext(hwc_context_t *hwcCtx) {
    ALOGD_IF(HWC_SERVICE_DEBUG, "hwcCtx=0x%x", (int)hwcCtx);
    if(hwcCtx) {
        mHwcContext = hwcCtx;
    }
}

}
