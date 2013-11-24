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

#ifndef ANDROID_IQCLIENT_H
#define ANDROID_IQCLIENT_H

#include <stdint.h>
#include <sys/types.h>
#include <utils/Errors.h>
#include <utils/RefBase.h>
#include <binder/IInterface.h>
#include <hwc_ppmetadata.h>

using namespace qhwc;

namespace qClient {
// ----------------------------------------------------------------------------
class IQClient : public android::IInterface
{
public:
    DECLARE_META_INTERFACE(QClient);
    virtual android::status_t notifyCallback(uint32_t msg, uint32_t value) = 0;
    virtual android::status_t getStdFrameratePixclock(ConfigChangeParams
            *params) = 0;
    virtual android::status_t getCurrentFrameratePixclock(
            ConfigChangeParams *params) = 0;
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
};

// ----------------------------------------------------------------------------

class BnQClient : public android::BnInterface<IQClient>
{
public:
    virtual android::status_t onTransact( uint32_t code,
                                          const android::Parcel& data,
                                          android::Parcel* reply,
                                          uint32_t flags = 0);
};

// ----------------------------------------------------------------------------
}; // namespace qClient

#endif // ANDROID_IQCLIENT_H
