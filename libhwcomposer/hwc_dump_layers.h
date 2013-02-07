/*
 * Copyright (c) 2011-2012, Linux Foundation. All rights reserved.
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

#ifndef HWC_DUMP_LAYERS_H
#define HWC_DUMP_LAYERS_H

#include <gralloc_priv.h>
#include <comptype.h>
#include <ui/Region.h>
#include <hardware/hwcomposer.h>
#include "egl_handles.h"

namespace qhwc {

class HwcDebug {
public:
/*
 * Dump layers for debugging based on "debug.sf.dump*" system property.
 * See needToDumpLayers() for usage.
 *
 * @param: list - The HWC layer-list to dump.
 *
 */
static void dumpLayers(hwc_layer_list_t* list);

private:
    // Prevent instantiation.
    HwcDebug();
    ~HwcDebug();

// Using static variables for layer dumping since "property_set("debug.sf.dump",
// property)" does not work.
  static int sDumpCntLimRaw;
  static int sDumpCntrRaw;
  static char sDumpPropStrRaw[PROPERTY_VALUE_MAX];
  static char sDumpDirRaw[PATH_MAX];
  static int sDumpCntLimPng;
  static int sDumpCntrPng;
  static char sDumpPropStrPng[PROPERTY_VALUE_MAX];
  static char sDumpDirPng[PATH_MAX];

/*
 * Checks if layers need to be dumped based on system property "debug.sf.dump"
 * for raw dumps and "debug.sf.dump.png" for png dumps.
 *
 * Note: Set "debug.sf.dump" as non-empty in the device's /system/build.prop
 * file to enable layer logging/capturing feature. The feature is disabled by
 * default to avoid per-frame property_get() calls.
 *
 * For example, to dump 25 frames in raw format, do,
 *     adb shell setprop debug.sf.dump 25
 * Layers are dumped in a time-stamped location: /data/sfdump*.
 *
 * To dump 10 frames in png format, do,
 *     adb shell setprop debug.sf.dump.png 10
 * To dump another 25 or so frames in raw format, do,
 *     adb shell setprop debug.sf.dump 26
 *
 * To turn off logcat logging of layer-info, set both properties to 0,
 *     adb shell setprop debug.sf.dump.png 0
 *     adb shell setprop debug.sf.dump 0
 *
 * @return: true if layers need to be dumped (or logcat-ed).
 */
static bool needToDumpLayers();

/*
 * Log a few per-frame hwc properties into logcat.
 *
 * @param: listFlags - Flags used in hwcomposer's list.
 *
 */
static void logHwcProps(uint32_t listFlags);

/*
 * Log a layer's info into logcat.
 *
 * @param: layerIndex - Index of layer being dumped.
 * @param: hwLayers - Address of hwc_layer_t to log and dump.
 *
 */
static void logLayer(size_t layerIndex, hwc_layer_t hwLayers[]);

/*
 * Dumps a layer buffer into raw/png files.
 *
 * @param: layerIndex - Index of layer being dumped.
 * @param: hwLayers - Address of hwc_layer_t to log and dump.
 *
 */
static void dumpLayer(size_t layerIndex, hwc_layer_t hwLayers[]);

static void getHalPixelFormatStr(int format, char pixelformatstr[]);
};

} // namespace qhwc

#endif /* HWC_DUMP_LAYERS_H */
