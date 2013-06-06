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
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "hwc_pllchange"

#include <utils/Log.h>
#include <utils/Trace.h>
#include <binder/IServiceManager.h>
#include "hwc_ppmetadata.h"
#include <hwc_service.h>
#include <hwc_utils.h>
#include <fb_priv.h>
#include <cutils/properties.h>


using namespace android;
using namespace hwcService;


int hwc_setoutputPLL(float mFps)
{
    sp<IServiceManager> gServiceManager = NULL;
    sp<IHWComposer> gBinder = NULL;
    qhwc::ConfigChangeParams params;
    gServiceManager = defaultServiceManager();
    if(gServiceManager != NULL)
    {
        gBinder = interface_cast<IHWComposer>(gServiceManager->getService(String16("display.hwcservice")));
        if(gBinder != NULL)
        {
            ALOGE("startConfigChange ");
            gBinder->startConfigChange(qhwc::PIXEL_CLOCK_CORRECTION);
            params.param2 = 0;
            params.param1 = mFps;
            ALOGE("doConfigChange ");
            ALOGE("Output PLL settings for %f",mFps);
            gBinder->doConfigChange(qhwc::PIXEL_CLOCK_CORRECTION,params);
            ALOGE("Output PLL settings change Done");
            gBinder->stopConfigChange(qhwc::PIXEL_CLOCK_CORRECTION);
            ALOGE("stopConfigChange Done");
        }
    }
    return 0;
}
