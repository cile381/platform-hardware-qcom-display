/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, Linux Foundation. All rights reserved.
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

typedef struct _VideoPPData {
    uint32_t ops;
    int32_t hue;
    float saturation;
    int32_t intensity;
    float contrast;
    uint32_t sharpness;
    bool isValid;
} VideoPPData;
// ----------------------------------------------------------------------------
}; // namespace qhwc

#endif // HWC_PPMETADATA_H
