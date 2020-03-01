#pragma once
#include <Eigen/Sparse>
#include <SparseDNN/utility/matrix_operation.hpp>
#include <SparseDNN/utility/matrix_format.h>
#include <cusparse_v2.h>
#include <algorithm>
#include<type_traits>

#define gpu_err_check(ans) { gpu_assert((ans), __FILE__, __LINE__); }

inline void gpu_assert(cudaError_t code, const char *file, int line, bool abort=true)
{
   if (code != cudaSuccess) 
   {
      fprintf(stderr,"GPUassert: %s %s %d\n", cudaGetErrorString(code), file, line);
      if (abort) exit(code);
   }
}
namespace sparse_dnn{


template<typename T>
__global__
void CSR_mutiply_CSC(
  const size_t* y_n_rows,
  const size_t* y_row_array,
  const size_t* y_col_array,
  const T* y_data_array,
  const size_t* w_n_cols,
  const size_t* w_col_array,
  const size_t* w_row_array,
  const T* w_data_array,
  size_t* result_row_array,
  size_t* result_col_array,
  T* result_data_array,
  const T* bias
);

template<typename T>
__global__
void check_nnz(
  const size_t* y_n_rows,
  const size_t* y_row_array,
  const size_t* y_col_array,
  const size_t* y_data_array,
  const size_t* w_n_cols,
  const size_t* w_col_array,
  const size_t* w_row_array,
  const size_t* w_data_array,
  size_t* result_row_array,
  const T* bias
);

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
__global__
void add_bias(T* arr, int nnz, T bias);

template <typename T>
void resize_CPU(CSRMatrix<T>& target, int rows);

template <typename T>
void add_bias_relu_CPU(T* arr, T bias, int rows);


//-----------------------------------------------------------------------------
//Definition of task function
//-----------------------------------------------------------------------------


template<typename T>
__global__
void CSR_mutiply_CSC(
  const size_t* y_n_rows,
  const size_t* y_row_array,
  const size_t* y_col_array,
  const T* y_data_array,
  const size_t* w_n_cols,
  const size_t* w_col_array,
  const size_t* w_row_array,
  const T* w_data_array,
  size_t* result_row_array,
  size_t* result_col_array,
  T* result_data_array,
  const T* bias
) {

  size_t row = blockIdx.x * blockDim.x + threadIdx.x;
  if(row < *y_n_rows){
    const size_t row_start = y_row_array[row];
    const size_t row_end = y_row_array[row + 1];
    T sum = 0;
    size_t current_col;
    size_t nnz_count = result_row_array[row];
    for(size_t w_col = 0; w_col < *w_n_cols; ++w_col){
      current_col = w_col_array[w_col];
      for(size_t y = row_start; y < row_end; ++y){
        for(size_t w = current_col; w < w_col_array[w_col + 1]; ++w){
          if(y_col_array[y] > w_row_array[w]){
            continue;
          }
          else if(y_col_array[y] < w_row_array[w]){
            break;
          }
          else{
            sum += y_data_array[y] * w_data_array[w];
            current_col = w;
            break;
          }
        }
      }
      if(sum == 0){
        continue;
      }
      sum += *bias;
      if(sum <= 0){
        sum = 0;
        continue;
      }
      if(sum > 32){
        sum = 32;
      }
      result_data_array[nnz_count] = sum;
      result_col_array[nnz_count++] = w_col;
      sum = 0;
    }
  }
}

void
cusparse_mutiplication(
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
__global__
void add_bias(T* arr, int nnz, T bias){
  int index = blockIdx.x * blockDim.x + threadIdx.x;
  if(index < nnz){
    arr[index] += bias;
  }
} 

template <typename T>
void resize_CPU(CSRMatrix<T>& target, int rows) {

  int nnz = target.row_array[rows - 1];
  int reduce_arr[rows];
  for(int j = 0; j < rows; ++j){ reduce_arr[j] = 0;}

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

  std::partial_sum(reduce_arr, reduce_arr + rows, reduce_arr);
  for(int k = 0; k < rows; ++k){
    target.row_array[k] -= reduce_arr[k];
  }

  std::remove(target.data_array, target.data_array + nnz, 0);
  std::remove(target.col_array, target.col_array + nnz, -1);
}

}// end of namespace sparse_dnn ----------------------------------------------
