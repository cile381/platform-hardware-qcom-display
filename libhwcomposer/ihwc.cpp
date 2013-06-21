/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, The Linux Foundation. All rights reserved.
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

#include <stdint.h>
#include <sys/types.h>
#include <utils/Errors.h>

#include <binder/Parcel.h>
#include <binder/IBinder.h>
#include <binder/IInterface.h>
#include <ihwc.h>
#include <hwc_utils.h>

using namespace android;

// ---------------------------------------------------------------------------

namespace hwcService {

class BpHWComposer : public BpInterface<IHWComposer>
{
public:
    BpHWComposer(const sp<IBinder>& impl)
        : BpInterface<IHWComposer>(impl)
    {
    }

    virtual status_t setHPDStatus(int hpdStatus) {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(hpdStatus);
        status_t result = remote()->transact(SET_EXT_HPD_ENABLE,
                                             data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t setResolutionMode(int resMode) {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(resMode);
        status_t result = remote()->transact(SET_EXT_DISPLAY_RESOLUTION_MODE,
                                             data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t setActionSafeDimension(int w, int h) {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(w);
        data.writeInt32(h);
        status_t result =
            remote()->transact(SET_EXT_DISPLAY_ACTIONSAFE_DIMENSIONS,
                                             data, &reply);
        result = reply.readInt32();
        return result;
    }
    virtual status_t setOpenSecureStart() {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(SET_OPEN_SECURE_START,
                                             data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t setOpenSecureEnd() {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(SET_OPEN_SECURE_END,
                                             data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t setCloseSecureStart() {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(SET_CLOSE_SECURE_START,
                                             data, &reply);
        result = reply.readInt32();
        return result;
    }
    virtual status_t setCloseSecureEnd() {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(SET_CLOSE_SECURE_END,
                                             data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t getExternalDisplay(int *extDispType) {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(GET_EXT_DISPLAY_TYPE,
                                             data, &reply);
        *extDispType = reply.readInt32();
        result = reply.readInt32();
        return result;
    }

    virtual status_t getResolutionModes(int *resModes, int count) {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(count);
        status_t result = remote()->transact(GET_EXT_DISPLAY_RESOLUTION_MODES,
                                             data, &reply);
        for(int i = 0;i < count;i++) {
            resModes[i] = reply.readInt32();
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t getResolutionModeCount(int *resModeCount) {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(
                          GET_EXT_DISPLAY_RESOLUTION_MODE_COUNT, data, &reply);
        *resModeCount = reply.readInt32();
        result = reply.readInt32();
        return result;
    }

    virtual status_t setPPParams(qhwc::VideoPPData pParams,
                                 qhwc::PP_Video_Layer_Type numVideoLayer){
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(numVideoLayer);
        data.writeInt32((int32_t) pParams.ops);
        data.writeInt32(pParams.hue);
        data.writeFloat(pParams.saturation);
        data.writeInt32(pParams.intensity);
        data.writeFloat(pParams.contrast);
        data.writeInt32(pParams.sharpness);
        status_t result = remote()->transact(
                          SET_PP_PARAMS, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t setPQCState(int value){
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(value);
        status_t result = remote()->transact(
                          SET_PQCSTATE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual android::status_t setOverScanCompensationParams(
            qhwc::OSRectDimensions oscparams) {
        Parcel data,reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(oscparams.left);
        data.writeInt32(oscparams.top);
        data.writeInt32(oscparams.right);
        data.writeInt32(oscparams.bottom);
        data.writeInt32(oscparams.isValid);
        status_t result = remote()->transact(
                SET_OVERSCANCOMPENSATION_PARAMS,data,&reply);
        result = reply.readInt32();
        return result;
    }

    virtual android::status_t setOverScanParams(
            qhwc::PP_Video_Layer_Type numVideoLayer,
            qhwc::OSRectDimensions ovsrcparams,
            qhwc::OSRectDimensions ovdstparams) {
        Parcel data,reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(numVideoLayer);
        data.writeInt32(ovsrcparams.left);
        data.writeInt32(ovsrcparams.top);
        data.writeInt32(ovsrcparams.right);
        data.writeInt32(ovsrcparams.bottom);
        data.writeInt32(ovsrcparams.isValid);

        data.writeInt32(ovdstparams.left);
        data.writeInt32(ovdstparams.top);
        data.writeInt32(ovdstparams.right);
        data.writeInt32(ovdstparams.bottom);
        data.writeInt32(ovdstparams.isValid);
        status_t result = remote()->transact(
                SET_OVERSCAN_PARAMS,data,&reply);
        result = reply.readInt32();
        return result;
    }

    virtual android::status_t startConfigChange(
            qhwc::CONFIG_CHANGE_TYPE configChangeType){
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(configChangeType);
        status_t result = remote()->transact(
                          START_CONFIG_CHANGE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual android::status_t doConfigChange(
        qhwc::CONFIG_CHANGE_TYPE configChangeType,
        qhwc::ConfigChangeParams params) {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(configChangeType);
        data.writeFloat(params.param1);
        data.writeFloat(params.param2);
        status_t result = remote()->transact(
                          DO_CONFIG_CHANGE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual android::status_t stopConfigChange(
            qhwc::CONFIG_CHANGE_TYPE configChangeType){
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(configChangeType);
        status_t result = remote()->transact(
                          STOP_CONFIG_CHANGE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual android::status_t getStdFrameratePixclock(
        qhwc::ConfigChangeParams *params){
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(GET_STD_FRAMERATE_PIXCLOCK,
                                             data, &reply);
        params->param1 = reply.readFloat();
        params->param2 = reply.readFloat();
        result = reply.readInt32();
        return result;
    }

    virtual android::status_t getCurrentFrameratePixclock(
        qhwc::ConfigChangeParams *params){
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(GET_CURRENT_FRAMERATE_PIXCLOCK,
                                             data, &reply);
        params->param1 = reply.readFloat();
        params->param2 = reply.readFloat();
        result = reply.readInt32();
        return result;
    }
    virtual android::status_t ConfigChange(
        qhwc::CONFIG_CHANGE_TYPE configChangeType,
        qhwc::ConfigChangeParams params) {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(configChangeType);
        data.writeFloat(params.param1);
        data.writeFloat(params.param2);
        status_t result = remote()->transact(
                          CONFIG_CHANGE, data, &reply);
        result = reply.readInt32();
        return result;
    }

};

IMPLEMENT_META_INTERFACE(HWComposer, "android.display.IHWComposer");

// ----------------------------------------------------------------------

status_t BnHWComposer::onTransact(
    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
    // codes that don't require permission check
    switch(code) {
        case SET_EXT_HPD_ENABLE: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            int hpdStatus = data.readInt32();
            status_t res = setHPDStatus(hpdStatus);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_EXT_DISPLAY_RESOLUTION_MODE: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            int resMode = data.readInt32();
            status_t res = setResolutionMode(resMode);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_EXT_DISPLAY_ACTIONSAFE_DIMENSIONS: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            int w = data.readInt32();
            int h = data.readInt32();
            status_t res = setActionSafeDimension(w, h);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_OPEN_SECURE_START: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            status_t res = setOpenSecureStart();
            reply->writeInt32(res);
            return NO_ERROR;
        }break;
        case SET_OPEN_SECURE_END: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            status_t res = setOpenSecureEnd();
            reply->writeInt32(res);
            return NO_ERROR;
        }break;
        case SET_CLOSE_SECURE_START: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            status_t res = setCloseSecureStart();
            reply->writeInt32(res);
            return NO_ERROR;
        }break;
        case SET_CLOSE_SECURE_END: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            status_t res = setCloseSecureEnd();
            reply->writeInt32(res);
            return NO_ERROR;
        }break;
        case GET_EXT_DISPLAY_TYPE: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            int extDispType;
            status_t res = getExternalDisplay(&extDispType);
            reply->writeInt32(extDispType);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case GET_EXT_DISPLAY_RESOLUTION_MODES: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            int count = data.readInt32();
            int resModes[64];
            status_t res = getResolutionModes(&resModes[0]);
            for(int i = 0;i < count;i++) {
                reply->writeInt32(resModes[i]);
            }
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case GET_EXT_DISPLAY_RESOLUTION_MODE_COUNT: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            int resModeCount;
            status_t res = getResolutionModeCount(&resModeCount);
            reply->writeInt32(resModeCount);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_PP_PARAMS: {
            qhwc::VideoPPData pParams;
            qhwc::PP_Video_Layer_Type numVideoLayer;
            CHECK_INTERFACE(IHWComposer, data, reply);
            numVideoLayer = (qhwc::PP_Video_Layer_Type)data.readInt32();
            pParams.ops = data.readInt32();
            pParams.hue = data.readInt32();
            pParams.saturation = data.readFloat();
            pParams.intensity = data.readInt32();
            pParams.contrast = data.readFloat();
            pParams.sharpness = data.readInt32();
            status_t res = setPPParams(pParams, numVideoLayer);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_PQCSTATE: {
           int value;
           CHECK_INTERFACE(IHWComposer, data, reply);
           value = data.readInt32();
           status_t res = setPQCState(value);
           reply->writeInt32(res);
           return NO_ERROR;
        } break;
        case SET_OVERSCANCOMPENSATION_PARAMS: {
            qhwc::OSRectDimensions oscparams;
            CHECK_INTERFACE(IHWComposer, data, reply);
            oscparams.left = data.readInt32();
            oscparams.top = data.readInt32();
            oscparams.right = data.readInt32();
            oscparams.bottom = data.readInt32();
            oscparams.isValid = data.readInt32();
            status_t res = setOverScanCompensationParams(oscparams);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_OVERSCAN_PARAMS: {
            qhwc::PP_Video_Layer_Type numVideoLayer;
            qhwc::OSRectDimensions ovsrcparams;
            qhwc::OSRectDimensions ovdstparams;
            CHECK_INTERFACE(IHWComposer, data, reply);
            numVideoLayer = (qhwc::PP_Video_Layer_Type)data.readInt32();
            ovsrcparams.left = data.readInt32();
            ovsrcparams.top = data.readInt32();
            ovsrcparams.right = data.readInt32();
            ovsrcparams.bottom = data.readInt32();
            ovsrcparams.isValid = data.readInt32();

            ovdstparams.left = data.readInt32();
            ovdstparams.top = data.readInt32();
            ovdstparams.right = data.readInt32();
            ovdstparams.bottom = data.readInt32();
            ovdstparams.isValid = data.readInt32();
            status_t res = setOverScanParams(numVideoLayer,ovsrcparams,ovdstparams);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case START_CONFIG_CHANGE:{
            qhwc::CONFIG_CHANGE_TYPE configType;
            CHECK_INTERFACE(IHWComposer, data, reply);
            configType = (qhwc::CONFIG_CHANGE_TYPE)data.readInt32();
            status_t res = startConfigChange(configType);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case DO_CONFIG_CHANGE:{
            qhwc::CONFIG_CHANGE_TYPE configType;
            qhwc::ConfigChangeParams params;
            CHECK_INTERFACE(IHWComposer, data, reply);
            configType = (qhwc::CONFIG_CHANGE_TYPE)data.readInt32();
            params.param1 = data.readFloat();
            params.param2 = data.readFloat();
            status_t res = doConfigChange(configType,params);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case STOP_CONFIG_CHANGE:{
            qhwc::CONFIG_CHANGE_TYPE configType;
            CHECK_INTERFACE(IHWComposer, data, reply);
            configType = (qhwc::CONFIG_CHANGE_TYPE)data.readInt32();
            status_t res = stopConfigChange(configType);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case GET_STD_FRAMERATE_PIXCLOCK:{
            CHECK_INTERFACE(IHWComposer, data, reply);
            qhwc::ConfigChangeParams params;
            status_t res = getStdFrameratePixclock(&params);
            reply->writeFloat(params.param1);
            reply->writeFloat(params.param2);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case GET_CURRENT_FRAMERATE_PIXCLOCK:{
            CHECK_INTERFACE(IHWComposer, data, reply);
            qhwc::ConfigChangeParams params;
            status_t res = getCurrentFrameratePixclock(&params);
            reply->writeFloat(params.param1);
            reply->writeFloat(params.param2);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case CONFIG_CHANGE:{
            qhwc::CONFIG_CHANGE_TYPE configType;
            qhwc::ConfigChangeParams params;
            CHECK_INTERFACE(IHWComposer, data, reply);
            configType = (qhwc::CONFIG_CHANGE_TYPE)data.readInt32();
            params.param1 = data.readFloat();
            params.param2 = data.readFloat();
            status_t res = ConfigChange(configType,params);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        default:
            return BBinder::onTransact(code, data, reply, flags);
    }
}

}; // namespace hwcService
