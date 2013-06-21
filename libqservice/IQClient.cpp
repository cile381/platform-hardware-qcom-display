/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2014, The Linux Foundation. All rights reserved.
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

#include <sys/types.h>
#include <binder/Parcel.h>
#include <binder/IBinder.h>
#include <binder/IInterface.h>
#include <utils/Errors.h>
#include <IQClient.h>

using namespace android;
using namespace qhwc;

// ---------------------------------------------------------------------------

namespace qClient {

enum {
    NOTIFY_CALLBACK = IBinder::FIRST_CALL_TRANSACTION,
    GET_STD_FRAMERATE_PIXCLOCK,
    GET_CURRENT_FRAMERATE_PIXCLOCK,
    SET_OVERSCAN_PARAMS,
    SET_OVERSCANCOMPENSATION_PARAMS,
    START_CONFIG_CHANGE,
    DO_CONFIG_CHANGE,
    STOP_CONFIG_CHANGE,
    SET_PP_PARAMS,
    SET_PQCSTATE,
    CONFIG_CHANGE,

};

class BpQClient : public BpInterface<IQClient>
{
public:
    BpQClient(const sp<IBinder>& impl)
        : BpInterface<IQClient>(impl) {}

    virtual status_t notifyCallback(uint32_t msg, uint32_t value) {
        Parcel data, reply;
        data.writeInterfaceToken(IQClient::getInterfaceDescriptor());
        data.writeInt32(msg);
        data.writeInt32(value);
        remote()->transact(NOTIFY_CALLBACK, data, &reply);
        status_t result = reply.readInt32();
        return result;
    }

    virtual status_t getStdFrameratePixclock(ConfigChangeParams *params) {
        Parcel data, reply;
        data.writeInterfaceToken(IQClient::getInterfaceDescriptor());
        data.writeFloat(params->param1);
        data.writeFloat(params->param2);
        remote()->transact(GET_STD_FRAMERATE_PIXCLOCK, data, &reply);
        status_t result = reply.readInt32();
        return result;
    }

    virtual status_t getCurrentFrameratePixclock(ConfigChangeParams
            *params) {
        Parcel data, reply;
        data.writeInterfaceToken(IQClient::getInterfaceDescriptor());
        data.writeFloat(params->param1);
        data.writeFloat(params->param2);
        remote()->transact(GET_CURRENT_FRAMERATE_PIXCLOCK, data, &reply);
        status_t result = reply.readInt32();
        return result;
    }

    virtual android::status_t setOverScanParams(
            PP_Video_Layer_Type numVideoLayer,
            OSRectDimensions ovsrcparams,
            OSRectDimensions ovdstparams) {
        Parcel data,reply;
        data.writeInterfaceToken(IQClient::getInterfaceDescriptor());
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
        status_t result = remote()->transact(SET_OVERSCAN_PARAMS,data,&reply);
        result = reply.readInt32();
        return result;
    }

    virtual android::status_t setOverScanCompensationParams(
            OSRectDimensions oscparams) {
        Parcel data,reply;
        data.writeInterfaceToken(IQClient::getInterfaceDescriptor());
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

    virtual android::status_t startConfigChange(
            CONFIG_CHANGE_TYPE configChangeType){
        Parcel data, reply;
        data.writeInterfaceToken(IQClient::getInterfaceDescriptor());
        data.writeInt32(configChangeType);
        status_t result = remote()->transact(
                START_CONFIG_CHANGE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t doConfigChange(
        CONFIG_CHANGE_TYPE configChangeType,
        ConfigChangeParams params) {
        Parcel data, reply;
        data.writeInterfaceToken(IQClient::getInterfaceDescriptor());
        data.writeInt32(configChangeType);
        data.writeFloat(params.param1);
        data.writeFloat(params.param2);
        status_t result = remote()->transact(
                DO_CONFIG_CHANGE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual android::status_t stopConfigChange(
            CONFIG_CHANGE_TYPE configChangeType){
        Parcel data, reply;
        data.writeInterfaceToken(IQClient::getInterfaceDescriptor());
        data.writeInt32(configChangeType);
        status_t result = remote()->transact(
                STOP_CONFIG_CHANGE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t setPPParams(VideoPPData pParams,
            PP_Video_Layer_Type numVideoLayer){
        Parcel data, reply;
        data.writeInterfaceToken(IQClient::getInterfaceDescriptor());
        data.writeInt32(numVideoLayer);
        data.writeInt32((int32_t) pParams.ops);
        data.writeInt32(pParams.hue);
        data.writeFloat(pParams.saturation);
        data.writeInt32(pParams.intensity);
        data.writeFloat(pParams.contrast);
        data.writeInt32(pParams.sharpness);
        status_t result = remote()->transact(SET_PP_PARAMS, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t setPQCState(int value){
        Parcel data, reply;
        data.writeInterfaceToken(IQClient::getInterfaceDescriptor());
        data.writeInt32(value);
        status_t result = remote()->transact(SET_PQCSTATE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t ConfigChange(
        CONFIG_CHANGE_TYPE configChangeType,
        ConfigChangeParams params) {
        Parcel data, reply;
        data.writeInterfaceToken(IQClient::getInterfaceDescriptor());
        data.writeInt32(configChangeType);
        data.writeFloat(params.param1);
        data.writeFloat(params.param2);
        status_t result = remote()->transact(
                          CONFIG_CHANGE, data, &reply);
        result = reply.readInt32();
        return result;
    }

};

IMPLEMENT_META_INTERFACE(QClient, "android.display.IQClient");

// ----------------------------------------------------------------------

status_t BnQClient::onTransact(
    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
    switch(code) {
        case NOTIFY_CALLBACK: {
            CHECK_INTERFACE(IQClient, data, reply);
            uint32_t msg = data.readInt32();
            uint32_t value = data.readInt32();
            notifyCallback(msg, value);
            return NO_ERROR;
        } break;
        case GET_STD_FRAMERATE_PIXCLOCK: {
            CHECK_INTERFACE(IQClient, data, reply);
            ConfigChangeParams params;
            params.param1 = data.readFloat();
            params.param2 = data.readFloat();
            status_t res = getStdFrameratePixclock(&params);
            reply->writeFloat(params.param1);
            reply->writeFloat(params.param2);
            reply->writeInt32(res);
            return NO_ERROR;
        }break;
        case GET_CURRENT_FRAMERATE_PIXCLOCK: {
            CHECK_INTERFACE(IQClient, data, reply);
            ConfigChangeParams params;
            params.param1 = data.readFloat();
            params.param2 = data.readFloat();
            status_t res = getCurrentFrameratePixclock(&params);
            reply->writeFloat(params.param1);
            reply->writeFloat(params.param2);
            reply->writeInt32(res);
            return NO_ERROR;
        }break;
        case SET_OVERSCAN_PARAMS: {
            PP_Video_Layer_Type numVideoLayer;
            OSRectDimensions ovsrcparams;
            OSRectDimensions ovdstparams;
            CHECK_INTERFACE(IQClient, data, reply);
            numVideoLayer = (PP_Video_Layer_Type)data.readInt32();
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
            status_t res = setOverScanParams(numVideoLayer,ovsrcparams,
                    ovdstparams);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_OVERSCANCOMPENSATION_PARAMS: {
            OSRectDimensions oscparams;
            CHECK_INTERFACE(IQClient, data, reply);
            oscparams.left = data.readInt32();
            oscparams.top = data.readInt32();
            oscparams.right = data.readInt32();
            oscparams.bottom = data.readInt32();
            oscparams.isValid = data.readInt32();
            status_t res = setOverScanCompensationParams(oscparams);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case START_CONFIG_CHANGE:{
            CONFIG_CHANGE_TYPE configType;
            CHECK_INTERFACE(IQClient, data, reply);
            configType = (CONFIG_CHANGE_TYPE)data.readInt32();
            status_t res = startConfigChange(configType);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case DO_CONFIG_CHANGE:{
            CONFIG_CHANGE_TYPE configType;
            ConfigChangeParams params;
            CHECK_INTERFACE(IQClient, data, reply);
            configType = (CONFIG_CHANGE_TYPE)data.readInt32();
            params.param1 = data.readFloat();
            params.param2 = data.readFloat();
            status_t res = doConfigChange(configType,params);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case STOP_CONFIG_CHANGE:{
            CONFIG_CHANGE_TYPE configType;
            CHECK_INTERFACE(IQClient, data, reply);
            configType = (CONFIG_CHANGE_TYPE)data.readInt32();
            status_t res = stopConfigChange(configType);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_PP_PARAMS: {
            VideoPPData pParams;
            PP_Video_Layer_Type numVideoLayer;
            CHECK_INTERFACE(IQClient, data, reply);
            numVideoLayer = (PP_Video_Layer_Type)data.readInt32();
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
           CHECK_INTERFACE(IQClient, data, reply);
           value = data.readInt32();
           status_t res = setPQCState(value);
           reply->writeInt32(res);
           return NO_ERROR;
        } break;
        case CONFIG_CHANGE:{
            qhwc::CONFIG_CHANGE_TYPE configType;
            qhwc::ConfigChangeParams params;
            CHECK_INTERFACE(IQClient, data, reply);
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

}; // namespace qClient
