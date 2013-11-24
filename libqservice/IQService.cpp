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

#include <fcntl.h>
#include <stdint.h>
#include <sys/types.h>
#include <binder/Parcel.h>
#include <binder/IBinder.h>
#include <binder/IInterface.h>
#include <binder/IPCThreadState.h>
#include <utils/Errors.h>
#include <private/android_filesystem_config.h>

#include <IQService.h>

using namespace android;
using namespace qClient;
using namespace qhwc;

// ---------------------------------------------------------------------------

namespace qService {

class BpQService : public BpInterface<IQService>
{
public:
    BpQService(const sp<IBinder>& impl)
        : BpInterface<IQService>(impl) {}

    virtual void securing(uint32_t startEnd) {
        Parcel data, reply;
        data.writeInterfaceToken(IQService::getInterfaceDescriptor());
        data.writeInt32(startEnd);
        remote()->transact(SECURING, data, &reply);
    }

    virtual void unsecuring(uint32_t startEnd) {
        Parcel data, reply;
        data.writeInterfaceToken(IQService::getInterfaceDescriptor());
        data.writeInt32(startEnd);
        remote()->transact(UNSECURING, data, &reply);
    }

    virtual void connect(const sp<IQClient>& client) {
        Parcel data, reply;
        data.writeInterfaceToken(IQService::getInterfaceDescriptor());
        data.writeStrongBinder(client->asBinder());
        remote()->transact(CONNECT, data, &reply);
    }

    virtual status_t screenRefresh() {
        Parcel data, reply;
        data.writeInterfaceToken(IQService::getInterfaceDescriptor());
        remote()->transact(SCREEN_REFRESH, data, &reply);
        status_t result = reply.readInt32();
        return result;
    }
    virtual status_t getStdFrameratePixclock(ConfigChangeParams *params) {
        Parcel data, reply;
        data.writeInterfaceToken(IQService::getInterfaceDescriptor());
        status_t result = remote()->transact(GET_STD_FRAMERATE_PIXCLOCK,
                data, &reply);
        params->param1 = reply.readFloat();
        params->param2 = reply.readFloat();
        result = reply.readInt32();
        return result;
    }

    virtual status_t getCurrentFrameratePixclock(ConfigChangeParams
            *params) {
        Parcel data, reply;
        data.writeInterfaceToken(IQService::getInterfaceDescriptor());
        status_t result = remote()->transact(GET_CURRENT_FRAMERATE_PIXCLOCK,
                data, &reply);
        params->param1 = reply.readFloat();
        params->param2 = reply.readFloat();
        result = reply.readInt32();
        return result;
    }

    virtual android::status_t setOverScanParams(
        PP_Video_Layer_Type numVideoLayer,
        OSRectDimensions ossrcparams,
        OSRectDimensions osdstparams) {
        Parcel data,reply;
        data.writeInterfaceToken(IQService::getInterfaceDescriptor());
        data.writeInt32(numVideoLayer);
        data.writeInt32(ossrcparams.left);
        data.writeInt32(ossrcparams.top);
        data.writeInt32(ossrcparams.right);
        data.writeInt32(ossrcparams.bottom);
        data.writeInt32(ossrcparams.isValid);

        data.writeInt32(osdstparams.left);
        data.writeInt32(osdstparams.top);
        data.writeInt32(osdstparams.right);
        data.writeInt32(osdstparams.bottom);
        data.writeInt32(osdstparams.isValid);
        status_t result = remote()->transact(
                SET_OVERSCAN_PARAMS,data,&reply);
        result = reply.readInt32();
        return result;
     }

     virtual android::status_t setOverScanCompensationParams(
            OSRectDimensions oscparams) {
        Parcel data,reply;
        data.writeInterfaceToken(IQService::getInterfaceDescriptor());
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
        data.writeInterfaceToken(IQService::getInterfaceDescriptor());
        data.writeInt32(configChangeType);
        status_t result = remote()->transact(
                START_CONFIG_CHANGE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual android::status_t doConfigChange(
        CONFIG_CHANGE_TYPE configChangeType,
        ConfigChangeParams params) {
        Parcel data, reply;
        data.writeInterfaceToken(IQService::getInterfaceDescriptor());
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
        data.writeInterfaceToken(IQService::getInterfaceDescriptor());
        data.writeInt32(configChangeType);
        status_t result = remote()->transact(STOP_CONFIG_CHANGE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t setPPParams(VideoPPData pParams,
            PP_Video_Layer_Type numVideoLayer){
        Parcel data, reply;
        data.writeInterfaceToken(IQService::getInterfaceDescriptor());
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
        data.writeInterfaceToken(IQService::getInterfaceDescriptor());
        data.writeInt32(value);
        status_t result = remote()->transact(SET_PQCSTATE, data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual void setExtOrientation(uint32_t orientation) {
        Parcel data, reply;
        data.writeInterfaceToken(IQService::getInterfaceDescriptor());
        data.writeInt32(orientation);
        remote()->transact(EXTERNAL_ORIENTATION, data, &reply);
    }

    virtual void setBufferMirrorMode(uint32_t enable) {
        Parcel data, reply;
        data.writeInterfaceToken(IQService::getInterfaceDescriptor());
        data.writeInt32(enable);
        remote()->transact(BUFFER_MIRRORMODE, data, &reply);
    }
};

IMPLEMENT_META_INTERFACE(QService, "android.display.IQService");

// ----------------------------------------------------------------------

static void getProcName(int pid, char *buf, int size);

status_t BnQService::onTransact(
    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
    // IPC should be from mediaserver only
    IPCThreadState* ipc = IPCThreadState::self();
    const int callerPid = ipc->getCallingPid();
    const int callerUid = ipc->getCallingUid();
    const size_t MAX_BUF_SIZE = 1024;
    char callingProcName[MAX_BUF_SIZE] = {0};

    getProcName(callerPid, callingProcName, MAX_BUF_SIZE);

    const bool permission = (callerUid == AID_MEDIA);

    switch(code) {
        case GET_STD_FRAMERATE_PIXCLOCK:{
            CHECK_INTERFACE(IQService, data, reply);
            ConfigChangeParams params;
            status_t res = getStdFrameratePixclock(&params);
            reply->writeFloat(params.param1);
            reply->writeFloat(params.param2);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case GET_CURRENT_FRAMERATE_PIXCLOCK:{
            CHECK_INTERFACE(IQService, data, reply);
            ConfigChangeParams params;
            status_t res = getCurrentFrameratePixclock(&params);
            reply->writeFloat(params.param1);
            reply->writeFloat(params.param2);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_OVERSCAN_PARAMS:{
            PP_Video_Layer_Type numVideoLayer;
            OSRectDimensions ovsrcparams;
            OSRectDimensions ovdstparams;
            CHECK_INTERFACE(IQService, data, reply);
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
            CHECK_INTERFACE(IQService, data, reply);
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
            CHECK_INTERFACE(IHWComposer, data, reply);
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
            CHECK_INTERFACE(IQService, data, reply);
            configType = (CONFIG_CHANGE_TYPE)data.readInt32();
            status_t res = stopConfigChange(configType);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_PP_PARAMS: {
            VideoPPData pParams;
            PP_Video_Layer_Type numVideoLayer;
            CHECK_INTERFACE(IQService, data, reply);
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
           CHECK_INTERFACE(IQService, data, reply);
           value = data.readInt32();
           status_t res = setPQCState(value);
           reply->writeInt32(res);
           return NO_ERROR;
        } break;
        case SECURING: {
            if(!permission) {
                ALOGE("display.qservice SECURING access denied: \
                      pid=%d uid=%d process=%s",
                      callerPid, callerUid, callingProcName);
                return PERMISSION_DENIED;
            }
            CHECK_INTERFACE(IQService, data, reply);
            uint32_t startEnd = data.readInt32();
            securing(startEnd);
            return NO_ERROR;
        } break;
        case UNSECURING: {
            if(!permission) {
                ALOGE("display.qservice UNSECURING access denied: \
                      pid=%d uid=%d process=%s",
                      callerPid, callerUid, callingProcName);
                return PERMISSION_DENIED;
            }
            CHECK_INTERFACE(IQService, data, reply);
            uint32_t startEnd = data.readInt32();
            unsecuring(startEnd);
            return NO_ERROR;
        } break;
        case CONNECT: {
            CHECK_INTERFACE(IQService, data, reply);
            if(callerUid != AID_GRAPHICS) {
                ALOGE("display.qservice CONNECT access denied: \
                      pid=%d uid=%d process=%s",
                      callerPid, callerUid, callingProcName);
                return PERMISSION_DENIED;
            }
            sp<IQClient> client =
                interface_cast<IQClient>(data.readStrongBinder());
            connect(client);
            return NO_ERROR;
        } break;
        case SCREEN_REFRESH: {
            CHECK_INTERFACE(IQService, data, reply);
            if(callerUid != AID_SYSTEM) {
                ALOGE("display.qservice SCREEN_REFRESH access denied: \
                      pid=%d uid=%d process=%s",callerPid,
                      callerUid, callingProcName);
                return PERMISSION_DENIED;
            }
            return screenRefresh();
        } break;
        case EXTERNAL_ORIENTATION: {
            CHECK_INTERFACE(IQService, data, reply);
            if(callerUid != AID_SYSTEM) {
                ALOGE("display.qservice EXTERNAL_ORIENTATION access denied: \
                      pid=%d uid=%d process=%s",callerPid,
                      callerUid, callingProcName);
                return PERMISSION_DENIED;
            }
            uint32_t orientation = data.readInt32();
            setExtOrientation(orientation);
            return NO_ERROR;
        } break;
        case BUFFER_MIRRORMODE: {
            CHECK_INTERFACE(IQService, data, reply);
            if(callerUid != AID_SYSTEM) {
                ALOGE("display.qservice BUFFER_MIRRORMODE access denied: \
                      pid=%d uid=%d process=%s",callerPid,
                      callerUid, callingProcName);
                return PERMISSION_DENIED;
            }
            uint32_t enable = data.readInt32();
            setBufferMirrorMode(enable);
            return NO_ERROR;
        } break;
        default:
            return BBinder::onTransact(code, data, reply, flags);
    }
}

//Helper
static void getProcName(int pid, char *buf, int size) {
    int fd = -1;
    snprintf(buf, size, "/proc/%d/cmdline", pid);
    fd = open(buf, O_RDONLY);
    if (fd < 0) {
        strcpy(buf, "Unknown");
    } else {
        int len = read(fd, buf, size - 1);
        buf[len] = 0;
        close(fd);
    }
}

}; // namespace qService
