/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2013, 2015 The Linux Foundation. All rights reserved.
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

#include <cutils/properties.h>
#include <utils/Log.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/msm_mdp.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include "hwc_utils.h"
#include "string.h"
#include "external.h"
#include "overlay.h"

namespace qhwc {

#define HWC_VSYNC_THREAD_NAME      "hwcVsyncThread%u"
#define HWC_VSYNC_SYS_NODE_PATH    "/sys/class/graphics/fb%u/vsync_event"
#define HWC_VSYNC_PATH_MAX         64
#define HWC_VSYNC_THREAD_MAX       32

struct hwc_vsync_ctl_t {
    hwc_context_t* ctx;
    int disp_id;
};

static pthread_t vsync_threads[HWC_NUM_PHYSICAL_DISPLAY_TYPES];
static hwc_vsync_ctl_t hwc_vsync_ctl_arr[HWC_NUM_PHYSICAL_DISPLAY_TYPES];

int hwc_vsync_control(hwc_context_t* ctx, int dpy, int enable)
{
    int ret = 0;
    if(!ctx->vstate.fakevsync &&
       ioctl(ctx->dpyAttr[dpy].fd, MSMFB_OVERLAY_VSYNC_CTRL,
             &enable) < 0) {
        ALOGE("%s: vsync control failed. Dpy=%d, enable=%d : %s",
              __FUNCTION__, dpy, enable, strerror(errno));
        ret = -errno;
    }
    return ret;
}

static void *vsync_loop(void *param)
{
    const int MAX_DATA = 64;
    static char vdata[MAX_DATA];
    char property[PROPERTY_VALUE_MAX];
    char thread_name[HWC_VSYNC_THREAD_MAX];
    char vsync_fbtimestamp[HWC_VSYNC_PATH_MAX];

    uint64_t cur_timestamp=0;
    ssize_t len = -1;
    int fd_timestamp = -1;
    int ret = 0;
    bool fb1_vsync = false;
    bool logvsync = false;

    hwc_vsync_ctl_t *v_ctl = reinterpret_cast<hwc_vsync_ctl_t *>(param);
    if (!v_ctl) {
         ALOGE ("ERROR:%s:v_ctl not valid! %s",  __FUNCTION__, strerror(errno));
         return NULL;
    }

    int dpy = v_ctl->disp_id;
    hwc_context_t *ctx = v_ctl->ctx;
    if (!ctx) {
         ALOGE ("ERROR:%s:ctx not valid! %s",  __FUNCTION__, strerror(errno));
         return NULL;
    }

    memset(thread_name, 0x00, HWC_VSYNC_THREAD_MAX);
    snprintf(thread_name, HWC_VSYNC_THREAD_MAX, HWC_VSYNC_THREAD_NAME, dpy);

    memset(vsync_fbtimestamp, 0x00, HWC_VSYNC_PATH_MAX);
    snprintf(vsync_fbtimestamp, HWC_VSYNC_PATH_MAX, HWC_VSYNC_SYS_NODE_PATH, dpy);

    prctl(PR_SET_NAME, (unsigned long) &thread_name, 0, 0, 0);
    setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY +
                android::PRIORITY_MORE_FAVORABLE);

    if(property_get("debug.hwc.fakevsync", property, NULL) > 0) {
        if(atoi(property) == 1)
            ctx->vstate.fakevsync = true;
    }

    if(property_get("debug.hwc.logvsync", property, 0) > 0) {
        if(atoi(property) == 1)
            logvsync = true;
    }

    /* Currently read vsync timestamp from drivers
       e.g. VSYNC=41800875994
       */
    fd_timestamp = open(vsync_fbtimestamp, O_RDONLY);
    if (fd_timestamp < 0) {
        // Make sure fb device is opened before starting this thread so this
        // never happens.
        ALOGE ("FATAL:%s:not able to open file:%s, %s",  __FUNCTION__,
               vsync_fbtimestamp, strerror(errno));
        ctx->vstate.fakevsync = true;
    }

    do {
        if (LIKELY(!ctx->vstate.fakevsync)) {
            len = pread(fd_timestamp, vdata, MAX_DATA, 0);
            if (len < 0) {
                // If the read was just interrupted - it is not a fatal error
                // In either case, just continue.
                if (errno != EAGAIN &&
                    errno != EINTR  &&
                    errno != EBUSY) {
                    ALOGE ("FATAL:%s:not able to read file:%s, %s",
                           __FUNCTION__,
                           vsync_fbtimestamp, strerror(errno));
                }
                continue;
            }
            // extract timestamp
            const char *str = vdata;
            if (!strncmp(str, "VSYNC=", strlen("VSYNC="))) {
                cur_timestamp = strtoull(str + strlen("VSYNC="), NULL, 0);
            }
        } else {
            usleep(16666);
            cur_timestamp = systemTime();
        }
        // send timestamp to HAL
        if(ctx->vstate.enable) {
            ALOGD_IF (logvsync, "%s: timestamp %llu sent to HWC for %s%d",
                      __FUNCTION__, cur_timestamp, "fb", dpy);
            ctx->proc->vsync(ctx->proc, dpy, cur_timestamp);
        }

    } while (true);
    if(fd_timestamp >= 0)
        close (fd_timestamp);

    return NULL;
}

void init_vsync_thread(hwc_context_t* ctx)
{
    int ret, i;
    ALOGI("Initializing VSYNC Thread");

    for (i = 0; i < HWC_NUM_PHYSICAL_DISPLAY_TYPES; i++) {
        hwc_vsync_ctl_arr[i].ctx = ctx;
        hwc_vsync_ctl_arr[i].disp_id = ctx->dpyAttr[i].fb_idx;
        ret = pthread_create(&vsync_threads[i], NULL, vsync_loop, (void*)&hwc_vsync_ctl_arr[i]);
        if (ret) {
            ALOGE("%s: failed to create %s %s", __FUNCTION__,
                  HWC_VSYNC_THREAD_NAME, i, strerror(ret));
        }
    }
}

}; //namespace
