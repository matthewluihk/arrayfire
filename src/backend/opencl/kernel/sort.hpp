/*******************************************************
 * Copyright (c) 2014, ArrayFire
 * All rights reserved.
 *
 * This file is distributed under 3-clause BSD license.
 * The complete license agreement can be obtained at:
 * http://arrayfire.com/licenses/BSD-3-Clause
 ********************************************************/

#pragma once
#include <program.hpp>
#include <traits.hpp>
#include <string>
#include <mutex>
#include <dispatch.hpp>
#include <Param.hpp>
#include <debug_opencl.hpp>
#include <kernel/sort_helper.hpp>
#include <kernel/iota.hpp>
#include <kernel/sort_by_key.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <boost/compute/core.hpp>
#include <boost/compute/algorithm/sort.hpp>
#include <boost/compute/functional/operator.hpp>
#include <boost/compute/iterator/buffer_iterator.hpp>

namespace compute = boost::compute;

using cl::Buffer;
using cl::Program;
using cl::Kernel;
using cl::make_kernel;
using cl::EnqueueArgs;
using cl::NDRange;
using std::string;

namespace opencl
{
    namespace kernel
    {
        template<typename T, bool isAscending>
        void sort0Iterative(Param val)
        {
            try {
                compute::command_queue c_queue(getQueue()());

                compute::buffer val_buf((*val.data)());

                for(int w = 0; w < val.info.dims[3]; w++) {
                    int valW = w * val.info.strides[3];
                    for(int z = 0; z < val.info.dims[2]; z++) {
                        int valWZ = valW + z * val.info.strides[2];
                        for(int y = 0; y < val.info.dims[1]; y++) {

                            int valOffset = valWZ + y * val.info.strides[1];

                            if(isAscending) {
                                compute::sort(
                                        compute::make_buffer_iterator< type_t<T> >(val_buf, valOffset),
                                        compute::make_buffer_iterator< type_t<T> >(val_buf, valOffset + val.info.dims[0]),
                                        compute::less< type_t<T> >(), c_queue);
                            } else {
                                compute::sort(
                                        compute::make_buffer_iterator< type_t<T> >(val_buf, valOffset),
                                        compute::make_buffer_iterator< type_t<T> >(val_buf, valOffset + val.info.dims[0]),
                                        compute::greater< type_t<T> >(), c_queue);
                            }
                        }
                    }
                }

                CL_DEBUG_FINISH(getQueue());
            } catch (cl::Error err) {
                CL_TO_AF_ERROR(err);
                throw;
            }
        }

        template<typename T, bool isAscending, int dim>
        void sortBatched(Param pVal)
        {
            try{
                af::dim4 inDims;
                for(int i = 0; i < 4; i++)
                    inDims[i] = pVal.info.dims[i];

                // Sort dimension
                // tileDims * seqDims = inDims
                af::dim4 tileDims(1);
                af::dim4 seqDims = inDims;
                tileDims[dim] = inDims[dim];
                seqDims[dim] = 1;

                // Create/call iota
                // Array<uint> key = iota<uint>(seqDims, tileDims);
                dim4 keydims = inDims;
                cl::Buffer* key = bufferAlloc(keydims.elements() * sizeof(uint));
                Param pKey;
                pKey.data = key;
                pKey.info.offset = 0;
                pKey.info.dims[0] = keydims[0];
                pKey.info.strides[0] = 1;
                for(int i = 1; i < 4; i++) {
                    pKey.info.dims[i] = keydims[i];
                    pKey.info.strides[i] = pKey.info.strides[i - 1] * pKey.info.dims[i - 1];
                }
                kernel::iota<uint>(pKey, seqDims, tileDims);

                // Flat
                //val.modDims(inDims.elements());
                //key.modDims(inDims.elements());
                pKey.info.dims[0] = inDims.elements();
                pKey.info.strides[0] = 1;
                pVal.info.dims[0] = inDims.elements();
                pVal.info.strides[0] = 1;
                for(int i = 1; i < 4; i++) {
                    pKey.info.dims[i] = 1;
                    pKey.info.strides[i] = pKey.info.strides[i - 1] * pKey.info.dims[i - 1];
                    pVal.info.dims[i] = 1;
                    pVal.info.strides[i] = pVal.info.strides[i - 1] * pVal.info.dims[i - 1];
                }

                // Sort indices
                // sort_by_key<T, uint, isAscending>(*resVal, *resKey, val, key, 0);
                kernel::sort0_by_key<T, uint, isAscending>(pVal, pKey);

                // Needs to be ascending (true) in order to maintain the indices properly
                kernel::sort0_by_key<uint, T, true>(pKey, pVal);

                // No need of doing moddims here because the original Array<T>
                // dimensions have not been changed
                //val.modDims(inDims);

                CL_DEBUG_FINISH(getQueue());
                bufferFree(key);
            } catch (cl::Error err) {
                CL_TO_AF_ERROR(err);
                throw;
            }
        }

        template<typename T, bool isAscending>
        void sort0(Param val)
        {
            int higherDims =  val.info.dims[1] * val.info.dims[2] * val.info.dims[3];
            // TODO Make a better heurisitic
            if(higherDims > 10)
                sortBatched<T, isAscending, 0>(val);
            else
                kernel::sort0Iterative<T, isAscending>(val);
        }
    }
}

#pragma GCC diagnostic pop
