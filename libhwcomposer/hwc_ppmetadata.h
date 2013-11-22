/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2013, Linux Foundation. All rights reserved.
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

#ifndef HWC_PPMETADATA_H
#define HWC_PPMETADATA_H

#include <utils/threads.h>

#define DEBUG_METADATA 0

namespace qhwc {
// ----------------------------------------------------------------------------
enum PP_Video_Layer_Type {
    VIDEO_LAYER_0,
    VIDEO_LAYER_1,
    VIDEO_MAX_LAYER = VIDEO_LAYER_1,
};

enum PPParamType {
    PP_PARAM_HSIC = 0x1,
    PP_PARAM_SHARPNESS = 0x2,
};

enum CONFIG_CHANGE_TYPE {
    NO_CORRECTION,
    PICTURE_QUALITY_CORRECTION,
    PIXEL_CLOCK_CORRECTION
};

typedef struct _ConfigChangeParams {
    float param1;
    float param2;
} ConfigChangeParams;

typedef struct _VideoPPData {
    uint32_t ops;
    int32_t hue;
    float saturation;
    int32_t intensity;
    float contrast;
    uint32_t sharpness;
    bool isValid;
} VideoPPData;

struct OSRectDimensions {
    OSRectDimensions(): left(0), top(0),
        right(0), bottom(0), isValid(0){}
    OSRectDimensions(int32_t _left, int32_t _top,
            int32_t _right, int32_t _bottom):
    left(_left), top(_top), right(_right), bottom(_bottom), isValid(0){}
    OSRectDimensions(int32_t _left, int32_t _top,
            int32_t _right, int32_t _bottom, int32_t _isValid):
    left(_left), top(_top), right(_right), bottom(_bottom), isValid(_isValid){}

    void dump() const {
        if(isValid)
            ALOGE_IF(DEBUG_METADATA,"OSRectDimensions are not set");
        else {
            ALOGE_IF(DEBUG_METADATA,"OSRectDimensions are left %u, top %u, "
                    "right %u, bottom %u",left,top,right,bottom);
        }
    }

    void set(int32_t _left, int32_t _top,int32_t _right, int32_t _bottom,
            int32_t _isValid){
        left = _left;
        top = _top;
        right = _right;
        bottom = _bottom;
        isValid = _isValid;
    }

    void set(OSRectDimensions& osrect){
        left = osrect.left;
        top = osrect.top;
        right = osrect.right;
        bottom = osrect.bottom;
        isValid = osrect.isValid;
    }

    void set(int valid) {
        isValid = valid;
    }

    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    int32_t isValid;
};

enum eConfigChangeState {
    CONFIG_CHANGE_START_BEGIN,
    CONFIG_CHANGE_START_INPROGRESS,
    CONFIG_CHANGE_START_FINISH,

    CONFIG_CHANGE_DO_BEGIN,
    CONFIG_CHANGE_DO_INPROGRESS,
    CONFIG_CHANGE_DO_FINISH,

    CONFIG_CHANGE_STOP_BEGIN,
    CONFIG_CHANGE_STOP_INPROGRESS,
    CONFIG_CHANGE_STOP_FINISH
};

class ConfigChangeState {

private:
    int mState;
    mutable android::Mutex mConfigChangeStateLock;
    static ConfigChangeState* sConfigChangeState;

public:

    ConfigChangeState() {
        mState = CONFIG_CHANGE_STOP_FINISH;
    }

    const char* getConfigStateString(int state) {

        int sstate = static_cast<qhwc::eConfigChangeState>(state);
        switch(state) {
            case CONFIG_CHANGE_START_BEGIN:
                 return "CONFIG_CHANGE_START_BEGIN";
            case CONFIG_CHANGE_START_INPROGRESS:
                 return "CONFIG_CHANGE_START_INPROGRESS";
            case CONFIG_CHANGE_START_FINISH:
                 return "CONFIG_CHANGE_START_FINISH";

            case CONFIG_CHANGE_DO_BEGIN:
                return "CONFIG_CHANGE_DO_BEGIN";
            case CONFIG_CHANGE_DO_INPROGRESS:
                return "CONFIG_CHANGE_DO_INPROGRESS";
            case CONFIG_CHANGE_DO_FINISH:
                return "CONFIG_CHANGE_DO_FINISH";

            case CONFIG_CHANGE_STOP_BEGIN:
                return "CONFIG_CHANGE_STOP_BEGIN";
            case CONFIG_CHANGE_STOP_INPROGRESS:
                return "CONFIG_CHANGE_STOP_INPROGRESS";
            case CONFIG_CHANGE_STOP_FINISH:
                return "CONFIG_CHANGE_STOP_FINISH";
            default:
                return "INVALID CONFIG_CHANGE_STATE";
        }
    }

    int getState() {
        android::Mutex::Autolock lock(mConfigChangeStateLock);
        return mState;
    }

    bool setState(int state){
        android::Mutex::Autolock lock(mConfigChangeStateLock);
        if(state >= CONFIG_CHANGE_START_BEGIN and
                state <= CONFIG_CHANGE_STOP_FINISH) {
            ALOGE_IF(DEBUG_METADATA,"Changing the state from %s to %s",
                    getConfigStateString(mState),
                    getConfigStateString(state));
            mState = state;
            return true;
        } else {
            ALOGE_IF(DEBUG_METADATA,"No state change from %s to %s",
                    getConfigStateString(mState),
                    getConfigStateString(state));
        }
        return false;
    }

    /* Gets an instance if one does not already exist */
    static ConfigChangeState* getInstance() {
        if(!sConfigChangeState)
            sConfigChangeState = new ConfigChangeState();
        return sConfigChangeState;
    }
};
}; // namespace qhwc

#endif // HWC_PPMETADATA_H
