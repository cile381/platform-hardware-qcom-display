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
 * SUBSTITUTE GOODS OR CLIENTS; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <hwc_qclient.h>
#include <IQService.h>
#include <hwc_utils.h>
#include <hwc_vpuclient.h>

#define QCLIENT_DEBUG 0

using namespace android;
using namespace qService;

namespace qClient {

// ----------------------------------------------------------------------------
QClient::QClient(hwc_context_t *ctx) : mHwcContext(ctx),
        mMPDeathNotifier(new MPDeathNotifier(ctx))
{
    ALOGD_IF(QCLIENT_DEBUG, "QClient Constructor invoked");
}

QClient::~QClient()
{
    ALOGD_IF(QCLIENT_DEBUG,"QClient Destructor invoked");
}

static void securing(hwc_context_t *ctx, uint32_t startEnd) {
    Locker::Autolock _sl(ctx->mDrawLock);
    //The only way to make this class in this process subscribe to media
    //player's death.
    IMediaDeathNotifier::getMediaPlayerService();

    ctx->mSecuring = startEnd;
    //We're done securing
    if(startEnd == IQService::END)
        ctx->mSecureMode = true;
    if(ctx->proc)
        ctx->proc->invalidate(ctx->proc);
}

static void unsecuring(hwc_context_t *ctx, uint32_t startEnd) {
    Locker::Autolock _sl(ctx->mDrawLock);
    ctx->mSecuring = startEnd;
    //We're done unsecuring
    if(startEnd == IQService::END)
        ctx->mSecureMode = false;
    if(ctx->proc)
        ctx->proc->invalidate(ctx->proc);
}

void QClient::MPDeathNotifier::died() {
    Locker::Autolock _sl(mHwcContext->mDrawLock);
    ALOGD_IF(QCLIENT_DEBUG, "Media Player died");
    mHwcContext->mSecuring = false;
    mHwcContext->mSecureMode = false;
    if(mHwcContext->proc)
        mHwcContext->proc->invalidate(mHwcContext->proc);
}

static android::status_t screenRefresh(hwc_context_t *ctx) {
    status_t result = NO_INIT;
#ifdef QCOM_BSP
    if(ctx->proc) {
        ctx->proc->invalidate(ctx->proc);
        result = NO_ERROR;
    }
#endif
    return result;
}

static android::status_t vpuCommand(hwc_context_t *ctx,
        uint32_t command,
        const Parcel* inParcel,
        Parcel* outParcel) {
    status_t result = NO_INIT;
#ifdef QCOM_BSP
#ifdef VPU_TARGET
    result = ctx->mVPUClient->processCommand(command, inParcel, outParcel);
#endif
#endif
    return result;
}

static void setExtOrientation(hwc_context_t *ctx, uint32_t orientation) {
    ctx->mExtOrientation = orientation;
}

static void setBufferMirrorMode(hwc_context_t *ctx, uint32_t enable) {
    ctx->mBufferMirrorMode = enable;
}

status_t QClient::notifyCallback(uint32_t command, const Parcel* inParcel,
        Parcel* outParcel) {

    if (command > IQService::VPU_COMMAND_LIST_START &&
        command < IQService::VPU_COMMAND_LIST_END) {
        return vpuCommand(mHwcContext, command, inParcel, outParcel);
    }

    switch(command) {
        case IQService::SECURING:
            securing(mHwcContext, inParcel->readInt32());
            break;
        case IQService::UNSECURING:
            unsecuring(mHwcContext, inParcel->readInt32());
            break;
        case IQService::SCREEN_REFRESH:
            return screenRefresh(mHwcContext);
            break;
        case IQService::EXTERNAL_ORIENTATION:
            setExtOrientation(mHwcContext, inParcel->readInt32());
            break;
        case IQService::BUFFER_MIRRORMODE:
            setBufferMirrorMode(mHwcContext, inParcel->readInt32());
            break;
        default:
            return NO_ERROR;
    }
    return NO_ERROR;
}



}
