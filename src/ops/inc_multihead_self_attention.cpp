/* Copyright 2023 CMU, Facebook, LANL, MIT, NVIDIA, and Stanford (alphabetical)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "flexflow/ops/inc_multihead_self_attention.h"
#include "flexflow/utils/hip_helper.h"
#include <hip/hip_runtime.h>

namespace FlexFlow {

// declare Legion names
using Legion::coord_t;
using Legion::Memory;

/*static*/
void IncMultiHeadSelfAttention::inference_kernel(
    IncMultiHeadSelfAttentionMeta const *m,
    float const *input_ptr,
    float const *weight_ptr,
    float *output_ptr,
    hipStream_t stream) {
  checkCUDNN(miopenSetStream(m->handle.dnn, stream));
  handle_unimplemented_hip_kernel(OP_INC_MULTIHEAD_SELF_ATTENTION);
}

/*static*/
void IncMultiHeadSelfAttention::inference_kernel_wrapper(
    IncMultiHeadSelfAttentionMeta const *m,
    float const *input_ptr,
    float const *weight_ptr,
    float *output_ptr) {
  hipStream_t stream;
  checkCUDA(get_legion_stream(&stream));

  hipEvent_t t_start, t_end;
  if (m->profiling) {
    hipEventCreate(&t_start);
    hipEventCreate(&t_end);
    hipEventRecord(t_start, stream);
  }
  IncMultiHeadSelfAttention::inference_kernel(
      m, input_ptr, weight_ptr, output_ptr, stream);
  if (m->profiling) {
    hipEventRecord(t_end, stream);
    checkCUDA(hipEventSynchronize(t_end));
    float elapsed = 0;
    checkCUDA(hipEventElapsedTime(&elapsed, t_start, t_end));
    hipEventDestroy(t_start);
    hipEventDestroy(t_end);
    printf("IncMultiHeadSelfAttention forward time = %.2fms\n", elapsed);
    // print_tensor<3, float>(acc_query.ptr, acc_query.rect,
    // "[Attention:forward:query]"); print_tensor<3, float>(acc_output.ptr,
    // acc_output.rect, "[Attention:forward:output]");
  }
}

IncMultiHeadSelfAttentionMeta::IncMultiHeadSelfAttentionMeta(
    FFHandler handler,
    IncMultiHeadSelfAttention const *attn,
    Memory gpu_mem,
    int num_samples,
    int num_heads)
    : OpMeta(handler, attn) {
  hipStream_t stream;
  checkCUDA(get_legion_stream(&stream));
  checkCUDNN(miopenSetStream(handler.dnn, stream));
#if 0
  checkCUDNN(cudnnCreateAttnDescriptor(&attnDesc));
  checkCUDNN(cudnnCreateSeqDataDescriptor(&qDesc));
  checkCUDNN(cudnnCreateSeqDataDescriptor(&kDesc));
  checkCUDNN(cudnnCreateSeqDataDescriptor(&vDesc));
  checkCUDNN(cudnnCreateSeqDataDescriptor(&oDesc));
  // Currently do not support adding bias to key/value projection
  assert(!attn->add_bias_kv);
  cudnnAttnQueryMap_t attnMode = CUDNN_ATTN_QUERYMAP_ALL_TO_ONE;
  // Assume no beam search for now
  int maxBeamSize = 1;
  // printf("batchSize(%d) qSize(%d) kSize(%d) vSize(%d) qProjSize(%d)
  // kProjSize(%d)\n",
  //     num_samples, attn->qSize, attn->kSize, attn->vSize, attn->qProjSize,
  //     attn->kProjSize);
  // printf("vProjSize(%d) oProjSize(%d) qoSeqLength(%d) kvSeqLength(%d)\n",
  //     attn->vProjSize, attn->oProjSize, attn->qoSeqLength,
  //     attn->kvSeqLength);
  cudnnMathType_t math_type;
  if (handle.allowTensorOpMathConversion) {
    math_type = CUDNN_TENSOR_OP_MATH_ALLOW_CONVERSION;
  } else {
    math_type = CUDNN_TENSOR_OP_MATH;
  }
  checkCUDNN(cudnnSetAttnDescriptor(attnDesc,
                                    attnMode,
                                    num_heads,
                                    1.0f /*smScalar*/,
                                    CUDNN_DATA_FLOAT,
                                    CUDNN_DATA_FLOAT,
                                    math_type,
                                    NULL /*attnDropoutDesc*/,
                                    NULL /*postDropoutDesc*/,
                                    attn->qSize,
                                    attn->kSize,
                                    attn->vSize,
                                    attn->qProjSize,
                                    attn->kProjSize,
                                    attn->vProjSize,
                                    attn->oProjSize,
                                    attn->qoSeqLength,
                                    attn->kvSeqLength,
                                    num_samples,
                                    maxBeamSize));
  size_t workSpaceSize;
  checkCUDNN(cudnnGetMultiHeadAttnBuffers(
      handler.dnn, attnDesc, &weightSize, &workSpaceSize, &reserveSpaceSize));
  assert(workSpaceSize <= handler.workSpaceSize);
  // printf("weightSize(%zu) workSpaceSize(%zu) reserveSpaceSize(%zu)\n",
  // weightSize, workSpaceSize, reserveSpaceSize);
  int dimA[CUDNN_SEQDATA_DIM_COUNT];
  cudnnSeqDataAxis_t axes[CUDNN_SEQDATA_DIM_COUNT];
  assert(CUDNN_SEQDATA_DIM_COUNT == 4);
  axes[3] = CUDNN_SEQDATA_VECT_DIM; // 3 = nbDims-1
  axes[2] = CUDNN_SEQDATA_BEAM_DIM;
  axes[1] = CUDNN_SEQDATA_TIME_DIM;
  axes[0] = CUDNN_SEQDATA_BATCH_DIM;
  int *qoSeqArray = (int *)malloc(sizeof(int) * num_samples);
  int *kvSeqArray = (int *)malloc(sizeof(int) * num_samples);
  for (int i = 0; i < num_samples; i++) {
    qoSeqArray[i] = attn->qoSeqLength;
    kvSeqArray[i] = attn->kvSeqLength;
  }
  // Set qDesc
  {
    dimA[CUDNN_SEQDATA_BEAM_DIM] = 1;
    dimA[CUDNN_SEQDATA_BATCH_DIM] = num_samples;
    dimA[CUDNN_SEQDATA_TIME_DIM] = attn->qoSeqLength;
    dimA[CUDNN_SEQDATA_VECT_DIM] = attn->qSize;
    checkCUDNN(cudnnSetSeqDataDescriptor(qDesc,
                                         CUDNN_DATA_FLOAT,
                                         CUDNN_SEQDATA_DIM_COUNT,
                                         dimA,
                                         axes,
                                         num_samples,
                                         qoSeqArray,
                                         NULL));
  }
  // Set kDesc
  {
    dimA[CUDNN_SEQDATA_BEAM_DIM] = 1;
    dimA[CUDNN_SEQDATA_BATCH_DIM] = num_samples;
    dimA[CUDNN_SEQDATA_TIME_DIM] = attn->kvSeqLength;
    dimA[CUDNN_SEQDATA_VECT_DIM] = attn->kSize;
    checkCUDNN(cudnnSetSeqDataDescriptor(kDesc,
                                         CUDNN_DATA_FLOAT,
                                         CUDNN_SEQDATA_DIM_COUNT,
                                         dimA,
                                         axes,
                                         num_samples,
                                         kvSeqArray,
                                         NULL));
  }
  // Set vDesc
  {
    dimA[CUDNN_SEQDATA_BEAM_DIM] = 1;
    dimA[CUDNN_SEQDATA_BATCH_DIM] = num_samples;
    dimA[CUDNN_SEQDATA_TIME_DIM] = attn->kvSeqLength;
    dimA[CUDNN_SEQDATA_VECT_DIM] = attn->vSize;
    checkCUDNN(cudnnSetSeqDataDescriptor(vDesc,
                                         CUDNN_DATA_FLOAT,
                                         CUDNN_SEQDATA_DIM_COUNT,
                                         dimA,
                                         axes,
                                         num_samples,
                                         kvSeqArray,
                                         NULL));
  }
  // Set oDesc
  {
    dimA[CUDNN_SEQDATA_BEAM_DIM] = 1;
    dimA[CUDNN_SEQDATA_BATCH_DIM] = num_samples;
    dimA[CUDNN_SEQDATA_TIME_DIM] = attn->qoSeqLength;
    dimA[CUDNN_SEQDATA_VECT_DIM] = attn->oProjSize;
    checkCUDNN(cudnnSetSeqDataDescriptor(oDesc,
                                         CUDNN_DATA_FLOAT,
                                         CUDNN_SEQDATA_DIM_COUNT,
                                         dimA,
                                         axes,
                                         num_samples,
                                         qoSeqArray,
                                         NULL));
  }
  // allocate memory for the seqArray and reserve space
  {
    size_t totalSize = reserveSpaceSize + sizeof(int) * num_samples * 2;
    Realm::Rect<1, coord_t> bounds(Realm::Point<1, coord_t>(0),
                                   Realm::Point<1, coord_t>(totalSize - 1));
    std::vector<size_t> field_sizes;
    field_sizes.push_back(sizeof(char));
    Realm::RegionInstance::create_instance(reserveInst,
                                           gpu_mem,
                                           bounds,
                                           field_sizes,
                                           0,
                                           Realm::ProfilingRequestSet())
        .wait();
    devQoSeqArray = (int *)reserveInst.pointer_untyped(0, sizeof(char));
    checkCUDA(cudaMemcpy(devQoSeqArray,
                         qoSeqArray,
                         sizeof(int) * num_samples,
                         cudaMemcpyHostToDevice));
    devKvSeqArray = (int *)devQoSeqArray + num_samples;
    checkCUDA(cudaMemcpy(devKvSeqArray,
                         kvSeqArray,
                         sizeof(int) * num_samples,
                         cudaMemcpyHostToDevice));
    reserveSpace = (int *)devKvSeqArray + num_samples;
  }
  // allocate memory for loWinIdx/hiWinIdx
  loWinIdx = (int *)malloc(sizeof(int) * attn->qoSeqLength);
  hiWinIdx = (int *)malloc(sizeof(int) * attn->qoSeqLength);
  for (int i = 0; i < attn->qoSeqLength; i++) {
    loWinIdx[i] = 0;
    hiWinIdx[i] = attn->kvSeqLength;
  }
  free(qoSeqArray);
  free(kvSeqArray);
#endif
}

IncMultiHeadSelfAttentionMeta::~IncMultiHeadSelfAttentionMeta(void) {
#if 0
  reserveInst.destroy();
  free(loWinIdx);
  free(hiWinIdx);
  checkCUDNN(cudnnDestroyAttnDescriptor(attnDesc));
  checkCUDNN(cudnnDestroySeqDataDescriptor(qDesc));
  checkCUDNN(cudnnDestroySeqDataDescriptor(kDesc));
  checkCUDNN(cudnnDestroySeqDataDescriptor(vDesc));
  checkCUDNN(cudnnDestroySeqDataDescriptor(oDesc));
#endif
}

}; // namespace FlexFlow