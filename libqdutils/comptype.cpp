/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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


#include <utils/Singleton.h>
#include "comptype.h"
#include <cutils/log.h>
#include "soc_id.h"
ANDROID_SINGLETON_STATIC_INSTANCE(qdutils::QCCompositionType);
namespace qdutils {

QCCompositionType::QCCompositionType()
{
   char property[PROPERTY_VALUE_MAX];
   mCompositionType = 0;
   fb_width = fb_height = -1;
   if (property_get("debug.sf.hw", property, NULL) > 0) {
        if(atoi(property) == 0) {
            mCompositionType = COMPOSITION_TYPE_CPU;
        } else { //debug.sf.hw = 1
            property_get("debug.composition.type", property, NULL);
            if (property == NULL) {
                mCompositionType = COMPOSITION_TYPE_GPU;
            } else if ((strncmp(property, "mdp", 3)) == 0) {
                mCompositionType = COMPOSITION_TYPE_MDP;
            } else if ((strncmp(property, "c2d", 3)) == 0) {
                mCompositionType = COMPOSITION_TYPE_C2D;
            } else if ((strncmp(property, "dyn", 3)) == 0) {
                 if (qdutils::MDPVersion::getInstance().getMDPVersion() < 400)
                     mCompositionType =
                         COMPOSITION_TYPE_DYN |COMPOSITION_TYPE_MDP;
                 else
                     mCompositionType =
                         COMPOSITION_TYPE_DYN|COMPOSITION_TYPE_C2D;
            } else {
                mCompositionType = COMPOSITION_TYPE_GPU;
            }
        }
    } else { //debug.sf.hw is not set. Use cpu composition
        mCompositionType = COMPOSITION_TYPE_CPU;
    }

}
void QCCompositionType::changeTargetCompositionType(int32_t width, int32_t height)
{
    if(width>0 && height>0) {
         fb_width = width;
         fb_height = height;
         struct sysinfo info;
         unsigned long int ramSize = -1;
         if (sysinfo(&info)) {
             ALOGE("%s: Problem in reading sysinfo()", __FUNCTION__);
         }
         else {
             ALOGV("%s: total RAM = %lu", __FUNCTION__, info.totalram );
             ramSize = info.totalram ;
         }
         // For MDP3 targets, for panels larger than qHD resolution
         // Set GPU Composition or performance reasons.
         int soc_id = qdutils::SOCId::getInstance().getSOCId();
         char property[PROPERTY_VALUE_MAX];
         if (property_get("debug.sf.hw", property, NULL) > 0) {
             if(atoi(property) != 0) {
               property_get("debug.composition.type", property, NULL);
               if ((strncmp(property, "dyn", 3) == 0) &&
                  (qdutils::MDPVersion::getInstance().getMDPVersion()
                   < MDP_V4_0)) {
                     if(((property_get("debug.sf.gpufor720p",
                          property, NULL) > 0) &&
                          (atoi(property)!=0) &&
                          ((fb_width > TARGET_WIDTH &&
                          fb_height > TARGET_HEIGHT)
                          ||(fb_width > TARGET_HEIGHT &&
                          fb_height > TARGET_WIDTH)))
                          || ((ramSize <= RAM_SIZE) &&
                          ((soc_id == 168)||
                          (soc_id == 169)||(soc_id == 170)))){
                          mCompositionType = COMPOSITION_TYPE_GPU;
                        }
                  }
             }
         }
     }
}
};
