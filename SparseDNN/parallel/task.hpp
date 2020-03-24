#pragma once
#include <Eigen/SparseCore>
#include <SparseDNN/utility/matrix_operation.hpp>
#include <SparseDNN/utility/matrix_format.h>
#include <SparseDNN/utility/cuda_error.hpp>
#include <cusparse_v2.h>
#include <algorithm>
#include <thrust/scan.h>

namespace sparse_dnn{

struct HostNerowArgs{
  int num_inputs;
  int update_layer;
  int** rlenY;
  int** rowsY;
  int* nerowsY;
};

inline
void cusparse_mutiplication(
  const CSRMatrix<float>& a,
  const CSRMatrix<float>& b,
  int a_row,
  int a_col,
  int b_col,
  int nnz_a,
  int nnz_b,
  CSRMatrix<float>& c
);

inline
void cusparse_mutiplication(
  const CSRMatrix<double>& a,
  const CSRMatrix<double>& b,
  int a_row,
  int a_col,
  int b_col,
  int nnz_a,
  int nnz_b,
  CSRMatrix<double>& c
);

template <typename T>
void resize_CPU(CSRMatrix<T>& target, int rows);

template <typename T>
void add_bias_relu_CPU(T* arr, T bias, int rows);

template <typename T>
__global__ 
void baseline_inference(
  const T* Y0,
  const int* rowsY0,
  int* rlenY0,
  const int COL_BLK,
  const int N_SLAB,
  const int num_neurons_per_layer,
  const int* roffW,
  const int* colsW,
  const T* valsW,
  const T bias,
  T* Y1,
  int* rlenY1
);

template <typename T>
__global__ 
void wo_host_inference(
  const T* Y0,
  const int* rowsY0,
  int* rlenY0,
  const int COL_BLK,
  const int N_SLAB,
  const int num_neurons_per_layer,
  const int* roffW,
  const int* colsW,
  const T* valsW,
  const T bias,
  T* Y1,
  int* rlenY1
);

template <typename T>
__global__ 
void wo_host_inference_test(
  const T* Y0,
  const bool* rowsY0,
  const int COL_BLK,
  const int N_SLAB,
  const int num_neurons_per_layer,
  const int* roffW,
  const int* colsW,
  const T* valsW,
  const T bias,
  bool* rowsY1,
  T* Y1
);

template<typename T>
__global__ 
void mem_reset(T* Y, bool* rowsY, int num_inputs, int num_neurons_per_layer);

//-----------------------------------------------------------------------------
//Definition of task function
//-----------------------------------------------------------------------------

inline
void cusparse_mutiplication(
  const CSRMatrix<float>& a,
  const CSRMatrix<float>& b,
  int a_row,
  int a_col,
  int b_col,
  int nnz_a,
  int nnz_b,
  CSRMatrix<float>& c
) {

  cusparseHandle_t handle;
  cusparseMatDescr_t descr_a, descr_b, descr_c;
  cusparseMatDescr_t descr_d = 0;
  cusparseCreate(&handle);
  cusparseCreateMatDescr(&descr_a);
  cusparseCreateMatDescr(&descr_b);
  cusparseCreateMatDescr(&descr_d);
  cusparseCreateMatDescr(&descr_c);
  cusparseSetMatType(descr_a, CUSPARSE_MATRIX_TYPE_GENERAL);
  cusparseSetMatIndexBase(descr_a, CUSPARSE_INDEX_BASE_ZERO);
  cusparseSetMatType(descr_b, CUSPARSE_MATRIX_TYPE_GENERAL);
  cusparseSetMatIndexBase(descr_b, CUSPARSE_INDEX_BASE_ZERO);
  cusparseSetMatType(descr_c, CUSPARSE_MATRIX_TYPE_GENERAL);
  cusparseSetMatIndexBase(descr_c, CUSPARSE_INDEX_BASE_ZERO);

  int base_c, nnz_c;
  csrgemm2Info_t info = NULL;
  size_t buffer_size;
  void *buffer = NULL;
  int *nnz = &nnz_c;
  float alpha = 1.0;
  cusparseSetPointerMode(handle, CUSPARSE_POINTER_MODE_HOST);
  cusparseCreateCsrgemm2Info(&info);
  cusparseScsrgemm2_bufferSizeExt(
    handle, a_row, a_col, b_col, &alpha,
    descr_a, nnz_a, a.row_array, a.col_array,
    descr_b, nnz_b, b.row_array, b.col_array,
    NULL,
    descr_d, 0, NULL, NULL,
    info,
    &buffer_size
  );
  cudaMalloc(&buffer, buffer_size);

  cusparseXcsrgemm2Nnz(
    handle, a_row, a_col, b_col,
    descr_a, nnz_a, a.row_array, a.col_array,
    descr_b, nnz_b, b.row_array, b.col_array,
    descr_d, 0, NULL, NULL,
    descr_c, c.row_array, nnz,
    info, 
    buffer
  );

  if (NULL != nnz){
      nnz_c = *nnz;
  }else{
      cudaMemcpy(&nnz_c, c.row_array + a_row, sizeof(int), cudaMemcpyDeviceToHost);
      cudaMemcpy(&base_c, c.row_array, sizeof(int), cudaMemcpyDeviceToHost);
      nnz_c -= base_c;
  }

  cudaMalloc(&c.col_array, sizeof(int) * nnz_c);
  cudaMalloc(&c.data_array, sizeof(float) * nnz_c);
  cusparseScsrgemm2(
    handle, a_row, a_col, b_col, &alpha,
    descr_a, nnz_a, a.data_array, a.row_array, a.col_array,
    descr_b, nnz_b, b.data_array, b.row_array, b.col_array,
    NULL,
    descr_d, 0, NULL, NULL, NULL,
    descr_c, c.data_array, c.row_array, c.col_array,
    info, 
    buffer
  );

  cudaDeviceSynchronize();

  cusparseDestroyCsrgemm2Info(info);
  cudaFree(buffer);

}

inline
void cusparse_mutiplication(
  const CSRMatrix<double>& a,
  const CSRMatrix<double>& b,
  int a_row,
  int a_col,
  int b_col,
  int nnz_a,
  int nnz_b,
  CSRMatrix<double>& c
) {

  cusparseHandle_t handle;
  cusparseMatDescr_t descr_a, descr_b, descr_c;
  cusparseMatDescr_t descr_d = 0;
  cusparseCreate(&handle);
  cusparseCreateMatDescr(&descr_a);
  cusparseCreateMatDescr(&descr_b);
  cusparseCreateMatDescr(&descr_d);
  cusparseCreateMatDescr(&descr_c);
  cusparseSetMatType(descr_a, CUSPARSE_MATRIX_TYPE_GENERAL);
  cusparseSetMatIndexBase(descr_a, CUSPARSE_INDEX_BASE_ZERO);
  cusparseSetMatType(descr_b, CUSPARSE_MATRIX_TYPE_GENERAL);
  cusparseSetMatIndexBase(descr_b, CUSPARSE_INDEX_BASE_ZERO);
  cusparseSetMatType(descr_c, CUSPARSE_MATRIX_TYPE_GENERAL);
  cusparseSetMatIndexBase(descr_c, CUSPARSE_INDEX_BASE_ZERO);

  int base_c, nnz_c;
  csrgemm2Info_t info = NULL;
  size_t buffer_size;
  void *buffer = NULL;
  int *nnz = &nnz_c;
  double alpha = 1.0;
  cusparseSetPointerMode(handle, CUSPARSE_POINTER_MODE_HOST);
  cusparseCreateCsrgemm2Info(&info);
  cusparseDcsrgemm2_bufferSizeExt(
    handle, a_row, a_col, b_col, &alpha,
    descr_a, nnz_a, a.row_array, a.col_array,
    descr_b, nnz_b, b.row_array, b.col_array,
    NULL,
    descr_d, 0, NULL, NULL,
    info,
    &buffer_size
  );
  cudaMalloc(&buffer, buffer_size);

  cusparseXcsrgemm2Nnz(
    handle, a_row, a_col, b_col,
    descr_a, nnz_a, a.row_array, a.col_array,
    descr_b, nnz_b, b.row_array, b.col_array,
    descr_d, 0, NULL, NULL,
    descr_c, c.row_array, nnz,
    info, 
    buffer
  );

  if (NULL != nnz){
      nnz_c = *nnz;
  }else{
      cudaMemcpy(&nnz_c, c.row_array + a_row, sizeof(int), cudaMemcpyDeviceToHost);
      cudaMemcpy(&base_c, c.row_array, sizeof(int), cudaMemcpyDeviceToHost);
      nnz_c -= base_c;
  }

  cudaMalloc(&c.col_array, sizeof(int) * nnz_c);
  cudaMalloc(&c.data_array, sizeof(double) * nnz_c);
  cusparseDcsrgemm2(
    handle, a_row, a_col, b_col, &alpha,
    descr_a, nnz_a, a.data_array, a.row_array, a.col_array,
    descr_b, nnz_b, b.data_array, b.row_array, b.col_array,
    NULL,
    descr_d, 0, NULL, NULL, NULL,
    descr_c, c.data_array, c.row_array, c.col_array,
    info, 
    buffer
  );

  cudaDeviceSynchronize();

  cusparseDestroyCsrgemm2Info(info);
  cudaFree(buffer);

}

template <typename T>
void add_bias_relu_CPU(T* arr, T bias, int rows){

  for(int k = 0; k < rows; ++k){
    arr[k] += bias;
    if(arr[k] < 0){
      arr[k] = 0;
    }
    else if(arr[k] > 32){
      arr[k] = 32;
    }
  }
}

template <typename T>
void resize_CPU(CSRMatrix<T>& target, int rows) {

  int nnz = target.row_array[rows - 1];
  int reduce_arr[rows];
  std::memset(reduce_arr, 0, sizeof(int) * rows);

  for(int i = 0; i < nnz; ++i){
    if(target.data_array[i] == 0){

      auto it = std::lower_bound(
        target.row_array, 
        target.row_array + rows,
        i + 1
      );
      ++reduce_arr[it - target.row_array]; 

      target.col_array[i] = -1;
    }
  }

  thrust::inclusive_scan(reduce_arr, reduce_arr + rows, reduce_arr);
  for(int k = 0; k < rows; ++k){
    target.row_array[k] -= reduce_arr[k];
  }

  std::remove(target.data_array, target.data_array + nnz, 0);
  std::remove(target.col_array, target.col_array + nnz, -1);
}

template <typename T>
__global__ 
void baseline_inference(
  const T* Y0,
  const int nerowsY,
  const int* rowsY0,
  int* rlenY0,
  const int COL_BLK,
  const int N_SLAB,
  const int num_neurons_per_layer,
  const int* roffW,
  const int* colsW,
  const T* valsW,
  const T bias,
  T* Y1,
  int* rlenY1
) {

  if(blockIdx.x >= nerowsY){
    return;
  }

  extern  __shared__ T shRow[];

  int tid = threadIdx.y * blockDim.x + threadIdx.x;
  int rid = rowsY0[blockIdx.x];
  __syncthreads();
  if(tid == 0){
    rlenY0[rid] = 0;
    rlenY1[rid] = 0;
  }
  for(int i = 0; i < N_SLAB; i++){
    __syncthreads();
    for(int j = threadIdx.x; j < COL_BLK; j++){
      shRow[j] = 0;  
    }
    __syncthreads();
    for(int j = threadIdx.y; j < num_neurons_per_layer; j += blockDim.y){
      T valY = Y0[rid * num_neurons_per_layer + j];
      if(valY == 0){
        continue;
      }
      int begOffW = roffW[i * num_neurons_per_layer + j] + threadIdx.x;
      int endOffW = roffW[i * num_neurons_per_layer + j + 1];
      for(int k = begOffW; k < endOffW; k += blockDim.x){
        int colW = colsW[k];
        T valW = valsW[k];
        atomicAdd(&shRow[colW - i * COL_BLK], valY * valW);
      }
    }
    __syncthreads();
    int count = 0;
    for(int j = 0; j < COL_BLK; j += blockDim.x * blockDim.y){
      T v = j + tid < COL_BLK ? shRow[j + tid] + bias : -1;
      count += __syncthreads_count(v > 0);
      if(j + tid < COL_BLK){
        Y1[rid * num_neurons_per_layer + i * COL_BLK + j + tid] = min(T(32), max(T(0), v));
      }
    }
    if(tid == 0){
      rlenY1[rid] += count;
    }
  }

}

template <typename T>
__global__ 
void wo_host_inference(
  const T* Y0,
  const int* rowsY0,
  int* rlenY0,
  const int COL_BLK,
  const int N_SLAB,
  const int num_neurons_per_layer,
  const int* roffW,
  const int* colsW,
  const T* valsW,
  const T bias,
  T* Y1,
  int* rlenY1
) {

  if(rlenY0[blockIdx.x] == 0){
    return;
  }

  extern  __shared__ T shRow[];

  int tid = threadIdx.y * blockDim.x + threadIdx.x;
  __syncthreads();
  if(tid == 0){
    rlenY0[blockIdx.x] = 0;
    rlenY1[blockIdx.x] = 0;
  }
  for(int i = 0; i < N_SLAB; i++){
    __syncthreads();
    for(int j = tid; j < COL_BLK; j += blockDim.x * blockDim.y){
      shRow[j] = 0;  
    }
    __syncthreads();
    for(int j = threadIdx.y; j < num_neurons_per_layer; j += blockDim.y){
      T valY = Y0[blockIdx.x * num_neurons_per_layer + j];
      if(valY == 0){
        continue;
      }
      int begOffW = roffW[i * num_neurons_per_layer + j] + threadIdx.x;
      int endOffW = roffW[i * num_neurons_per_layer + j + 1];
      for(int k = begOffW; k < endOffW; k += blockDim.x){
        int colW = colsW[k];
        T valW = valsW[k];
        atomicAdd(&shRow[colW - i * COL_BLK], valY * valW);
      }
    }
    __syncthreads();
    int count = 0;
    for(int j = 0; j < COL_BLK; j += blockDim.x * blockDim.y){
      T v = j + tid < COL_BLK ? shRow[j + tid] + bias : -1;
      count += __syncthreads_count(v > 0);
      if(j + tid < COL_BLK){
        Y1[blockIdx.x * num_neurons_per_layer + i * COL_BLK + j + tid] = min(T(32), max(T(0), v));
      }
    }
    if(tid == 0){
      rlenY1[blockIdx.x] += count;
    }
  }

}

template <typename T>
__global__ 
void wo_host_inference_test(
  const T* Y0,
  const bool* rowsY0,
  const int COL_BLK,
  const int N_SLAB,
  const int num_neurons_per_layer,
  const int* roffW,
  const int* colsW,
  const T* valsW,
  const T bias,
  bool* rowsY1,
  T* Y1
) {

  if(rowsY0[blockIdx.x] == false){
    return;
  }

  extern  __shared__ T shRow[];
//issue check if over size
  __shared__ bool is_nerow[2];
  is_nerow[1] = false;

  int tid = threadIdx.y * blockDim.x + threadIdx.x;
  __syncthreads();
  for(int i = 0; i < N_SLAB; i++){
    __syncthreads();
    for(int j = tid; j < COL_BLK; j += blockDim.x * blockDim.y){
      shRow[j] = 0;  
    }
    __syncthreads();
    for(int j = threadIdx.y; j < num_neurons_per_layer; j += blockDim.y){
      T valY = Y0[blockIdx.x * num_neurons_per_layer + j];
      if(valY == 0){
        continue;
      }
      int begOffW = roffW[i * num_neurons_per_layer + j] + threadIdx.x;
      int endOffW = roffW[i * num_neurons_per_layer + j + 1];
      for(int k = begOffW; k < endOffW; k += blockDim.x){
        int colW = colsW[k];
        T valW = valsW[k];
        atomicAdd(&shRow[colW - i * COL_BLK], valY * valW);
      }
    }
    __syncthreads();
    for(int j = 0; j < COL_BLK; j += blockDim.x * blockDim.y){
      T v = j + tid < COL_BLK ? shRow[j + tid] + bias : -1;
      if(j + tid < COL_BLK){
        Y1[blockIdx.x * num_neurons_per_layer + i * COL_BLK + j + tid] = min(T(32), max(T(0), v));
        is_nerow[v > 0] = true;
      }
    }
  }
//only syc here
  __syncthreads();
  if(tid == 0){
    rowsY1[blockIdx.x] = is_nerow[1];
  }

}

template<typename T>
__global__ 
void mem_reset(T* Y, bool* rowsY, int num_inputs, int num_neurons_per_layer) {

  int tid = threadIdx.x;
  int rid = blockIdx.x;
  
  if(tid == 0){
    rowsY[rid] = false;
  }

  for(int i = rid; i < num_inputs; i += gridDim.x){
    for(int j = tid; j < num_neurons_per_layer; j += blockDim.x){
      Y[i * num_neurons_per_layer + j] = 0;
    } 
  }
}

}// end of namespace sparse_dnn ----------------------------------------------
