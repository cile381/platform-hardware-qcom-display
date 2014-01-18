/*
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
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
#include <cutils/log.h>
#include <linux/msm_mdp.h>
#include "mdp_version.h"

#define DEBUG 0

ANDROID_SINGLETON_STATIC_INSTANCE(qdutils::MDPVersion);
namespace qdutils {

#define TOKEN_PARAMS_DELIM  "="

MDPVersion::MDPVersion()
{
    mMDPVersion = MDSS_V5;
    mMdpRev = 0;
    mRGBPipes = 0;
    mVGPipes = 0;
    mDMAPipes = 0;
    mFeatures = 0;
    mMDPUpscale = 0;
    mMDPDownscale = 0;
    mMacroTileEnabled = false;
    mPanelType = NO_PANEL;
    mLowBw = 0;
    mHighBw = 0;

    if(!updatePanelInfo()) {
        ALOGE("Unable to read Primary Panel Information");
    }
    if(!updateSysFsInfo()) {
        ALOGE("Unable to read display sysfs node");
    }
    if (mMdpRev == MDP_V3_0_4){
        mMDPVersion = MDP_V3_0_4;
    }

    mHasOverlay = false;
    if((mMDPVersion >= MDP_V4_0) ||
       (mMDPVersion == MDP_V_UNKNOWN) ||
       (mMDPVersion == MDP_V3_0_4))
        mHasOverlay = true;
    if(!updateSplitInfo()) {
        ALOGE("Unable to read display split node");
    }
}

MDPVersion::~MDPVersion() {
    close(mFd);
}

int MDPVersion::tokenizeParams(char *inputParams, const char *delim,
                                char* tokenStr[], int *idx) {
    char *tmp_token = NULL;
    char *temp_ptr;
    int ret = 0, index = 0;
    if (!inputParams) {
        return -1;
    }
    tmp_token = strtok_r(inputParams, delim, &temp_ptr);
    while (tmp_token != NULL) {
        tokenStr[index++] = tmp_token;
        tmp_token = strtok_r(NULL, " ", &temp_ptr);
    }
    *idx = index;
    return 0;
}
// This function reads the sysfs node to read the primary panel type
// and updates information accordingly
bool MDPVersion::updatePanelInfo() {
    FILE *displayDeviceFP = NULL;
    const int MAX_FRAME_BUFFER_NAME_SIZE = 128;
    char fbType[MAX_FRAME_BUFFER_NAME_SIZE];
    const char *strCmdPanel = "mipi dsi cmd panel";
    const char *strVideoPanel = "mipi dsi video panel";
    const char *strLVDSPanel = "lvds panel";
    const char *strEDPPanel = "edp panel";

    displayDeviceFP = fopen("/sys/class/graphics/fb0/msm_fb_type", "r");
    if(displayDeviceFP){
        fread(fbType, sizeof(char), MAX_FRAME_BUFFER_NAME_SIZE,
                displayDeviceFP);
        if(strncmp(fbType, strCmdPanel, strlen(strCmdPanel)) == 0) {
            mPanelType = MIPI_CMD_PANEL;
        }
        else if(strncmp(fbType, strVideoPanel, strlen(strVideoPanel)) == 0) {
            mPanelType = MIPI_VIDEO_PANEL;
        }
        else if(strncmp(fbType, strLVDSPanel, strlen(strLVDSPanel)) == 0) {
            mPanelType = LVDS_PANEL;
        }
        else if(strncmp(fbType, strEDPPanel, strlen(strEDPPanel)) == 0) {
            mPanelType = EDP_PANEL;
        }
        fclose(displayDeviceFP);
        return true;
    }else {
        return false;
    }
}

// This function reads the sysfs node to read MDP capabilities
// and parses and updates information accordingly.
bool MDPVersion::updateSysFsInfo() {
    FILE *sysfsFd;
    size_t len = 0;
    ssize_t read;
    char *line = NULL;
    char sysfsPath[255];
    memset(sysfsPath, 0, sizeof(sysfsPath));
    snprintf(sysfsPath , sizeof(sysfsPath),
            "/sys/class/graphics/fb0/mdp/caps");
    char property[PROPERTY_VALUE_MAX];
    bool enableMacroTile = false;

    if((property_get("persist.hwc.macro_tile_enable", property, NULL) > 0) &&
       (!strncmp(property, "1", PROPERTY_VALUE_MAX ) ||
        (!strncasecmp(property,"true", PROPERTY_VALUE_MAX )))) {
        enableMacroTile = true;
    }

    sysfsFd = fopen(sysfsPath, "rb");

    if (sysfsFd == NULL) {
        ALOGE("%s: sysFsFile file '%s' not found",
                __FUNCTION__, sysfsPath);
        return false;
    } else {
        while((read = getline(&line, &len, sysfsFd)) != -1) {
            int index=0;
            char *tokens[10];
            memset(tokens, 0, sizeof(tokens));

            // parse the line and update information accordingly
            if(!tokenizeParams(line, TOKEN_PARAMS_DELIM, tokens, &index)) {
                if(!strncmp(tokens[0], "hw_rev", strlen("hw_rev"))) {
                    mMdpRev = atoi(tokens[1]);
                }
                else if(!strncmp(tokens[0], "rgb_pipes", strlen("rgb_pipes"))) {
                    mRGBPipes = atoi(tokens[1]);
                }
                else if(!strncmp(tokens[0], "vig_pipes", strlen("vig_pipes"))) {
                    mVGPipes = atoi(tokens[1]);
                }
                else if(!strncmp(tokens[0], "dma_pipes", strlen("dma_pipes"))) {
                    mDMAPipes = atoi(tokens[1]);
                }
                else if(!strncmp(tokens[0], "max_downscale_ratio",
                                strlen("max_downscale_ratio"))) {
                    mMDPDownscale = atoi(tokens[1]);
                }
                else if(!strncmp(tokens[0], "max_upscale_ratio",
                                strlen("max_upscale_ratio"))) {
                    mMDPUpscale = atoi(tokens[1]);
                } else if(!strncmp(tokens[0], "max_bandwidth_low",
                        strlen("max_bandwidth_low"))) {
                    mLowBw = atol(tokens[1]);
                } else if(!strncmp(tokens[0], "max_bandwidth_high",
                        strlen("max_bandwidth_high"))) {
                    mHighBw = atol(tokens[1]);
                } else if(!strncmp(tokens[0], "features", strlen("features"))) {
                    for(int i=1; i<index;i++) {
                        if(!strncmp(tokens[i], "bwc", strlen("bwc"))) {
                           mFeatures |= MDP_BWC_EN;
                        }
                        else if(!strncmp(tokens[i], "decimation",
                                    strlen("decimation"))) {
                           mFeatures |= MDP_DECIMATION_EN;
                        }
                        else if(!strncmp(tokens[i], "tile_format",
                                    strlen("tile_format"))) {
                           if(enableMacroTile)
                               mMacroTileEnabled = true;
                        }
                    }
                }
            }
            free(line);
            line = NULL;
        }
        fclose(sysfsFd);
    }
    ALOGD_IF(DEBUG, "%s: mMDPVersion: %d mMdpRev: %x mRGBPipes:%d,"
                    "mVGPipes:%d", __FUNCTION__, mMDPVersion, mMdpRev,
                    mRGBPipes, mVGPipes);
    ALOGD_IF(DEBUG, "%s:mDMAPipes:%d \t mMDPDownscale:%d, mFeatures:%d",
                     __FUNCTION__,  mDMAPipes, mMDPDownscale, mFeatures);
    ALOGD_IF(DEBUG, "%s:mLowBw: %lu mHighBw: %lu", __FUNCTION__,  mLowBw,
            mHighBw);

    return true;
}

// This function reads the sysfs node to read MDP capabilities
// and parses and updates information accordingly.
bool MDPVersion::updateSplitInfo() {
    if(mMDPVersion >= MDSS_V5) {
        char split[64] = {0};
        FILE* fp = fopen("/sys/class/graphics/fb0/msm_fb_split", "r");
        if(fp){
            //Format "left right" space as delimiter
            if(fread(split, sizeof(char), 64, fp)) {
                mSplit.mLeft = atoi(split);
                ALOGI_IF(mSplit.mLeft, "Left Split=%d", mSplit.mLeft);
                char *rght = strpbrk(split, " ");
                if(rght)
                    mSplit.mRight = atoi(rght + 1);
                ALOGI_IF(mSplit.mRight, "Right Split=%d", mSplit.mRight);
            }
        } else {
            ALOGE("Failed to open mdss_fb_split node");
            return false;
        }
        if(fp)
            fclose(fp);
    }
    return true;
}


bool MDPVersion::supportsDecimation() {
    return mFeatures & MDP_DECIMATION_EN;
}

uint32_t MDPVersion::getMaxMDPDownscale() {
    return mMDPDownscale;
}

uint32_t MDPVersion::getMaxMDPUpscale() {
    return mMDPUpscale;
}

bool MDPVersion::supportsBWC() {
    // BWC - Bandwidth Compression
    return (mFeatures & MDP_BWC_EN);
}

bool MDPVersion::supportsMacroTile() {
    // MACRO TILE support
    return mMacroTileEnabled;
}

}; //namespace qdutils

