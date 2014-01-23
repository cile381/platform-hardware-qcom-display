/*
 *  Copyright (c) 2013, The Linux Foundation. All rights reserved.
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
 * SUBSTITUTE GOODS OR CLIENTS; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <hwc_qclient.h>
#include <IQService.h>
#include <hwc_utils.h>
#include <external.h>

#define QCLIENT_DEBUG 0

using namespace android;
using namespace qService;
using namespace qhwc;

namespace qClient {

// ----------------------------------------------------------------------------
QClient::QClient(hwc_context_t *ctx) : mHwcContext(ctx),
        mMPDeathNotifier(new MPDeathNotifier(ctx))
{
    ALOGD_IF(QCLIENT_DEBUG, "QClient Constructor invoked");
}

QClient::~QClient()
{
    ALOGD_IF(QCLIENT_DEBUG,"QClient Destructor invoked");
}

status_t QClient::notifyCallback(uint32_t msg, uint32_t value) {
    switch(msg) {
        case IQService::SECURING:
            securing(value);
            break;
        case IQService::UNSECURING:
            unsecuring(value);
            break;
        case IQService::SCREEN_REFRESH:
            return screenRefresh();
            break;
        case IQService::EXTERNAL_ORIENTATION:
            setExtOrientation(value);
            break;
        case IQService::BUFFER_MIRRORMODE:
            setBufferMirrorMode(value);
            break;
        case IQService::SET_MODE:
            return setMode(value);
            break;
        default:
            return NO_ERROR;
    }
    return NO_ERROR;
}

status_t QClient::getStdFrameratePixclock(
        ConfigChangeParams *params) {
    int num_channels = 1;
    if(isPanelLVDS(0)){
        num_channels = 2;
    }
    params->param1 = mHwcContext->default_framerate * num_channels;
    params->param2 = mHwcContext->default_pixclock * num_channels;
    ALOGE_IF(QCLIENT_DEBUG,"%s: Std framerate is %f and "
            "Std pixclock is %f",__FUNCTION__,params->param1,
            params->param2);

    return NO_ERROR;
}

status_t QClient::getCurrentFrameratePixclock(
        ConfigChangeParams *params) {
    struct fb_var_screeninfo info;
    int num_channels = 1;
    int fb_fd = -1;
    const char *devtmpl = "/dev/graphics/fb%u";
    char name[64] = {0};
    snprintf(name, 64, devtmpl, HWC_DISPLAY_PRIMARY);
    fb_fd = open(name, O_RDWR);

    if(fb_fd < 0) {
        ALOGE("%s: Error Opening FB : %s", __FUNCTION__, strerror(errno));
        return -errno;
    }

    if(isPanelLVDS(0)){
        num_channels = 2;
    }

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("%s:Error in ioctl FBIOGET_VSCREENINFO: %s", __FUNCTION__,
                strerror(errno));
        close(fb_fd);
        return -errno;
    }
    params->param1 = info.reserved[3] * num_channels;
    params->param2 = info.pixclock * num_channels;
    ALOGE_IF(QCLIENT_DEBUG,"%s: Current framerate is %f and"
            "Current pixclock is %f",__FUNCTION__,params->param1,
            params->param2);

    return NO_ERROR;
}

status_t QClient::setOverScanParams(
        PP_Video_Layer_Type numVideoLayer,
        OSRectDimensions ossrcparams,
        OSRectDimensions osdstparams) {

    if(numVideoLayer < PP_MAX_VG_PIPES) {
        mHwcContext->ossrcparams[numVideoLayer].set(ossrcparams);
        mHwcContext->osdstparams[numVideoLayer].set(osdstparams);
    }
    else{
        ALOGE("invalid layer type : %d",numVideoLayer);
        return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t QClient::setOverScanCompensationParams(
        OSRectDimensions oscparams) {
    mHwcContext->oscparams.set(oscparams);
    return NO_ERROR;
}

status_t QClient::startConfigChange(
        CONFIG_CHANGE_TYPE configChangeType){

    ALOGE_IF(QCLIENT_DEBUG," %s configChangeType %s",__FUNCTION__,
            getConfigTypeString(configChangeType));

        switch(mHwcContext->mConfigChangeType) {
        case NO_CORRECTION:
            /* No ConfigChanges are in progress. Start new ConfigChange */
            switch(configChangeType) {
                case PIXEL_CLOCK_CORRECTION:
                case PICTURE_QUALITY_CORRECTION:
                    ALOGE_IF(QCLIENT_DEBUG,"%s: %s start", __FUNCTION__,
                            getConfigTypeString(configChangeType));
                    mHwcContext->mConfigChangeType = configChangeType;
                    mHwcContext->mConfigChangeState->setState(
                            CONFIG_CHANGE_START_BEGIN);
                    inValidate();
            pthread_mutex_lock(&(mHwcContext->mConfigChangeLock));
            while(mHwcContext->mConfigChangeState->getState() !=
                    CONFIG_CHANGE_START_FINISH) {
                pthread_cond_wait(&(mHwcContext->mConfigChangeCond),
                        &(mHwcContext->mConfigChangeLock));
            }
            pthread_mutex_unlock(&(mHwcContext->mConfigChangeLock));
                    break;
                default:
                    ALOGE("%s: InValid configChangeType %s",__FUNCTION__,
                            getConfigTypeString(configChangeType));
                    return BAD_VALUE;
            }
            break;
        case PIXEL_CLOCK_CORRECTION:
        case PICTURE_QUALITY_CORRECTION:
            ALOGE("%s: ConfigChange %s in progress."
                " Aborting new ConfigChange %s",__FUNCTION__,
                getConfigTypeString(mHwcContext->mConfigChangeType),
                getConfigTypeString(configChangeType));
            return BAD_VALUE;
        default:
            ALOGE("%s: unknown mconfigChangeType %s", __FUNCTION__,
                    getConfigTypeString(mHwcContext->mConfigChangeType));
            return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t QClient::doConfigChange(
        CONFIG_CHANGE_TYPE configChangeType,
        ConfigChangeParams params) {

    ALOGE_IF(QCLIENT_DEBUG," %s configChangeType %s params are %f %f",
            __FUNCTION__, getConfigTypeString(configChangeType),
            params.param1,params.param2);
        switch(mHwcContext->mConfigChangeType) {
        case NO_CORRECTION:
            ALOGE("%s: No configChange in progress. Cannot do %s",
                    __FUNCTION__,getConfigTypeString(configChangeType));
            return BAD_VALUE;
        case PIXEL_CLOCK_CORRECTION:
        case PICTURE_QUALITY_CORRECTION:
            if(mHwcContext->mConfigChangeType == configChangeType){
                ALOGE_IF(QCLIENT_DEBUG,"%s: %s do", __FUNCTION__,
                        getConfigTypeString(configChangeType));
                mHwcContext->mConfigChangeParams = params;
                mHwcContext->mConfigChangeState->setState(
                        CONFIG_CHANGE_DO_BEGIN);
                inValidate();
        pthread_mutex_lock(&(mHwcContext->mConfigChangeLock));
        while(mHwcContext->mConfigChangeState->getState() !=
                CONFIG_CHANGE_DO_FINISH) {
            pthread_cond_wait(&(mHwcContext->mConfigChangeCond),
                    &(mHwcContext->mConfigChangeLock));
        }
        pthread_mutex_unlock(&(mHwcContext->mConfigChangeLock));
            } else {
                ALOGE("%s: ConfigChange %s in progress."
                        " Aborting new ConfigChange %s",__FUNCTION__,
                        getConfigTypeString(mHwcContext->mConfigChangeType),
                        getConfigTypeString(configChangeType));
                return BAD_VALUE;
            }
            break;
        default:
            ALOGE("%s: unknown mconfigChangeType %s", __FUNCTION__,
                    getConfigTypeString(mHwcContext->mConfigChangeType));
            return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t QClient::stopConfigChange(
        CONFIG_CHANGE_TYPE configChangeType) {

    ALOGE_IF(QCLIENT_DEBUG," %s configChangeType %s",__FUNCTION__,
            getConfigTypeString(configChangeType));

    switch(mHwcContext->mConfigChangeType) {
        case NO_CORRECTION:
            ALOGE("%s: No configChange in progress. Cannot stop %s",
                    __FUNCTION__,getConfigTypeString(configChangeType));
            return BAD_VALUE;
        case PIXEL_CLOCK_CORRECTION:
        case PICTURE_QUALITY_CORRECTION:
            switch(configChangeType) {
                case PIXEL_CLOCK_CORRECTION:
                case PICTURE_QUALITY_CORRECTION:
                    ALOGE_IF(QCLIENT_DEBUG,"%s: %s stop", __FUNCTION__,
                            getConfigTypeString(configChangeType));
                    mHwcContext->mConfigChangeType = NO_CORRECTION;
                    mHwcContext->mConfigChangeState->setState(
                             CONFIG_CHANGE_STOP_BEGIN);
                    inValidate();
            pthread_mutex_lock(&(mHwcContext->mConfigChangeLock));
            while(mHwcContext->mConfigChangeState->getState() !=
                    CONFIG_CHANGE_STOP_FINISH) {
                pthread_cond_wait(&(mHwcContext->mConfigChangeCond),
                        &(mHwcContext->mConfigChangeLock));
            }
            pthread_mutex_unlock(&(mHwcContext->mConfigChangeLock));
                    break;
                default:
                    ALOGE("%s: InValid configChangeType %s",__FUNCTION__,
                            getConfigTypeString(configChangeType));
                    return BAD_VALUE;
            }
            break;
        default:
            ALOGE("%s: unknown mconfigChangeType %s", __FUNCTION__,
                    getConfigTypeString(mHwcContext->mConfigChangeType));
            return BAD_VALUE;
    }

    return NO_ERROR;
}

status_t QClient::setPPParams(VideoPPData pParams,
        PP_Video_Layer_Type numVideoLayer){
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

status_t QClient::setPQCState(int value) {
    if((value == PQC_START) || (value == qhwc::PQC_STOP)) {
        mHwcContext->mPQCState = value;
        //Invalidate
        hwc_procs* proc = (hwc_procs*)mHwcContext->proc;
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

void QClient::inValidate() {
    //Invalidate
    hwc_procs* proc = (hwc_procs*)mHwcContext->proc;
    if(!proc) {
        ALOGE("%s: HWC proc not registered", __FUNCTION__);
    } else {
        /* Trigger redraw */
        ALOGD_IF(QCLIENT_DEBUG, "%s: Invalidate !!", __FUNCTION__);
        proc->invalidate(proc);
    }
}

void QClient::securing(uint32_t startEnd) {
    Locker::Autolock _sl(mHwcContext->mDrawLock);
    //The only way to make this class in this process subscribe to media
    //player's death.
    IMediaDeathNotifier::getMediaPlayerService();

    mHwcContext->mSecuring = startEnd;
    //We're done securing
    if(startEnd == IQService::END)
        mHwcContext->mSecureMode = true;
    if(mHwcContext->proc)
        mHwcContext->proc->invalidate(mHwcContext->proc);
}

void QClient::unsecuring(uint32_t startEnd) {
    Locker::Autolock _sl(mHwcContext->mDrawLock);
    mHwcContext->mSecuring = startEnd;
    //We're done unsecuring
    if(startEnd == IQService::END)
        mHwcContext->mSecureMode = false;
    if(mHwcContext->proc)
        mHwcContext->proc->invalidate(mHwcContext->proc);
}

void QClient::MPDeathNotifier::died() {
    Locker::Autolock _sl(mHwcContext->mDrawLock);
    ALOGD_IF(QCLIENT_DEBUG, "Media Player died");
    mHwcContext->mSecuring = false;
    mHwcContext->mSecureMode = false;
    if(mHwcContext->proc)
        mHwcContext->proc->invalidate(mHwcContext->proc);
}

android::status_t QClient::screenRefresh() {
    status_t result = NO_INIT;
    if(mHwcContext->proc) {
        mHwcContext->proc->invalidate(mHwcContext->proc);
        result = NO_ERROR;
    }
    return result;
}

void QClient::setExtOrientation(uint32_t orientation) {
    mHwcContext->mExtOrientation = orientation;
}

void QClient::setBufferMirrorMode(uint32_t enable) {
    mHwcContext->mBufferMirrorMode = enable;
}

android::status_t QClient::setMode(int32_t mode) {
    status_t result = NO_INIT;
#ifdef QCOM_BSP
    mHwcContext->mExtDisplay->readResolution();
    if (mHwcContext->mExtDisplay->isValidMode(mode) &&
        !mHwcContext->mExtDisplay->isInterlacedMode(mode)) {
        mHwcContext->mResChanged = true;
        mHwcContext->mExtDisplay->setCustomMode(mode);
        screenRefresh();
        result = NO_ERROR;
    } else {
        result = BAD_VALUE;
    }
#endif
    return result;
}

}
