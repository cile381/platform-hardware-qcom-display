/*
* Copyright (c) 2014 - 2015, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <utils/constants.h>
#include <utils/debug.h>

#include "offline_ctrl.h"
#include "hw_rotator_interface.h"
#include <core/buffer_sync_handler.h>
#include "hw_interface.h"

#define __CLASS__ "OfflineCtrl"

namespace sde {

// TODO(user): Move this offline controller under composition manager like other modules
// [resource manager]. Implement session management and buffer management in offline controller.
OfflineCtrl::OfflineCtrl() : hw_rotator_intf_(NULL), hw_rotator_present_(false) {
}

DisplayError OfflineCtrl::Init(BufferSyncHandler *buffer_sync_handler) {
  DisplayError error = kErrorNone;

  error = HWRotatorInterface::Create(&hw_rotator_intf_, buffer_sync_handler);
  if (error != kErrorNone) {
    if (hw_rotator_intf_) {
      HWRotatorInterface::Destroy(hw_rotator_intf_);
    }
    return error;
  }

  error = hw_rotator_intf_->Open();
  if (error != kErrorNone) {
    DLOGW("Failed to open rotator device");
  } else {
    hw_rotator_present_ = true;
  }

  return kErrorNone;
}

DisplayError OfflineCtrl::Deinit() {
  DisplayError error = kErrorNone;

  error = hw_rotator_intf_->Close();
  if (error != kErrorNone) {
    DLOGW("Failed to close rotator device");
    return error;
  }
  hw_rotator_present_ = false;
  HWRotatorInterface::Destroy(hw_rotator_intf_);

  return kErrorNone;
}

DisplayError OfflineCtrl::RegisterDisplay(DisplayType type, Handle *display_ctx) {
  DisplayOfflineContext *disp_offline_ctx = new DisplayOfflineContext();
  if (disp_offline_ctx == NULL) {
    return kErrorMemory;
  }

  disp_offline_ctx->display_type = type;
  *display_ctx = disp_offline_ctx;

  return kErrorNone;
}

void OfflineCtrl::UnregisterDisplay(Handle display_ctx) {
  DisplayOfflineContext *disp_offline_ctx = reinterpret_cast<DisplayOfflineContext *>(display_ctx);

  delete disp_offline_ctx;
  disp_offline_ctx = NULL;
}


DisplayError OfflineCtrl::Prepare(Handle display_ctx, HWLayers *hw_layers) {
  DisplayError error = kErrorNone;

  DisplayOfflineContext *disp_offline_ctx = reinterpret_cast<DisplayOfflineContext *>(display_ctx);

  disp_offline_ctx->pending_rot_commit = false;

  if (!hw_rotator_present_ && IsRotationRequired(hw_layers)) {
    DLOGV_IF(kTagOfflineCtrl, "No Rotator device found");
    return kErrorHardware;
  }

  error = CloseRotatorSession(hw_layers);
  if (error != kErrorNone) {
    DLOGE("Close rotator session failed for display %d", disp_offline_ctx->display_type);
    return error;
  }


  if (IsRotationRequired(hw_layers)) {
    error = OpenRotatorSession(hw_layers);
    if (error != kErrorNone) {
      DLOGE("Open rotator session failed for display %d", disp_offline_ctx->display_type);
      return error;
    }

    error = hw_rotator_intf_->Validate(hw_layers);
    if (error != kErrorNone) {
      DLOGE("Rotator validation failed for display %d", disp_offline_ctx->display_type);
      return error;
    }
    disp_offline_ctx->pending_rot_commit = true;
  }

  return kErrorNone;
}

DisplayError OfflineCtrl::Commit(Handle display_ctx, HWLayers *hw_layers) {
  DisplayError error = kErrorNone;

  DisplayOfflineContext *disp_offline_ctx = reinterpret_cast<DisplayOfflineContext *>(display_ctx);

  if (disp_offline_ctx->pending_rot_commit) {
    error = hw_rotator_intf_->Commit(hw_layers);
    if (error != kErrorNone) {
      DLOGE("Rotator commit failed for display %d", disp_offline_ctx->display_type);
      return error;
    }
    disp_offline_ctx->pending_rot_commit = false;
  }

  return kErrorNone;
}

DisplayError OfflineCtrl::OpenRotatorSession(HWLayers *hw_layers) {
  HWLayersInfo &hw_layer_info = hw_layers->info;
  DisplayError error = kErrorNone;

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    Layer& layer = hw_layer_info.stack->layers[hw_layer_info.index[i]];
    bool rot90 = (layer.transform.rotation == 90.0f);

    for (uint32_t count = 0; count < 2; count++) {
      HWRotateInfo *rotate_info = &hw_layers->config[i].rotates[count];
      HWBufferInfo *rot_buf_info = &rotate_info->hw_buffer_info;

      if (!rotate_info->valid || rot_buf_info->session_id >= 0) {
        continue;
      }

      rotate_info->input_buffer = layer.input_buffer;
      rotate_info->frame_rate = layer.frame_rate;

      error = hw_rotator_intf_->OpenSession(rotate_info);
      if (error != kErrorNone) {
        return error;
      }
    }
  }

  return kErrorNone;
}

DisplayError OfflineCtrl::CloseRotatorSession(HWLayers *hw_layers) {
  DisplayError error = kErrorNone;
  uint32_t i = 0;

  while (hw_layers->closed_session_ids[i] >= 0) {
    error = hw_rotator_intf_->CloseSession(hw_layers->closed_session_ids[i]);
    if (error != kErrorNone) {
      return error;
    }
    hw_layers->closed_session_ids[i++] = -1;
  }

  return kErrorNone;
}

bool OfflineCtrl::IsRotationRequired(HWLayers *hw_layers) {
  HWLayersInfo &layer_info = hw_layers->info;

  for (uint32_t i = 0; i < layer_info.count; i++) {
    Layer& layer = layer_info.stack->layers[layer_info.index[i]];

    HWRotateInfo *rotate = &hw_layers->config[i].rotates[0];
    if (rotate->valid) {
      return true;
    }

    rotate = &hw_layers->config[i].rotates[1];
    if (rotate->valid) {
      return true;
    }
  }
  return false;
}

}  // namespace sde

