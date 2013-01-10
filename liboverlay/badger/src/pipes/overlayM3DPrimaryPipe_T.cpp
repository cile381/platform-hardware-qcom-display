/*
 * Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name of The Linux Foundation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
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

#include "overlayUtils.h"

namespace overlay2 {
   template <int CHAN>
   inline M3DPrimaryPipe<CHAN>::M3DPrimaryPipe() : mM3Dfmt(0) {}
   template <int CHAN>
   inline M3DPrimaryPipe<CHAN>::~M3DPrimaryPipe() { close(); }
   template <int CHAN>
   inline bool M3DPrimaryPipe<CHAN>::open(RotatorBase* rot) {
      LOGE_IF(DEBUG_OVERLAY, "M3DPrimaryPipe open");
      if(!mM3d.open(rot)) {
         LOGE("3Dpipe failed to open");
         return false;
      }
      return true;
   }
   template <int CHAN>
   inline bool M3DPrimaryPipe<CHAN>::close() {
      return mM3d.close();
   }
   template <int CHAN>
   inline bool M3DPrimaryPipe<CHAN>::commit() { return mM3d.commit(); }
   template <int CHAN>
   inline void M3DPrimaryPipe<CHAN>::setId(int id) { mM3d.setId(id); }
   template <int CHAN>
   inline void M3DPrimaryPipe<CHAN>::setMemoryId(int id) { mM3d.setMemoryId(id); }
   template <int CHAN>
   inline bool M3DPrimaryPipe<CHAN>::queueBuffer(uint32_t offset) {
      return mM3d.queueBuffer(offset); }
   template <int CHAN>
   inline bool M3DPrimaryPipe<CHAN>::dequeueBuffer(void*& buf) {
      return mM3d.dequeueBuffer(buf); }
   template <int CHAN>
   inline bool M3DPrimaryPipe<CHAN>::waitForVsync() {
      return mM3d.waitForVsync(); }
   template <int CHAN>
   inline bool M3DPrimaryPipe<CHAN>::setCrop(const utils::Dim& d) {
      utils::Dim _dim;
      if(!utils::getCropS3D<CHAN>(d, _dim, mM3Dfmt)){
         LOGE("M3DPrimaryPipe setCrop failed to getCropS3D");
         _dim = d;
      }
      return mM3d.setCrop(_dim);
   }
   template <int CHAN>
   inline bool M3DPrimaryPipe<CHAN>::start(const utils::PipeArgs& args) {
      if(!mM3d.start(args)) {
         LOGE("M3DPrimaryPipe start failed");
         return false;
      }
      return true;
   }
   template <int CHAN>
   inline bool M3DPrimaryPipe<CHAN>::setPosition(const utils::Dim& d) {
      return mM3d.setPosition(d);
   }
   template <int CHAN>
   inline bool M3DPrimaryPipe<CHAN>::setParameter(const utils::Params& param) {
      return mM3d.setParameter(param);
   }
   template <int CHAN>
   inline bool M3DPrimaryPipe<CHAN>::setSource(const utils::PipeArgs& args)
   {
      // extract 3D fmt
      mM3Dfmt = utils::format3DInput(utils::getS3DFormat(args.whf.format)) |
         utils::HAL_3D_OUT_MONOS_MASK;
      if (mM3d.isClosed()) {
         if (!this->start(args)) {
            LOGE("M3DPrimaryPipe setSource failed to start");
            return false;
         }
      }
      return mM3d.setSource(args);
   }
   template <int CHAN>
   inline const utils::PipeArgs& M3DPrimaryPipe<CHAN>::getArgs() const {
      return mM3d.getArgs();
   }
   template <int CHAN>
   inline utils::eOverlayPipeType M3DPrimaryPipe<CHAN>::getOvPipeType() const {
      return utils::OV_PIPE_TYPE_M3D_PRIMARY;
   }
   template <int CHAN>
   inline void M3DPrimaryPipe<CHAN>::dump() const {
      LOGE("M3DPrimaryPipe Pipe fmt=%d", mM3Dfmt);
      mM3d.dump();
   }

}// overlay2
