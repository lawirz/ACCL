/*******************************************************************************
#  Copyright (C) 2022 Xilinx, Inc
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#
#
*******************************************************************************/

#pragma once
#include "buffer.hpp"
#include "common.hpp"
#include "cProcess.hpp"
#include "cDefs.hpp"
#include "coyotedevice.hpp"
#include <cstdlib>
#include <cstring>
#include <iomanip>

/** @file coyotebuffer.hpp */

namespace ACCL {
  /**
   * A coyote buffer that is allocated and mapped to FPGA TLB
   *
   * The host pointer will be aligned to 2M bytes. 
   *
   * @tparam dtype Datatype of the buffer.
   */
template <typename dtype> class CoyoteBuffer : public Buffer<dtype> {
  public:

    /**
     * Construct a new CoyoteBuffer object without an existing host pointer.
     *
     * This constructor will allocate a buffer on both the host and the FPGA.
     *
     * @param length  Amount of elements to allocate the buffers for.
     * @param type    ACCL datatype of the buffer.
     * @param device  Device to allocate the FPGA buffer on.
     */
    CoyoteBuffer(addr_t length, dataType type, CCLO *device)
        : Buffer<dtype>(nullptr, length, type, 0x0)
    {
      CoyoteDevice *dev = dynamic_cast<CoyoteDevice *>(device);
      this->device = dev;
      // 2M pages
      size_t page_size = 1ULL << 21;
      this->buffer_size = length * sizeof(dtype);
      this->n_pages = (buffer_size + page_size - 1) / page_size;
      std::cerr << "CoyoteBuffer contructor called! page_size:"<<page_size<<", buffer_size:"<<buffer_size<<",n_pages:"<<n_pages<< std::endl;

      this->aligned_buffer = (dtype *)this->device->coyote_proc->getMem({fpga::CoyoteAlloc::HUGE_2M, n_pages});

      this->update_buffer(this->aligned_buffer, (addr_t)this->aligned_buffer); 

      std::cerr << "Allocation successful! Allocated buffer: "<<std::setbase(16)<<(uint64_t)this->aligned_buffer << std::setbase(10) <<", Size: " << this->_size << std::endl;

      host_flag = true;
      

      // if Coyote device has multiple qProc, map the allocated buffer with all qProc
      // if(this->device->num_qp > 0 && this->device->coyote_qProc_vec.size()!=0){
      //   for (unsigned int i=0; i<this->device->coyote_qProc_vec.size(); i++)
      //   {
      //     std::cerr << "Mapping coyote buffer to qProc cPid:"<<this->device->coyote_qProc_vec[i]->getCpid()<<", buffer_size:"<<buffer_size<<std::endl;
      //     this->device->coyote_qProc_vec[i]->userMap(this->aligned_buffer, buffer_size);
      //   }
      // }
    }

    /**
     * Destroy the CoyoteBuffer object
     *
     */
    virtual ~CoyoteBuffer()
    {}

    /**
     * Check if the buffer is simulated, always false.
     *
     */
    bool is_simulated() const override
    {
      return false;
    }

    /**
    * Check if buffer is host-only.
    *
    * @return true   The buffer is host-only.
    * @return false  The buffer is not host-only.
    */
    bool is_host_only() const override
    {
      return false;
    }

    /**
    * Check if buffer is currently in host memory.
    *
    * @return true   The buffer is in host memory.
    * @return false  The buffer is not in host memory.
    */
    bool is_host() const override
    {
      return this->host_flag;
    }

    /**
     * Sync the data from the device back to the host. 
     *
     */
    void sync_from_device() override
    {
      std::cerr << "calling sync: " << std::setbase(16) << (uint64_t)this->aligned_buffer << ", size: " << std::setbase(10) << this->size() << std::endl;

      this->device->coyote_proc->invoke({fpga::CoyoteOper::SYNC, this->aligned_buffer, (uint32_t)this->_size, true, true, 0, false});
    
      this->host_flag = true;
    }

    /**
     * Sync the data from the host to the device. 
     *
     */
    void sync_to_device() override
    {
      std::cerr << "calling offload: " << std::setbase(16) << (uint64_t)this->aligned_buffer << ", size: " << std::setbase(10) << this->size() << std::endl;

      this->device->coyote_proc->invoke({fpga::CoyoteOper::OFFLOAD, this->aligned_buffer, (uint32_t)this->_size, true, true, 0, false});
    
      this->host_flag = false;
    }


    void free_buffer() override
    {
      // if Coyote device has multiple qProc, unmap the allocated buffer with all qProc
      // if(this->device->num_qp > 0 && this->device->coyote_qProc_vec.size()!=0){
      //   for (unsigned int i=0; i<this->device->coyote_qProc_vec.size(); i++)
      //   {
      //     std::cerr << "Unmapping user buffer from qProc cPid:"<< std::setbase(10)<<this->device->coyote_qProc_vec[i]->getCpid()<<", buffer_size:"<<buffer_size<<","<<std::setbase(16) << (uint64_t)this->aligned_buffer<<std::endl;
      //     this->device->coyote_qProc_vec[i]->userUnmap(this->aligned_buffer);
      //   }
      // }

      std::cerr << "Free user buffer from cProc cPid:"<< std::setbase(10)<<this->device->coyote_proc->getCpid()<<", buffer_size:"<<buffer_size<<","<<std::setbase(16) << (uint64_t)this->aligned_buffer<<std::endl;
      this->device->coyote_proc->freeMem(this->aligned_buffer);
      return;
    }

    std::unique_ptr<BaseBuffer> slice(size_t start, size_t end) override
    {
      size_t start_bytes = start * sizeof(dtype);
      size_t end_bytes = end * sizeof(dtype);
      size_t length_bytes = end_bytes - start_bytes;

      
      

      auto slice_buf = std::unique_ptr<Buffer<dtype>>(new CoyoteBuffer<dtype>(
							  length_bytes, this->type(), this->device));
      std::memcpy((void *) this->buffer(), (void *) slice_buf->buffer(), length_bytes);
      std::cerr << "WARNING: Sliced Coyote-Buffers will not alias the"
	  "original buffers, use host-side data"
	  "and need unecessary copies. Should be avoided" 
		<< std::endl;

	  
      // Buffers should be on device per default to keep it consistent with XRT
      slice_buf->sync_to_device();						    
      return slice_buf;
    }

    size_t size() const { return this->buffer_size; }

    size_t page() const { return this->n_pages; }


  private:
    dtype *aligned_buffer;
    CoyoteDevice *device;
    bool host_flag; 
    size_t buffer_size;
    uint32_t n_pages;
  };
} // namespace ACCL
