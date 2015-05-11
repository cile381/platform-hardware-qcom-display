/*
* Copyright (c) 2014, The Linux Foundation. All rights reserved.
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

#ifndef __HWC_DISPLAY_VIRTUAL_H__
#define __HWC_DISPLAY_VIRTUAL_H__

#include <qdMetaData.h>
#include <gralloc_priv.h>
#include "hwc_display.h"

namespace sde {

class HWCDisplayVirtual : public HWCDisplay {
 public:
  explicit HWCDisplayVirtual(CoreInterface *core_intf, hwc_procs_t const **hwc_procs);
  virtual int Init();
  virtual int Deinit();
  virtual int Prepare(hwc_display_contents_1_t *content_list);
  virtual int Commit(hwc_display_contents_1_t *content_list);
  virtual int SetActiveConfig(hwc_display_contents_1_t *content_list);
  virtual void SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type);

 private:
  int SetOutputBuffer(hwc_display_contents_1_t *content_list);
  void DumpOutputBuffer(hwc_display_contents_1_t *content_list);
  int GetWidth(const private_handle_t* handle);
  int GetHeight(const private_handle_t* handle);

  bool dump_output_layer_;
};

}  // namespace sde

#endif  // __HWC_DISPLAY_VIRTUAL_H__

