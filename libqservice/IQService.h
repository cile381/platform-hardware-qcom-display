/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are
 * retained for attribution purposes only.

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

#ifndef ANDROID_IQSERVICE_H
#define ANDROID_IQSERVICE_H

#include <stdint.h>
#include <sys/types.h>
#include <utils/Errors.h>
#include <utils/RefBase.h>
#include <binder/IInterface.h>
#include <binder/IBinder.h>
#include <IQClient.h>
#include <hwc_ppmetadata.h>

using namespace qhwc;

namespace qService {
// ----------------------------------------------------------------------------
class IQService : public android::IInterface
{
public:
    DECLARE_META_INTERFACE(QService);
    enum {
        // Hardware securing start/end notification
        SECURING = android::IBinder::FIRST_CALL_TRANSACTION,
        UNSECURING, // Hardware unsecuring start/end notification
        CONNECT,
        GET_STD_FRAMERATE_PIXCLOCK,
        GET_CURRENT_FRAMERATE_PIXCLOCK,
        SET_OVERSCAN_PARAMS,
        SET_OVERSCANCOMPENSATION_PARAMS,
        START_CONFIG_CHANGE,
        DO_CONFIG_CHANGE,
        STOP_CONFIG_CHANGE,
        SET_PP_PARAMS,
        SET_PQCSTATE,
        SCREEN_REFRESH,
        EXTERNAL_ORIENTATION,
        BUFFER_MIRRORMODE,
        SET_MODE,
    };
    enum {
        END = 0,
        START,
    };
    virtual void securing(uint32_t startEnd) = 0;
    virtual void unsecuring(uint32_t startEnd) = 0;
    virtual void connect(const android::sp<qClient::IQClient>& client) = 0;
    virtual android::status_t screenRefresh() = 0;
    virtual void setExtOrientation(uint32_t orientation) = 0;
    virtual void setBufferMirrorMode(uint32_t enable) = 0;
    virtual android::status_t getStdFrameratePixclock(ConfigChangeParams
        *params) = 0;
    virtual android::status_t getCurrentFrameratePixclock(ConfigChangeParams
        *params) = 0;
    virtual android::status_t setOverScanParams(
        PP_Video_Layer_Type numVideoLayer,
        OSRectDimensions ossrcparams,
        OSRectDimensions osdstparams) = 0;
    virtual android::status_t setOverScanCompensationParams(
        OSRectDimensions oscparams) = 0;
    virtual android::status_t setPPParams(VideoPPData pParams,
        PP_Video_Layer_Type numVideoLayer) = 0;
    virtual android::status_t startConfigChange(
        CONFIG_CHANGE_TYPE configChangeType) = 0;
    virtual android::status_t doConfigChange(
        CONFIG_CHANGE_TYPE configChangeType,
        ConfigChangeParams params) = 0;
    virtual android::status_t stopConfigChange(
        CONFIG_CHANGE_TYPE configChangeType) = 0;
    virtual android::status_t setPQCState(int value) = 0;
    virtual android::status_t setMode(int32_t mode) = 0;
};

// ----------------------------------------------------------------------------

class BnQService : public android::BnInterface<IQService>
{
public:
    virtual android::status_t onTransact( uint32_t code,
        const android::Parcel& data,
        android::Parcel* reply,
        uint32_t flags = 0);
};

// ----------------------------------------------------------------------------
}; // namespace qService

#endif // ANDROID_IQSERVICE_H
