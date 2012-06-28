/*
 * Copyright (C) 2012 The Android Open Source Project
 *
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

#ifndef HWC_BYPASS
#define HWC_BYPASS
#define MAX_STATIC_PIPES 3
#define BYPASS_DEBUG 0
#define BYPASS_INDEX_OFFSET 4
#define DEFAULT_IDLE_TIME 2000

#define MAX_VG 2
#define MAX_RGB 2
#define VAR_INDEX 3
#define MAX_PIPES (MAX_VG + MAX_RGB)



struct hwc_context_t;
void getLayerResolution(const hwc_layer_t* layer, int& width, int& height);
void calculate_scaled_destination(private_hwc_module_t* hwcModule, int &left,
                                  int &top, int &width, int& height);
// pipe status
enum {
    PIPE_UNASSIGNED = 0,
    PIPE_IN_FB_MODE,
    PIPE_IN_BYP_MODE,
};

// pipe request
enum {
    PIPE_NONE = 0,
    PIPE_REQ_VG,
    PIPE_REQ_RGB,
    PIPE_REQ_FB,
};

enum {
    BYPASS_SUCCESS = 0,
    BYPASS_FAILURE,
    BYPASS_ABORT,
};

//This class manages the status of 4 MDP pipes and keeps
//track of Variable pipe mode.

class PipeMgr {
public:
    PipeMgr() {
        reset();
    }
    void reset() {
        mVGPipes = MAX_VG;
        mVGUsed = 0;
        mVGIndex = 0;
        mRGBPipes = MAX_RGB;
        mRGBUsed = 0;
        mRGBIndex = MAX_VG;
        mTotalAvail = mVGPipes + mRGBPipes;
        memset(&mStatus, 0x0 , sizeof(int)*mTotalAvail);
    }

    //Based on the preference received, pipe mgr
    //allocates the best available pipe to handle
    //the case
    int req_for_pipe(int pipe_req) {

        switch(pipe_req) {
            case PIPE_REQ_VG: //VG
                if(mVGPipes){
                    mVGPipes--;
                    mVGUsed++;
                    mTotalAvail--;
                    return PIPE_REQ_VG;
                }
            case PIPE_REQ_RGB: // RGB
                if(mRGBPipes) {
                    mRGBPipes--;
                    mRGBUsed++;
                    mTotalAvail--;
                    return PIPE_REQ_RGB;
                }
                return PIPE_NONE;
            case PIPE_REQ_FB: //FB
                if(mRGBPipes) {
                   mRGBPipes--;
                   mRGBUsed++;
                   mTotalAvail--;
                   mStatus[VAR_INDEX] = PIPE_IN_FB_MODE;
                   return PIPE_REQ_FB;
               }
            default:
                break;
        };
        return PIPE_NONE;
    }

    //Allocate requested pipe and update availablity
    int assign_pipe(int pipe_pref) {

        switch(pipe_pref) {
            case PIPE_REQ_VG: //VG
                if(mVGUsed) {
                    mVGUsed--;
                    mStatus[mVGIndex] = PIPE_IN_BYP_MODE;
                    return mVGIndex++;
                }
            case PIPE_REQ_RGB: //RGB
                if(mRGBUsed) {
                    mRGBUsed--;
                    mStatus[mRGBIndex] = PIPE_IN_BYP_MODE;
                    return mRGBIndex++;
                }
            default:
                LOGE("%s: PipeMgr:invalid case in pipe_mgr_assign", __FUNCTION__);
                return -1;
        };
    }

    // Get/Set pipe status
    void setStatus(int pipe_index, int pipe_status) {
        mStatus[pipe_index] = pipe_status;
    }
    int getStatus(int pipe_index) {
        return mStatus[pipe_index];
    }
private:
    int mVGPipes;
    int mVGUsed;
    int mVGIndex;
    int mRGBPipes;
    int mRGBUsed;
    int mRGBIndex;
    int mTotalAvail;
    int mStatus[MAX_PIPES];
};

class Bypass {
public:
    enum State {
        BYPASS_ON = 0,
        BYPASS_OFF,
        BYPASS_OFF_PENDING,
    };

    enum {
        BYPASS_LAYER_BLEND = 1,
        BYPASS_LAYER_DOWNSCALE = 2,
        BYPASS_LAYER_SKIP = 4,
        BYPASS_LAYER_UNSUPPORTED_MEM = 8,
    };

    struct mdp_pipe_info {
        int index;
        int z_order;
        bool isVG;
        bool isFG;
        bool vsync_wait;
    };

    struct pipe_layer_pair {
        int layer_index;
        mdp_pipe_info pipe_index;
        native_handle_t* handle;
    };

    struct current_frame_info {
        int count;
        struct pipe_layer_pair* pipe_layer;

    };

    struct bypass_layer_info {
        bool can_bypass;
        int pipe_pref;
    };

    /* Handler to invoke frame redraw on Idle Timer expiry */
    static void timeout_handler(void *udata);

    /* get/set pipe index associated with bypassing layers */
    static void setLayerIndex(hwc_layer_t* layer, const int bypass_index);
    static int  getLayerIndex(hwc_layer_t* layer);

    /* set/reset bypass flags for bypassing layers */
    static void setBypassLayerFlags(hwc_layer_list_t* list);
    static void unsetBypassLayerFlags(hwc_context_t* ctx, hwc_layer_list_t* list);

    static void print_info(hwc_layer_t* layer);
    static void calculate_crop_rects(hwc_rect_t& crop, hwc_rect_t& dst, int hw_w, int hw_h);

    /* configure's bypass for the frame */
    static int  prepare(hwc_context_t *ctx, hwc_layer_t *layer, mdp_pipe_info& mdp_info);

    /* checks for conditions where bypass is not possible */
    static bool is_doable(hwc_composer_device_t *dev, const int yuvCount, const hwc_layer_list_t* list);

    /*sets up bypass for the current frame */
    static bool configure(hwc_composer_device_t *ctx,  hwc_layer_list_t* list, int YUVCount);
    static bool setup(hwc_context_t* ctx, hwc_layer_list_t* list);

    /* parses layer for properties affecting bypass */
    static void get_layer_info(hwc_layer_t* layer, int& flags);

    /* iterates through layer list to choose candidate for bypass */
    static int  mark_layers_for_bypass(hwc_layer_list_t* list, bypass_layer_info* layer_info,
                                                        current_frame_info& current_frame);
    static bool parse_and_allocate(hwc_context_t* ctx, hwc_layer_list_t* list,
                                                   current_frame_info& current_frame );

    /* clears layer info struct */
    static void reset_bypass_layer_info(bypass_layer_info* bypass_layer_info, int count);

    /* allocates pipes to selected candidates */
    static bool alloc_layer_pipes(hwc_layer_list_t* list, bypass_layer_info* layer_info,
                                                        current_frame_info& current_frame);
    /* updates variable pipe mode for the current frame */
    static int  configure_var_pipe(hwc_context_t* ctx);

    /* cleans up MDP pipes */
    static void close_pipes(hwc_context_t* ctx, bool close_variable_pipe = false,
                                                            bool rest_vsync_for_pipes  = true);
    /* closes unused MDP pipes for the current frame */
    static void closeExtraPipes(bool reset_vsync = true);

    /* draw */
    static int drawLayerUsingBypass(hwc_context_t *ctx, hwc_layer_t *layer, int layer_index);

    /* cleans up previous frame */
    static void unlock_prev_frame_buffers();
    static void update_previous_frame_info();

    /* configure/tear-down bypass params */
    static bool initialize(hwc_context_t *ctx);
    static bool deinit();

    /* get/set bypass state */
    static State get_state() { return sBypassState; };
    static void set_state(State state) { sBypassState = state; };

private:
    static overlay::OverlayUI* sOvUI[MAX_BYPASS_LAYERS];
    static State sBypassState;
    static IdleInvalidator *idleInvalidator;
    static struct current_frame_info sCurrentFrame;
    static struct current_frame_info sPreviousFrame;
    static PipeMgr sPipeMgr;
};

overlay::OverlayUI* Bypass:: sOvUI[MAX_BYPASS_LAYERS];
Bypass::State Bypass::sBypassState;
IdleInvalidator *Bypass::idleInvalidator;
struct Bypass::current_frame_info Bypass::sCurrentFrame;
struct Bypass::current_frame_info Bypass::sPreviousFrame;
PipeMgr Bypass::sPipeMgr;

void Bypass::close_pipes(hwc_context_t* ctx, bool close_variable_pipe,
                        bool rest_vsync_for_pipes) {
#ifdef COMPOSITION_BYPASS
        private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>
                                                          (ctx->device.common.module);
        framebuffer_device_t*fbDev = hwcModule->fbDevice;

        sPipeMgr.reset();
        closeExtraPipes(rest_vsync_for_pipes);

        if(close_variable_pipe) {
            int mode = VAR_PIPE_CLOSE;
            if( fbDev->perform(fbDev,EVENT_SET_VAR_PIPE_MODE, (void*)&mode) < 0){
                LOGE("%s: settign var pipe mode failed!!", __FUNCTION__);
            }
        }
        unlock_prev_frame_buffers();
#endif
}

bool Bypass::deinit() {
#ifdef COMPOSITION_BYPASS
    for(int i = 0; i < MAX_BYPASS_LAYERS - 1; i++) {
           delete sOvUI[i];
    }
    unlock_prev_frame_buffers();
#endif
    return true;
}

void Bypass::timeout_handler(void *udata) {
    struct hwc_context_t* ctx = (struct hwc_context_t*)(udata);

    if(!ctx) {
        LOGE("%s: received empty data in timer callback", __FUNCTION__);
        return;
    }

    hwc_procs* proc = (hwc_procs*)ctx->device.reserved_proc[0];

    if(!proc) {
        LOGE("%s: HWC proc not registered", __FUNCTION__);
        return;
    }
    ctx->forceComposition = true;
    /* Trigger SF to redraw the current frame */
    proc->invalidate(proc);
}

void Bypass::setLayerIndex(hwc_layer_t* layer, const int bypass_index)
{
    layer->flags &= ~HWC_BYPASS_INDEX_MASK;
    layer->flags |= bypass_index << BYPASS_INDEX_OFFSET;
}

int Bypass::getLayerIndex(hwc_layer_t* layer)
{
    int byp_index = -1;

    if(layer->flags & HWC_COMP_BYPASS) {
        byp_index = ((layer->flags & HWC_BYPASS_INDEX_MASK) >> BYPASS_INDEX_OFFSET);
        byp_index = (byp_index < MAX_BYPASS_LAYERS ? byp_index : -1 );
    }
    return byp_index;
}

void Bypass::update_previous_frame_info() {
    if(sCurrentFrame.count) {
        sPreviousFrame.count = sCurrentFrame.count;
        sPreviousFrame.pipe_layer = sCurrentFrame.pipe_layer;

        for(int i = 0; i < sPreviousFrame.count; i++ ) {
            private_handle_t* hnd = (private_handle_t*)
                               sPreviousFrame.pipe_layer[i].handle;
            hnd->flags |= private_handle_t::PRIV_FLAGS_HWC_LOCK;
        }

        sCurrentFrame.count = 0;
        sCurrentFrame.pipe_layer = NULL;
    }
}

void Bypass::unlock_prev_frame_buffers() {
#ifdef COMPOSITION_BYPASS

    // Unlock the previous frames bypass buffers. We can blindly
    // unlock the buffers here, because buffers will be stored in sPreviousFrame
    // if the lock was successfully acquired.

    for(int i = 0; (i < sPreviousFrame.count); i++) {
       // NULL Check
       if(sPreviousFrame.pipe_layer[i].handle == NULL) {
           LOGE("%s: bypass buffer handle is NULL", __FUNCTION__);
           continue;
       }
       private_handle_t *hnd = (private_handle_t*) sPreviousFrame.pipe_layer[i].handle;

       // Validate the handle to make sure it hasn't been deallocated.
       if (private_handle_t::validate(hnd)) {
           LOGE("%s: bypass buffer handle is Invalid.", __FUNCTION__);
           continue;
       }

	   // Check if the handle was locked previously
       if (!(private_handle_t::PRIV_FLAGS_HWC_LOCK & hnd->flags)) {
           LOGE("%s: bypass buffer handle is not locked by HWC", __FUNCTION__);
           continue;
       }

       //Unlock prev frame buffer
       if (GENLOCK_FAILURE == genlock_unlock_buffer(hnd)) {
           LOGE("%s: genlock_unlock_buffer failed", __FUNCTION__);
       } else {
           // Reset the lock flag
           hnd->flags &= ~private_handle_t::PRIV_FLAGS_HWC_LOCK;
           sPreviousFrame.pipe_layer[i].handle = NULL;
       }
    }

	// clean up previous frame data
    sPreviousFrame.count = 0;
    free(sPreviousFrame.pipe_layer);
    sPreviousFrame.pipe_layer = NULL;

	// update prev frame with current frame data
    update_previous_frame_info();
#endif
}

void Bypass::print_info(hwc_layer_t* layer)
{
     hwc_rect_t sourceCrop = layer->sourceCrop;
     hwc_rect_t displayFrame = layer->displayFrame;

     int s_l = sourceCrop.left;
     int s_t = sourceCrop.top;
     int s_r = sourceCrop.right;
     int s_b = sourceCrop.bottom;

     int d_l = displayFrame.left;
     int d_t = displayFrame.top;
     int d_r = displayFrame.right;
     int d_b = displayFrame.bottom;

     LOGE_IF(BYPASS_DEBUG, "src:[%d,%d,%d,%d] (%d x %d) dst:[%d,%d,%d,%d] (%d x %d)",
                             s_l, s_t, s_r, s_b, (s_r - s_l), (s_b - s_t),
                             d_l, d_t, d_r, d_b, (d_r - d_l), (d_b - d_t));
}

//Crops source buffer against destination and FB boundaries
void Bypass::calculate_crop_rects(hwc_rect_t& crop, hwc_rect_t& dst, int hw_w, int hw_h) {

    int& crop_x = crop.left;
    int& crop_y = crop.top;
    int& crop_r = crop.right;
    int& crop_b = crop.bottom;
    int crop_w = crop.right - crop.left;
    int crop_h = crop.bottom - crop.top;

    int& dst_x = dst.left;
    int& dst_y = dst.top;
    int& dst_r = dst.right;
    int& dst_b = dst.bottom;
    int dst_w = dst.right - dst.left;
    int dst_h = dst.bottom - dst.top;

    if(dst_x < 0) {
        float scale_x =  crop_w * 1.0f / dst_w;
        float diff_factor = (scale_x * abs(dst_x));
        crop_x = crop_x + (int)diff_factor;
        crop_w = crop_r - crop_x;

        dst_x = 0;
        dst_w = dst_r - dst_x;;
    }
    if(dst_r > hw_w) {
        float scale_x = crop_w * 1.0f / dst_w;
        float diff_factor = scale_x * (dst_r - hw_w);
        crop_r = crop_r - diff_factor;
        crop_w = crop_r - crop_x;

        dst_r = hw_w;
        dst_w = dst_r - dst_x;
    }
    if(dst_y < 0) {
        float scale_y = crop_h * 1.0f / dst_h;
        float diff_factor = scale_y * abs(dst_y);
        crop_y = crop_y + diff_factor;
        crop_h = crop_b - crop_y;

        dst_y = 0;
        dst_h = dst_b - dst_y;
    }
    if(dst_b > hw_h) {
        float scale_y = crop_h * 1.0f / dst_h;
        float diff_factor = scale_y * (dst_b - hw_h);
        crop_b = crop_b - diff_factor;
        crop_h = crop_b - crop_y;

        dst_b = hw_h;
        dst_h = dst_b - dst_y;
    }

    LOGE_IF(BYPASS_DEBUG,"crop: [%d,%d,%d,%d] dst:[%d,%d,%d,%d]",
                     crop_x, crop_y, crop_w, crop_h,dst_x, dst_y, dst_w, dst_h);
}

/*
 * Configures pipe(s) for composition bypass
 */
int Bypass::prepare(hwc_context_t *ctx, hwc_layer_t *layer, mdp_pipe_info& mdp_info) {

    int nPipeIndex = mdp_info.index;

    if (ctx && sOvUI[nPipeIndex]) {
        overlay::OverlayUI *ovUI = sOvUI[nPipeIndex];

        private_hwc_module_t* hwcModule = reinterpret_cast<
                private_hwc_module_t*>(ctx->device.common.module);
        if (!hwcModule) {
            LOGE("%s: NULL Module", __FUNCTION__);
            return -1;
        }

        private_handle_t *hnd = (private_handle_t *)layer->handle;

        if(!hnd) {
            LOGE("%s: layer handle is NULL", __FUNCTION__);
            return -1;
        }


        int hw_w = hwcModule->fbDevice->width;
        int hw_h = hwcModule->fbDevice->height;


        hwc_rect_t sourceCrop = layer->sourceCrop;
        hwc_rect_t displayFrame = layer->displayFrame;

        const int src_w = sourceCrop.right - sourceCrop.left;
        const int src_h = sourceCrop.bottom - sourceCrop.top;

        hwc_rect_t crop = sourceCrop;
        int crop_w = crop.right - crop.left;
        int crop_h = crop.bottom - crop.top;

        hwc_rect_t dst = displayFrame;
        int dst_w = dst.right - dst.left;
        int dst_h = dst.bottom - dst.top;

        //REDUNDANT ??
        if(hnd != NULL && (hnd->flags & private_handle_t::PRIV_FLAGS_NONCONTIGUOUS_MEM )) {
            LOGE("%s: Unable to setup bypass due to non-pmem memory",__FUNCTION__);
            return -1;
        }

        if(dst.left < 0 || dst.top < 0 || dst.right > hw_w || dst.bottom > hw_h) {
            LOGE_IF(BYPASS_DEBUG,"%s: Destination has negative coordinates", __FUNCTION__);

            calculate_crop_rects(crop, dst, hw_w, hw_h);

            //Update calulated width and height
            crop_w = crop.right - crop.left;
            crop_h = crop.bottom - crop.top;

            dst_w = dst.right - dst.left;
            dst_h = dst.bottom - dst.top;
        }

        if( (dst_w > hw_w)|| (dst_h > hw_h)) {
            LOGE_IF(BYPASS_DEBUG,"%s: Destination rectangle exceeds FB resolution", __FUNCTION__);
            print_info(layer);
            dst_w = hw_w;
            dst_h = hw_h;
        }

        overlay_buffer_info info;
        info.width = src_w;
        info.height = src_h;
        info.format = hnd->format;
        info.size = hnd->size;

        int fbnum = 0;
        int orientation = layer->transform;
        const bool useVGPipe =  mdp_info.isVG;
        //only last layer should wait for vsync
        const bool waitForVsync = mdp_info.vsync_wait;
        const bool isFg = mdp_info.isFG;
        //To differentiate zorders for different layers
        const int zorder = mdp_info.z_order;

        ovUI->setSource(info, orientation);
        ovUI->setCrop(crop.left, crop.top, crop_w, crop_h);
        ovUI->setDisplayParams(fbnum, waitForVsync, isFg, zorder, useVGPipe);
        calculate_scaled_destination(hwcModule, dst.left, dst.top, dst_w, dst_h);
        ovUI->setPosition(dst.left, dst.top, dst_w, dst_h);

        LOGE_IF(BYPASS_DEBUG,"%s: Bypass set: crop[%d,%d,%d,%d] dst[%d,%d,%d,%d] waitforVsync: %d \
                                isFg: %d zorder: %d VG = %d nPipe: %d",__FUNCTION__,
                                crop.left, crop.top, crop_w, crop_h,
                                dst.left, dst.top, dst_w, dst_h,
                                waitForVsync, isFg, zorder, useVGPipe, nPipeIndex );

        if(ovUI->commit() != overlay::NO_ERROR) {
            LOGE("%s: Overlay Commit failed", __FUNCTION__);
            return -1;
        }
    }
    return 0;
}

/*
 * Bypass is possible when
 * 1. External display is OFF
 * 2. No ASYNC layer
 * 3. No MDP pipe is used
 * 4. Rotation is not needed
 * 5. We have atmost MAX_BYPASS_LAYERS
 * 6. Composition is not triggered by
 *    Idle timer expiry
 */
bool Bypass::is_doable(hwc_composer_device_t *dev, const int yuvCount,
        const hwc_layer_list_t* list) {
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(dev->common.module);

    if(!ctx) {
        LOGE("%s: hwc context is NULL", __FUNCTION__);
        return false;
    }

    //Check if enabled in build.prop
    if(hwcModule->isBypassEnabled == false) {
        return false;
    }

    if(list->numHwLayers < 1) {
        return false;
    }

#if defined HDMI_DUAL_DISPLAY
    //Disable bypass when HDMI is enabled
    if(ctx->mHDMIEnabled || ctx->pendingHDMI) {
        return false;
    }
#endif

    if(ExtDispOnly::isModeOn()) {
        return false;
    }

    if(ctx->forceComposition) {
        return false;
    }

    //Bypass is not efficient if rotation or asynchronous mode is needed.

    for(int i = 0; i < list->numHwLayers; ++i) {
        if(list->hwLayers[i].transform) {
                return false;
        }
        if(list->hwLayers[i].flags & HWC_LAYER_ASYNCHRONOUS) {
            if (ctx->swapInterval > 0)
                return false;
        }
    }

    return (yuvCount == 0) && (ctx->hwcOverlayStatus == HWC_OVERLAY_CLOSED)
                                          && (list->numHwLayers <= MAX_BYPASS_LAYERS);
}

void Bypass::setBypassLayerFlags(hwc_layer_list_t* list) {

    for(int index = 0 ; index < sCurrentFrame.count; index++ )
    {
        int layer_index = sCurrentFrame.pipe_layer[index].layer_index;
        if(layer_index >= 0) {
            hwc_layer_t* layer = &(list->hwLayers[layer_index]);

            layer->flags |= HWC_COMP_BYPASS;
            layer->compositionType = HWC_USE_OVERLAY;
            layer->hints |= HWC_HINT_CLEAR_FB;
        }
    }

    if( list->numHwLayers > sCurrentFrame.count) {
         list->flags &= ~HWC_SKIP_COMPOSITION; //Compose to FB
    } else {
         list->flags |= HWC_SKIP_COMPOSITION; // Dont
    }
}

void Bypass::get_layer_info(hwc_layer_t* layer, int& flags) {

    private_handle_t* hnd = (private_handle_t*)layer->handle;

    if(layer->flags & HWC_SKIP_LAYER) {
        flags |= BYPASS_LAYER_SKIP;
    } else if(hnd != NULL &&
        (hnd->flags & private_handle_t::PRIV_FLAGS_NONCONTIGUOUS_MEM )) {
        flags |= BYPASS_LAYER_UNSUPPORTED_MEM;
    }

    if(layer->blending != HWC_BLENDING_NONE)
        flags |= BYPASS_LAYER_BLEND;

    int dst_w, dst_h;
    getLayerResolution(layer, dst_w, dst_h);

    hwc_rect_t sourceCrop = layer->sourceCrop;
    const int src_w = sourceCrop.right - sourceCrop.left;
    const int src_h = sourceCrop.bottom - sourceCrop.top;
    if(((src_w > dst_w) || (src_h > dst_h))) {
        flags |= BYPASS_LAYER_DOWNSCALE;
    }
}

int Bypass::mark_layers_for_bypass(hwc_layer_list_t* list, bypass_layer_info* layer_info,
                                                       current_frame_info& current_frame) {

    int layer_count = list->numHwLayers;

    if(layer_count > MAX_BYPASS_LAYERS) {
        if(!sPipeMgr.req_for_pipe(PIPE_REQ_FB)) {
            LOGE("%s: binding var pipe to FB failed!!", __FUNCTION__);
            return 0;
        }
    }

    //Parse layers from higher z-order
    for(int index = layer_count - 1 ; index >= 0; index-- ) {
        hwc_layer_t* layer = &list->hwLayers[index];

        int layer_prop = 0;
        get_layer_info(layer, layer_prop);

        LOGE_IF(BYPASS_DEBUG,"%s: prop for layer [%d]: %x", __FUNCTION__,
                                                             index, layer_prop);

        //Both in cases of NON-CONTIGUOUS memory or SKIP layer,
        //current version of bypass falls back completely to FB
        //composition.
        //TO DO: Support dual mode bypass

        if(layer_prop & BYPASS_LAYER_UNSUPPORTED_MEM) {
            LOGE("%s: Bypass not possible due to non contigous memory",__FUNCTION__);
            return BYPASS_ABORT;
        }

        if(layer_prop & BYPASS_LAYER_SKIP) {
            LOGE("%s: Bypass not possible due to skip",__FUNCTION__);
            return BYPASS_ABORT;
        }

        //Reques for MDP pipes
        int pipe_pref = PIPE_REQ_VG;

        if((layer_prop & BYPASS_LAYER_DOWNSCALE) &&
                        (layer_prop & BYPASS_LAYER_BLEND)) {
            pipe_pref = PIPE_REQ_RGB;
         }

        int allocated_pipe = sPipeMgr.req_for_pipe( pipe_pref);
        if(allocated_pipe) {
          layer_info[index].can_bypass = true;
          layer_info[index].pipe_pref = allocated_pipe;
          current_frame.count++;
        }else {
            LOGE("%s: pipe marking in mark layer fails for : %d",
                                          __FUNCTION__, allocated_pipe);
            return BYPASS_FAILURE;
        }
    }
    return BYPASS_SUCCESS;
}

void Bypass::reset_bypass_layer_info(bypass_layer_info* bypass_layer_info, int count) {
    for(int i = 0 ; i < count; i++ ) {
        bypass_layer_info[i].can_bypass = false;
        bypass_layer_info[i].pipe_pref = PIPE_NONE;
    }
}

bool Bypass::alloc_layer_pipes(hwc_layer_list_t* list,
                        bypass_layer_info* layer_info, current_frame_info& current_frame) {

    int layer_count = list->numHwLayers;
    int bypass_count = current_frame.count;
    int fallback_count = layer_count - bypass_count;
    int frame_pipe_count = 0;

    LOGE_IF(BYPASS_DEBUG, "%s:  dual mode: %d  total count: %d \
                                bypass count: %d fallback count: %d",
                            __FUNCTION__, (layer_count != bypass_count),
                            layer_count, bypass_count, fallback_count);

    for(int index = 0 ; index < layer_count ; index++ ) {
        hwc_layer_t* layer = &list->hwLayers[index];

        if(layer_info[index].can_bypass) {
             pipe_layer_pair& info  = current_frame.pipe_layer[frame_pipe_count];
             mdp_pipe_info& pipe_info = info.pipe_index;

             pipe_info.index = sPipeMgr.assign_pipe(layer_info[index].pipe_pref);
             pipe_info.isVG = (layer_info[index].pipe_pref == PIPE_REQ_VG);
             pipe_info.isFG = 0;//(frame_pipe_count == 0);
             /* if VAR pipe is attached to FB, FB will be updated with VSYNC WAIT flag, so
                no need to set VSYNC WAIT for any bypass pipes. if not, set VSYNC WAIT
                to the last updating pipe */
             pipe_info.vsync_wait = (sPipeMgr.getStatus(VAR_INDEX) == PIPE_IN_FB_MODE) ? false:
                                                           (frame_pipe_count == (bypass_count - 1));
             /* All the layers composed on FB will have MDP zorder 0, so start
                assigning from  1 for bypass layers */
                pipe_info.z_order = index - (fallback_count ? fallback_count - 1 : fallback_count);

             info.layer_index = index;
             frame_pipe_count++;
        }
    }
    return 1;
}

//returns array of layers and their allocated pipes
bool Bypass::parse_and_allocate(hwc_context_t* ctx, hwc_layer_list_t* list,
                                                  current_frame_info& current_frame ) {

    int layer_count = list->numHwLayers;

	/* clear pipe status */
    sPipeMgr.reset();

    bypass_layer_info* bp_layer_info = (bypass_layer_info*)
                                   malloc(sizeof(bypass_layer_info)* layer_count);

    reset_bypass_layer_info(bp_layer_info, layer_count);

    /* iterate through layer list to mark bypass candidate */
    if(mark_layers_for_bypass(list, bp_layer_info, current_frame) == BYPASS_ABORT) {
        free(bp_layer_info);
        current_frame.count = 0;
        LOGE("%s:mark_layers_for_bypass failed!!", __FUNCTION__);
        return false;
    }
    current_frame.pipe_layer = (pipe_layer_pair*)
                            malloc(sizeof(pipe_layer_pair) * current_frame.count);

    /* allocate MDP pipes for marked layers */
    alloc_layer_pipes( list, bp_layer_info, current_frame);

    free(bp_layer_info);
    return true;
}

int Bypass::configure_var_pipe(hwc_context_t* ctx) {

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           ctx->device.common.module);
    if(!hwcModule) {
       LOGE("%s: invalid params", __FUNCTION__);
       return -1;
    }

    framebuffer_device_t *fbDev = hwcModule->fbDevice;
    if (!fbDev) {
        LOGE("%s: fbDev is NULL", __FUNCTION__);
        return -1;
    }

    int new_mode = -1, cur_mode;
    fbDev->perform(fbDev,EVENT_GET_VAR_PIPE_MODE, (void*)&cur_mode);

    if(sPipeMgr.getStatus(VAR_INDEX) == PIPE_IN_FB_MODE) {
        new_mode = VAR_PIPE_FB_ATTACH;
    } else if(sPipeMgr.getStatus(VAR_INDEX) == PIPE_IN_BYP_MODE) {
        new_mode = VAR_PIPE_FB_DETACH;
        fbDev->perform(fbDev,EVENT_WAIT_POSTBUFFER,NULL);
    }

    LOGE_IF(BYPASS_DEBUG,"%s: old_mode: %d new_mode: %d", __FUNCTION__, cur_mode, new_mode);

    if((new_mode != cur_mode) && (new_mode >= 0)) {
       if(fbDev->perform(fbDev,EVENT_SET_VAR_PIPE_MODE,(void*)&new_mode) < 0) {
           LOGE("%s: Setting var pipe mode failed", __FUNCTION__);
       }
    }

    return 0;
}

bool Bypass::setup(hwc_context_t* ctx, hwc_layer_list_t* list) {
    int nPipeIndex, vsync_wait, isFG;
    int numHwLayers = list->numHwLayers;
    int nPipeAvailable = MAX_BYPASS_LAYERS;

    current_frame_info &current_frame = sCurrentFrame;
    current_frame.count = 0;

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           ctx->device.common.module);
    if(!hwcModule) {
       LOGE("%s: invalid params", __FUNCTION__);
       return -1;
    }

    framebuffer_device_t *fbDev = hwcModule->fbDevice;
    if (!fbDev) {
        LOGE("%s: fbDev is NULL", __FUNCTION__);
        return -1;
    }

    if(!parse_and_allocate(ctx, list, current_frame)) {
       int mode = VAR_PIPE_FB_ATTACH;
       if(fbDev->perform(fbDev,EVENT_SET_VAR_PIPE_MODE,(void*)&mode) < 0 ) {
           LOGE("%s: setting var pipe mode failed", __FUNCTION__);
       }
       LOGE_IF(BYPASS_DEBUG, "%s: Falling back to FB", __FUNCTION__);
       return false;
    }

    //update var pipe for the current frame
    configure_var_pipe(ctx);

    for (int index = 0 ; index < current_frame.count; index++) {
        int layer_index = current_frame.pipe_layer[index].layer_index;
        hwc_layer_t* layer = &list->hwLayers[layer_index];
        mdp_pipe_info& cur_pipe = current_frame.pipe_layer[index].pipe_index;

        if( prepare(ctx, layer, cur_pipe) != 0 ) {
           LOGE_IF(BYPASS_DEBUG, "%s: layer %d failed to configure bypass for pipe index: %d",
                                                               __FUNCTION__, index, cur_pipe.index);
           return false;
         } else {
            setLayerIndex(layer, index);
         }
    }
    return true;
}

void Bypass::unsetBypassLayerFlags(hwc_context_t* ctx, hwc_layer_list_t* list) {
    if (!list)
        return;

    for (int index = 0 ; index < sCurrentFrame.count; index++) {
        int l_index = sCurrentFrame.pipe_layer[index].layer_index;
        if(list->hwLayers[l_index].flags & HWC_COMP_BYPASS) {
            list->hwLayers[l_index].flags &= ~HWC_COMP_BYPASS;
        }
    }
}


void Bypass::closeExtraPipes(bool reset_vsync) {
#ifdef COMPOSITION_BYPASS
    for (int i = 0 ; i < MAX_STATIC_PIPES ; i++) {
        if (sPipeMgr.getStatus(i) == PIPE_UNASSIGNED)
            sOvUI[i]->closeChannel(reset_vsync);
    }
#endif
}

int Bypass::drawLayerUsingBypass(hwc_context_t *ctx, hwc_layer_t *layer, int layer_index) {
#ifdef COMPOSITION_BYPASS

    int data_index = getLayerIndex(layer);
    mdp_pipe_info& pipe_info = sCurrentFrame.pipe_layer[data_index].pipe_index;
    int index = pipe_info.index;

    if(index < 0) {
        LOGE("%s: Invalid bypass index (%d)", __FUNCTION__, index);
        return -1;
    }

    /* reset Invalidator */
    if(idleInvalidator)
     idleInvalidator->markForSleep();

    if (ctx && sOvUI[index]) {
        overlay::OverlayUI *ovUI = sOvUI[index];
        int ret = 0;

        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if(!hnd) {
            LOGE("%s handle null", __FUNCTION__);
            return -1;
        }

        if (GENLOCK_FAILURE == genlock_lock_buffer(hnd, GENLOCK_READ_LOCK,
                                                   GENLOCK_MAX_TIMEOUT)) {
            LOGE("%s: genlock_lock_buffer(READ) failed", __FUNCTION__);
            return -1;
        }

        LOGE_IF(BYPASS_DEBUG,"%s: Bypassing layer: %p hnd: %p  using pipe: %d",
                                                   __FUNCTION__, layer, hnd, index );

        ret = ovUI->queueBuffer(hnd);

        if (ret) {
            // Unlock the locked buffer
            if (ctx->swapInterval > 0) {
                if (GENLOCK_FAILURE == genlock_unlock_buffer(hnd)) {
                    LOGE("%s: genlock_unlock_buffer failed", __FUNCTION__);
                }
            }
            return -1;
        }
        hnd->flags |= private_handle_t::PRIV_FLAGS_HWC_LOCK;
        sCurrentFrame.pipe_layer[data_index].handle = hnd;
    }
     layer->flags &= ~HWC_COMP_BYPASS;
     layer->flags |= HWC_BYPASS_INDEX_MASK;
#endif
    return 0;
}

bool Bypass::initialize(hwc_context_t *dev) {
#ifdef COMPOSITION_BYPASS
    private_hwc_module_t* hwcModule = reinterpret_cast<
                private_hwc_module_t*>(dev->device.common.module);
    if(!hwcModule) {
        LOGE("%s: Invalid hwc module!!",__FUNCTION__);
        return false;
    }

    //Allocate OverlayUI objects for all bu VAR pipe
    for(int i = 0; i < MAX_STATIC_PIPES; i++) {
        sOvUI[i] = new overlay::OverlayUI();
    }

    if(MAX_BYPASS_LAYERS > MAX_STATIC_PIPES) {
        framebuffer_device_t *fbDev = hwcModule->fbDevice;
        if(fbDev == NULL) {
            LOGE("%s: FATAL: framebuffer device is NULL", __FUNCTION__);
            return false;
        }

        //Receive VAR pipe object from framebuffer
        OverlayUI* ov;
        if(fbDev->perform(fbDev,EVENT_GET_VAR_PIPE,(void*)&ov) < 0) {
            LOGE("%s: FATAL: getVariablePipe failed!!", __FUNCTION__);
            return false;
        }

        sOvUI[MAX_BYPASS_LAYERS-1] = (OverlayUI*)ov;

        sPipeMgr.setStatus(VAR_INDEX, PIPE_IN_FB_MODE);
    }
    sBypassState = BYPASS_OFF;

    char property[PROPERTY_VALUE_MAX];
    unsigned long idle_timeout = DEFAULT_IDLE_TIME;
    if(property_get("debug.bypass.idletime", property, NULL) > 0) {
        if(atoi(property) != 0)
           idle_timeout = atoi(property);
    }

    //create Idle Invalidator
    idleInvalidator = IdleInvalidator::getInstance();

    if(idleInvalidator == NULL) {
       LOGE("%s: failed to instantiate idleInvalidator  object", __FUNCTION__);
    } else {
       idleInvalidator->init(timeout_handler, dev, idle_timeout);
    }
#endif
    return true;
}

bool Bypass::configure(hwc_composer_device_t *dev,  hwc_layer_list_t* list, int YUVCount) {
#ifdef COMPOSITION_BYPASS
        hwc_context_t* ctx = (hwc_context_t*)(dev);

        bool isBypassUsed = true;
        bool doable = is_doable(dev, YUVCount, list);
        //Check if bypass is feasible
        if(doable) {
            if(setup(ctx, list)) {
                setBypassLayerFlags(list);
                sBypassState = BYPASS_ON;
            } else {
                LOGE_IF(BYPASS_DEBUG,"%s: Bypass setup Failed",__FUNCTION__);
                isBypassUsed = false;
            }
        } else {
            LOGE_IF( BYPASS_DEBUG,"%s: Bypass not possible[%d]",__FUNCTION__,
                       doable);
            isBypassUsed = false;
        }

        //Reset bypass states
        if(!isBypassUsed) {
            //Reset current frame
            sCurrentFrame.count = 0;
            free(sCurrentFrame.pipe_layer);
            sCurrentFrame.pipe_layer = NULL;

            //Reset MDP pipes
            sPipeMgr.reset();
            sPipeMgr.setStatus(VAR_INDEX, PIPE_IN_FB_MODE);
            configure_var_pipe(ctx);

            //Reset Bypass flags and state
            unsetBypassLayerFlags(ctx, list);
            if(sBypassState == BYPASS_ON) {
                sBypassState = BYPASS_OFF_PENDING;
            }
        }
        ctx->forceComposition = false;
#endif
        return true;
}

#endif
