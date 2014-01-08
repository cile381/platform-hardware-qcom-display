/*
* Copyright (c) 2013-2014 The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
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

#ifndef HWC_VPU_H
#define HWC_VPU_H

#include <sys/types.h>
#include "hwc_utils.h"

#define MAX_PIPES_PER_LAYER 2
#define MAX_LAYERS          16

//Forward declarations
struct hwc_display_contents_1;
typedef struct hwc_display_contents_1 hwc_display_contents_1_t;
struct hwc_layer_1;
typedef struct hwc_layer_1 hwc_layer_1_t;
struct hwc_context_t;

namespace vpu {
class VPU;
};
namespace android {
class Parcel;
};
using namespace vpu;

namespace qhwc {

class VPUClient {
public:
    VPUClient(hwc_context_t *ctx);

    ~VPUClient();

    int setupVpuSession(hwc_context_t *ctx, int display,
                                            hwc_display_contents_1_t* list);
    int prepare(hwc_context_t *ctx, int display,
                                            hwc_display_contents_1_t* list);

    int draw(hwc_context_t *ctx, int display, hwc_display_contents_1_t* list);

    int processCommand(uint32_t command,
                const android::Parcel* inParcel, android::Parcel* outParcel);

    int getPipeId(int dpy, int layer, int pipenum);
    int setPipeId(int dpy, int layer, int pipe);
    int setPipeId(int dpy, int layer, int lPipe, int rPipe);

    int getLayerFormat(int dpy, int layer);
    int getWidth(int dpy, int layer);
    int getHeight(int dpy, int layer);

    bool supportedVPULayer(hwc_context_t* ctx, hwc_layer_1_t* layer, int dpy,
            int idx);

    // To check if the current frame is a first video frame
    bool isFirstBuffer(int dpy, int layer) {
        return mProp[dpy][layer].firstBuffer;
    }
    // mark the first buffer displayed.
    void firstBufferDisplayed(int dpy, int layer) {
        mProp[dpy][layer].firstBuffer = false;
    }
    // dummy buffer
    private_handle_t* getDummyHandle(int dpy, int layer) {
        return mHandle[dpy][layer];
    }
    private_handle_t* mHnd;
private:
    vpu::VPU *mVPU;
    void* mVPULib;

    /* VpuLayerProp struct:
     *  This struct corresponds to only one layer
     *  pipeCount: number of pipes required for a layer
     *  pipeID[]: pipe ids corresponding to the layer
     */
    struct VpuLayerProp {
        int format;
        int width;
        int height;
        int pipeCount;
        bool vpuPipe;
        hwc_frect_t sourceCropf;
        bool firstBuffer;
        int pipeID[MAX_PIPES_PER_LAYER];
    };

    VpuLayerProp mProp[HWC_NUM_DISPLAY_TYPES][MAX_LAYERS];
    /* dummy buffer handle */
    private_handle_t* mHandle[HWC_NUM_DISPLAY_TYPES][MAX_LAYERS];

    int mDebugLogs;
    int32_t isDebug() { return (mDebugLogs == 1); }
    int32_t isDebug2() { return (mDebugLogs >= 2 ); }
}; // class VPU
}; // namespace qhwc
#endif /* end of include guard: HWC_VPU_H */
