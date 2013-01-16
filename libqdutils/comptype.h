/*
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
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

#ifndef INCLUDE_LIBQCOM_COMPTYPES
#define INCLUDE_LIBQCOM_COMPTYPES

#include <stdint.h>
#include <utils/Singleton.h>
#include <cutils/properties.h>
#include <mdp_version.h>
#include <sys/sysinfo.h>
#include <cutils/log.h>

#define RAM_SIZE 512*1024*1024
#define TARGET_WIDTH 540
#define TARGET_HEIGHT 960

using namespace android;
namespace qdutils {
// Enum containing the supported composition types
enum {
    COMPOSITION_TYPE_GPU = 0,
    COMPOSITION_TYPE_MDP = 0x1,
    COMPOSITION_TYPE_C2D = 0x2,
    COMPOSITION_TYPE_CPU = 0x4,
    COMPOSITION_TYPE_DYN = 0x8
};

/* This class caches the composition type
 */
class QCCompositionType : public Singleton <QCCompositionType>
{
    public:
        QCCompositionType();
        ~QCCompositionType() { }
        int getCompositionType() {return mCompositionType;}
        void changeTargetCompositionType(int32_t width, int32_t height);
   private:
       int mCompositionType;
       int32_t fb_width;
       int32_t fb_height;

};

}; //namespace qdutils
#endif //INCLUDE_LIBQCOM_COMPTYPES
