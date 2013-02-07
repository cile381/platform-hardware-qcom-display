/*
 * Copyright (c) 2012, Linux Foundation. All rights reserved.
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
 *   * Neither the name of Linux Foundation nor the names of its
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

#ifndef LOG_TAG
#define LOG_TAG "qsfdump"
#endif
#define LOG_NDEBUG 0
#include <hwc_dump_layers.h>
#include <hwc_utils.h>
#include <utils/String8.h>
#include <cutils/log.h>
#include <sys/stat.h>
#include <comptype.h>
#include <SkBitmap.h>
#include <SkImageEncoder.h>

namespace qhwc {

  int HwcDebug::sDumpCntLimRaw = 0;
  int HwcDebug::sDumpCntrRaw = 1;
  char HwcDebug::sDumpPropStrRaw[PROPERTY_VALUE_MAX] = "";
  char HwcDebug::sDumpDirRaw[PATH_MAX] = "";
  int HwcDebug::sDumpCntLimPng = 0;
  int HwcDebug::sDumpCntrPng = 1;
  char HwcDebug::sDumpPropStrPng[PROPERTY_VALUE_MAX] = "";
  char HwcDebug::sDumpDirPng[PATH_MAX] = "";

// MAX_ALLOWED_FRAMEDUMPS must be capped to (LONG_MAX - 1)
// 60fps => 216000 frames per hour
// Below setting of 216000 * 24 * 7 => 1 week or 168 hours of capture.
  enum {
    MAX_ALLOWED_FRAMEDUMPS = (216000 * 24 * 7)
  };

void HwcDebug::dumpLayers(hwc_layer_list_t* list)
{
    // Check need for dumping layers for debugging.
    if (UNLIKELY(needToDumpLayers()) && LIKELY(list)) {
        logHwcProps(list->flags);
        for (size_t i = 0; i < list->numHwLayers; i++) {
            logLayer(i, list->hwLayers);
            dumpLayer(i, list->hwLayers);
        }
    }
}

bool HwcDebug::needToDumpLayers()
{
    bool bDumpLayer = false;
    char dumpPropStr[PROPERTY_VALUE_MAX];
    // Enable dumping only if debug.sf.dump property is non-empty on boot.
    static bool sDumpEnable = property_get("debug.sf.dump", dumpPropStr, NULL) ?
                                true : false;
    time_t timeNow;
    tm dumpTime;

    if (false == sDumpEnable)
        return false;

    time(&timeNow);
    localtime_r(&timeNow, &dumpTime);

    if ((property_get("debug.sf.dump.png", dumpPropStr, NULL) > 0) &&
            (strncmp(dumpPropStr, sDumpPropStrPng, PROPERTY_VALUE_MAX - 1))) {
        // Strings exist & not equal implies it has changed, so trigger a dump
        strncpy(sDumpPropStrPng, dumpPropStr, PROPERTY_VALUE_MAX - 1);
        sDumpCntLimPng = atoi(dumpPropStr);
        if (sDumpCntLimPng > MAX_ALLOWED_FRAMEDUMPS) {
            ALOGW("Warning: Using debug.sf.dump.png %d (= max)",
                MAX_ALLOWED_FRAMEDUMPS);
            sDumpCntLimPng = MAX_ALLOWED_FRAMEDUMPS;
        }
        sDumpCntLimPng = (sDumpCntLimPng < 0) ? 0: sDumpCntLimPng;
        if (sDumpCntLimPng) {
            sprintf(sDumpDirPng,
                    "/data/sfdump.png.%04d.%02d.%02d.%02d.%02d.%02d",
                    dumpTime.tm_year + 1900, dumpTime.tm_mon + 1,
                    dumpTime.tm_mday, dumpTime.tm_hour,
                    dumpTime.tm_min, dumpTime.tm_sec);
            if (0 == mkdir(sDumpDirPng, 0777))
                sDumpCntrPng = 0;
            else {
                ALOGE("Error: %s. Failed to create sfdump directory: %s",
                    strerror(errno), sDumpDirPng);
                sDumpCntrPng = sDumpCntLimPng + 1;
            }
        }
    }

    if (sDumpCntrPng <= sDumpCntLimPng)
        sDumpCntrPng++;

    if ((property_get("debug.sf.dump", dumpPropStr, NULL) > 0) &&
            (strncmp(dumpPropStr, sDumpPropStrRaw, PROPERTY_VALUE_MAX - 1))) {
        // Strings exist & not equal implies it has changed, so trigger a dump
        strncpy(sDumpPropStrRaw, dumpPropStr, PROPERTY_VALUE_MAX - 1);
        sDumpCntLimRaw = atoi(dumpPropStr);
        if (sDumpCntLimRaw > MAX_ALLOWED_FRAMEDUMPS) {
            ALOGW("Warning: Using debug.sf.dump %d (= max)",
                MAX_ALLOWED_FRAMEDUMPS);
            sDumpCntLimRaw = MAX_ALLOWED_FRAMEDUMPS;
        }
        sDumpCntLimRaw = (sDumpCntLimRaw < 0) ? 0: sDumpCntLimRaw;
        if (sDumpCntLimRaw) {
            sprintf(sDumpDirRaw,
                    "/data/sfdump.raw.%04d.%02d.%02d.%02d.%02d.%02d",
                    dumpTime.tm_year + 1900, dumpTime.tm_mon + 1,
                    dumpTime.tm_mday, dumpTime.tm_hour,
                    dumpTime.tm_min, dumpTime.tm_sec);
            if (0 == mkdir(sDumpDirRaw, 0777))
                sDumpCntrRaw = 0;
            else {
                ALOGE("Error: %s. Failed to create sfdump directory: %s",
                    strerror(errno), sDumpDirRaw);
                sDumpCntrRaw = sDumpCntLimRaw + 1;
            }
        }
    }

    if (sDumpCntrRaw <= sDumpCntLimRaw)
        sDumpCntrRaw++;

    bDumpLayer = (sDumpCntLimPng || sDumpCntLimRaw)? true : false;
    return bDumpLayer;
}

void HwcDebug::logHwcProps(uint32_t listFlags)
{
    static int hwcModuleCompType = -1;
    static int sMdpCompMaxLayers = 0;
    static String8 hwcModuleCompTypeLog("");
    if (-1 == hwcModuleCompType) {
        // One time stuff
        char mdpCompPropStr[PROPERTY_VALUE_MAX];
        if (property_get("debug.mdpcomp.maxlayer", mdpCompPropStr, NULL) > 0) {
            sMdpCompMaxLayers = atoi(mdpCompPropStr);
        }
        hwcModuleCompType =
            qdutils::QCCompositionType::getInstance().getCompositionType();
        hwcModuleCompTypeLog.appendFormat("%s%s%s%s%s%s",
            // Is hwc module composition type now a bit-field?!
            (hwcModuleCompType == qdutils::COMPOSITION_TYPE_GPU)?
                "[GPU]": "",
            (hwcModuleCompType & qdutils::COMPOSITION_TYPE_MDP)?
                "[MDP]": "",
            (hwcModuleCompType & qdutils::COMPOSITION_TYPE_C2D)?
                "[C2D]": "",
            (hwcModuleCompType & qdutils::COMPOSITION_TYPE_CPU)?
                "[CPU]": "",
            (hwcModuleCompType & qdutils::COMPOSITION_TYPE_DYN)?
                "[DYN]": "",
            (hwcModuleCompType >= (qdutils::COMPOSITION_TYPE_DYN << 1))?
                "[???]": "");
    }
    ALOGI("Layer[*] %s-HwcModuleCompType, %d-layer MdpComp %s",
        hwcModuleCompTypeLog.string(), sMdpCompMaxLayers,
        (listFlags & HWC_GEOMETRY_CHANGED)? "[HwcList Geometry Changed]": "");
}

void HwcDebug::logLayer(size_t layerIndex, hwc_layer_t hwLayers[])
{
    if (NULL == hwLayers) {
        ALOGE("Layer[%d] Error. No hwc layers to log.", layerIndex);
        return;
    }

    hwc_layer *layer = &hwLayers[layerIndex];
    hwc_rect_t sourceCrop = layer->sourceCrop;
    hwc_rect_t displayFrame = layer->displayFrame;
    size_t numHwcRects = layer->visibleRegionScreen.numRects;
    hwc_rect_t const *hwcRects = layer->visibleRegionScreen.rects;
    private_handle_t *hnd = (private_handle_t *)layer->handle;

    char pixFormatStr[32] = "None";
    String8 hwcVisRegsScrLog("[None]");

    for (size_t i = 0 ; (hwcRects && (i < numHwcRects)); i++) {
        if (0 == i)
            hwcVisRegsScrLog.clear();
        hwcVisRegsScrLog.appendFormat("[%dl, %dt, %dr, %db]",
                                        hwcRects[i].left, hwcRects[i].top,
                                        hwcRects[i].right, hwcRects[i].bottom);
    }

    if (hnd)
        getHalPixelFormatStr(hnd->format, pixFormatStr);

    // Log Line 1
    ALOGI("Layer[%d] SrcBuff[%dx%d] SrcCrop[%dl, %dt, %dr, %db] "
        "DispFrame[%dl, %dt, %dr, %db] VisRegsScr%s", layerIndex,
        (hnd)? hnd->width : -1, (hnd)? hnd->height : -1,
        sourceCrop.left, sourceCrop.top,
        sourceCrop.right, sourceCrop.bottom,
        displayFrame.left, displayFrame.top,
        displayFrame.right, displayFrame.bottom,
        hwcVisRegsScrLog.string());
    // Log Line 2
    ALOGI("Layer[%d] LayerCompType = %s, Format = %s, "
        "Orientation = %s, Flags = %s%s%s%s%s, Hints = %s%s%s, "
        "Blending = %s%s%s", layerIndex,
        (layer->compositionType == HWC_FRAMEBUFFER)? "Framebuffer(GPU)":
            (layer->compositionType == HWC_OVERLAY)? "Overlay":
            (layer->compositionType == HWC_BACKGROUND)? "Background":
            (layer->compositionType == HWC_USE_COPYBIT)? "Copybit": "???",
         pixFormatStr,
         (layer->transform == 0)? "ROT_0":
             (layer->transform == HWC_TRANSFORM_FLIP_H)? "FLIP_H":
             (layer->transform == HWC_TRANSFORM_FLIP_V)? "FLIP_V":
             (layer->transform == HWC_TRANSFORM_ROT_90)? "ROT_90":
             (layer->transform == HWC_TRANSFORM_ROT_180)? "ROT_180":
             (layer->transform == HWC_TRANSFORM_ROT_270)? "ROT_270":
                                                        "ROT_INVALID",
         (layer->flags)? "": "[None]",
         (layer->flags & HWC_SKIP_LAYER)? "[Skip layer]":"",
         (layer->flags & qhwc::HWC_MDPCOMP)? "[MDP Comp]":"",
         (layer->flags & qhwc::HWC_LAYER_RESERVED_0)? "[Layer Reserved 0]":"",
         (layer->flags & qhwc::HWC_LAYER_RESERVED_1)? "[Layer Reserved 1]":"",
         (layer->hints)? "":"[None]",
         (layer->hints & HWC_HINT_TRIPLE_BUFFER)? "[Triple Buffer]":"",
         (layer->hints & HWC_HINT_CLEAR_FB)? "[Clear FB]":"",
         (layer->blending == HWC_BLENDING_NONE)? "[None]":"",
         (layer->blending == HWC_BLENDING_PREMULT)? "[PreMult]":"",
         (layer->blending == HWC_BLENDING_COVERAGE)? "[Coverage]":"");
}

void HwcDebug::dumpLayer(size_t layerIndex, hwc_layer_t hwLayers[])
{
    char dumpLogStrPng[128] = "";
    char dumpLogStrRaw[128] = "";
    bool needDumpPng = (sDumpCntrPng <= sDumpCntLimPng)? true:false;
    bool needDumpRaw = (sDumpCntrRaw <= sDumpCntLimRaw)? true:false;

    if (needDumpPng) {
        sprintf(dumpLogStrPng, "[png-dump-frame: %03d of %03d]", sDumpCntrPng,
            sDumpCntLimPng);
    }
    if (needDumpRaw) {
        sprintf(dumpLogStrRaw, "[raw-dump-frame: %03d of %03d]", sDumpCntrRaw,
            sDumpCntLimRaw);
    }

    if (!(needDumpPng || needDumpRaw))
        return;

    if (NULL == hwLayers) {
        ALOGE("Layer[%d] %s%s Error: No hwc layers to dump.", layerIndex,
            dumpLogStrRaw, dumpLogStrPng);
        return;
    }

    hwc_layer *layer = &hwLayers[layerIndex];
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    char pixFormatStr[32] = "None";

    if (NULL == hnd) {
        ALOGI("Layer[%d] %s%s Skipping dump: Bufferless layer.", layerIndex,
            dumpLogStrRaw, dumpLogStrPng);
        return;
    }

    getHalPixelFormatStr(hnd->format, pixFormatStr);

    if (needDumpPng && hnd->base) {
        bool bResult = false;
        char dumpFilename[PATH_MAX];
        SkBitmap *tempSkBmp = new SkBitmap();
        SkBitmap::Config tempSkBmpConfig = SkBitmap::kNo_Config;
        sprintf(dumpFilename, "%s/sfdump%03d.layer%d.png", sDumpDirPng,
            sDumpCntrPng, layerIndex);

        switch (hnd->format) {
            case HAL_PIXEL_FORMAT_RGBA_8888:
            case HAL_PIXEL_FORMAT_RGBX_8888:
            case HAL_PIXEL_FORMAT_BGRA_8888:
                tempSkBmpConfig = SkBitmap::kARGB_8888_Config;
                break;
            case HAL_PIXEL_FORMAT_RGB_565:
            case HAL_PIXEL_FORMAT_RGBA_5551:
            case HAL_PIXEL_FORMAT_RGBA_4444:
                tempSkBmpConfig = SkBitmap::kRGB_565_Config;
                break;
            case HAL_PIXEL_FORMAT_RGB_888:
            default:
                tempSkBmpConfig = SkBitmap::kNo_Config;
                break;
        }
        if (SkBitmap::kNo_Config != tempSkBmpConfig) {
            tempSkBmp->setConfig(tempSkBmpConfig, hnd->width, hnd->height);
            tempSkBmp->setPixels((void*)hnd->base);
            bResult = SkImageEncoder::EncodeFile(dumpFilename,
                                    *tempSkBmp, SkImageEncoder::kPNG_Type, 100);
            ALOGI("Layer[%d] %s Dump to %s: %s", layerIndex, dumpLogStrPng,
                dumpFilename, bResult ? "Success" : "Fail");
        } else {
            ALOGI("Layer[%d] %s Skipping dump: Unsupported layer format %s for "
                "png encoder", layerIndex, dumpLogStrPng, pixFormatStr);
        }
        delete tempSkBmp; // Calls SkBitmap::freePixels() internally.
    }

    if (needDumpRaw && hnd->base) {
        char dumpFilename[PATH_MAX];
        bool bResult = false;
        sprintf(dumpFilename, "%s/sfdump%03d.layer%d.%dx%d.%s.raw",
            sDumpDirRaw, sDumpCntrRaw,
            layerIndex, hnd->width, hnd->height,
            pixFormatStr);
        FILE* fp = fopen(dumpFilename, "w+");
        if (NULL != fp) {
            bResult = (bool) fwrite((void*)hnd->base, hnd->size, 1, fp);
            fclose(fp);
        }
        ALOGI("Layer[%d] %s Dump to %s: %s", layerIndex, dumpLogStrRaw,
            dumpFilename, bResult ? "Success" : "Fail");
    }
}

void HwcDebug::getHalPixelFormatStr(int format, char pixFormatStr[])
{
    if (!pixFormatStr)
        return;

    switch(format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            strcpy(pixFormatStr, "RGBA_8888");
            break;
        case HAL_PIXEL_FORMAT_RGBX_8888:
            strcpy(pixFormatStr, "RGBX_8888");
            break;
        case HAL_PIXEL_FORMAT_RGB_888:
            strcpy(pixFormatStr, "RGB_888");
            break;
        case HAL_PIXEL_FORMAT_RGB_565:
            strcpy(pixFormatStr, "RGB_565");
            break;
        case HAL_PIXEL_FORMAT_BGRA_8888:
            strcpy(pixFormatStr, "BGRA_8888");
            break;
        case HAL_PIXEL_FORMAT_RGBA_5551:
            strcpy(pixFormatStr, "RGBA_5551");
            break;
        case HAL_PIXEL_FORMAT_RGBA_4444:
            strcpy(pixFormatStr, "RGBA_4444");
            break;
        case HAL_PIXEL_FORMAT_YV12:
            strcpy(pixFormatStr, "YV12");
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            strcpy(pixFormatStr, "YCbCr_422_SP_NV16");
            break;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            strcpy(pixFormatStr, "YCrCb_420_SP_NV21");
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            strcpy(pixFormatStr, "YCbCr_422_I_YUY2");
            break;
        case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
            strcpy(pixFormatStr, "NV12_ENCODEABLE");
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:
            strcpy(pixFormatStr, "YCbCr_420_SP_TILED_TILE_4x2");
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            strcpy(pixFormatStr, "YCbCr_420_SP");
            break;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO:
            strcpy(pixFormatStr, "YCrCb_420_SP_ADRENO");
            break;
        case HAL_PIXEL_FORMAT_YCrCb_422_SP:
            strcpy(pixFormatStr, "YCrCb_422_SP");
            break;
        case HAL_PIXEL_FORMAT_R_8:
            strcpy(pixFormatStr, "R_8");
            break;
        case HAL_PIXEL_FORMAT_RG_88:
            strcpy(pixFormatStr, "RG_88");
            break;
        case HAL_PIXEL_FORMAT_INTERLACE:
            strcpy(pixFormatStr, "INTERLACE");
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
            strcpy(pixFormatStr, "YCbCr_420_SP_VENUS");
            break;
        default:
            sprintf(pixFormatStr, "Unknown0x%X", format);
            break;
    }
}

} // namespace qhwc

