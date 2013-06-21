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

#ifndef ANDROID_HWCOMPOSER_SERVICE_H
#define ANDROID_HWCOMPOSER_SERVICE_H

#include <utils/Errors.h>
#include <sys/types.h>
#include <cutils/log.h>
#include <binder/IServiceManager.h>
#include <ihwc.h>
#include <hwc_external.h>
#include <hwc_utils.h>


namespace hwcService {
// ----------------------------------------------------------------------------

class HWComposerService : public BnHWComposer {
private:
    HWComposerService();
public:
    ~HWComposerService();

    static HWComposerService* getInstance();
    virtual android::status_t getResolutionModeCount(int *modeCount);
    virtual android::status_t getResolutionModes(int *EDIDModes, int count = 1);
    virtual android::status_t getExternalDisplay(int *extDisp);

    virtual android::status_t setHPDStatus(int enable);
    virtual android::status_t setResolutionMode(int resMode);
    virtual android::status_t setActionSafeDimension(int w, int h);
    virtual android::status_t setPPParams(qhwc::VideoPPData pPPParams,
                            qhwc::PP_Video_Layer_Type numVideoLayer);
    virtual android::status_t setPQCState(int value);
    virtual android::status_t setOverScanCompensationParams(
            qhwc::OSRectDimensions oscparams);
    virtual android::status_t setOverScanParams(
            qhwc::PP_Video_Layer_Type numVideoLayer,
            qhwc::OSRectDimensions ovsrcparams,
            qhwc::OSRectDimensions ovdstparams);

    //Config Change Hooks
    virtual android::status_t startConfigChange(
            qhwc::CONFIG_CHANGE_TYPE configChangeType);
    virtual android::status_t doConfigChange(
        qhwc::CONFIG_CHANGE_TYPE configChangeType,
        qhwc::ConfigChangeParams params);
    virtual android::status_t stopConfigChange(
        qhwc::CONFIG_CHANGE_TYPE configChangeType);
    virtual android::status_t getStdFrameratePixclock(
        qhwc::ConfigChangeParams *params);
    virtual android::status_t getCurrentFrameratePixclock(
        qhwc::ConfigChangeParams *params);
    virtual android::status_t ConfigChange(
             qhwc::CONFIG_CHANGE_TYPE configChangeType,
             qhwc::ConfigChangeParams params);

    // Secure Intent Hooks
    virtual android::status_t setOpenSecureStart();
    virtual android::status_t setOpenSecureEnd();
    virtual android::status_t setCloseSecureStart();
    virtual android::status_t setCloseSecureEnd();
    void setHwcContext(hwc_context_t *hwcCtx);
private:
    virtual void inValidate();
    static HWComposerService *sHwcService;
    hwc_context_t *mHwcContext;
    int mMaxActionSafeWidth;
    int mMaxActionSafeHeight;
};

}; // namespace hwcService
#endif // ANDROID_HWCOMPOSER_SERVICE_H
