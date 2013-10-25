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

#ifndef ANDROID_QCLIENT_H
#define ANDROID_QCLIENT_H

#include <utils/Errors.h>
#include <sys/types.h>
#include <cutils/log.h>
#include <binder/IServiceManager.h>
#include <media/IMediaDeathNotifier.h>
#include <IQClient.h>
#include <hwc_ppmetadata.h>

using namespace qhwc;

struct hwc_context_t;

namespace qClient {
// ----------------------------------------------------------------------------

class QClient : public BnQClient {
public:
    QClient(hwc_context_t *ctx);
    virtual ~QClient();
    virtual android::status_t notifyCallback(uint32_t msg, uint32_t value);
    virtual android::status_t getStdFrameratePixclock(
        ConfigChangeParams *params);
    virtual android::status_t getCurrentFrameratePixclock(
        ConfigChangeParams *params);
    virtual android::status_t setOverScanParams(
        PP_Video_Layer_Type numVideoLayer,
        OSRectDimensions ossrcparams,
        OSRectDimensions osdstparams);
    virtual android::status_t setOverScanCompensationParams(
        OSRectDimensions oscparams);
    virtual android::status_t setPPParams(VideoPPData pParams,
        PP_Video_Layer_Type numVideoLayer);
    virtual android::status_t startConfigChange(
        CONFIG_CHANGE_TYPE configChangeType);
    virtual  android::status_t doConfigChange(
        CONFIG_CHANGE_TYPE configChangeType,
        ConfigChangeParams params);
    virtual android::status_t stopConfigChange(
        CONFIG_CHANGE_TYPE configChangeType);
    virtual android::status_t setPQCState(int value);

private:
    //Notifies of Media Player death
    class MPDeathNotifier : public android::IMediaDeathNotifier {
    public:
        MPDeathNotifier(hwc_context_t* ctx) : mHwcContext(ctx){}
        virtual void died();
        hwc_context_t *mHwcContext;
    };

    void securing(uint32_t startEnd);
    void unsecuring(uint32_t startEnd);
    android::status_t screenRefresh();
    void setExtOrientation(uint32_t orientation);
    void setBufferMirrorMode(uint32_t enable);

    hwc_context_t *mHwcContext;
    const android::sp<android::IMediaDeathNotifier> mMPDeathNotifier;
};
}; // namespace qClient
#endif // ANDROID_QCLIENT_H
