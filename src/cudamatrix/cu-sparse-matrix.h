// cudamatrix/cu-sparse-matrix.h

// Copyright      2015  Johns Hopkins University (author: Daniel Povey)


// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.



#ifndef KALDI_CUDAMATRIX_CU_SPARSE_MATRIX_H_
#define KALDI_CUDAMATRIX_CU_SPARSE_MATRIX_H_

#include <sstream>

#include "cudamatrix/cu-matrixdim.h"
#include "cudamatrix/cu-common.h"
#include "cudamatrix/cu-value.h"
#include "matrix/matrix-common.h"
#include "matrix/kaldi-matrix.h"
#include "cudamatrix/cu-array.h"
#include "cudamatrix/cu-math.h"
#include "cudamatrix/cu-rand.h"

namespace kaldi {

template <class Real>
class CuSparseMatrix {
 public:

  /// Copy from CPU-based matrix.
  CuSparseMatrix<Real> &operator = (SparseMatrix<Real> &smat);

  /// Copy from possibly-GPU-based matrix.
  CuSparseMatrix<Real> &operator = (CuSparseMatrix<Real> &smat);  

  /// Swap with CPU-based matrix.  
  void Swap(SparseMatrix<Real> *smat);

  /// Swap with possibly-CPU-based matrix.    
  void Swap(CuSparseMatrix<Real> *smat);

  /// Sets up to a pseudo-randomly initialized matrix, with each element zero
  /// with probability zero_prob and else normally distributed- mostly for
  /// purposes of testing.
  void SetRandn(BaseFloat zero_prob);
  
  void Write(std::ostream &os, bool binary) const;

  void Read(std::istream &os, bool binary);
  
  ~CuSparseMatrix() { }

  // Use the CuMatrix::CopyFromSmat() function to copy from this to
  // CuMatrix.
  // Also see CuMatrix::AddSmat().
  
 private:
  // This member is only used if we did not compile for the GPU, or if the GPU
  // is not enabled.  It needs to be first because we reinterpret_cast this
  std::vector<SparseVector<Real> > cpu_rows_;

  // This is where the data lives if we are using a GPU.  Notice that the format
  // is a little different from on CPU, as there is only one list, of matrix
  // elements, instead of a list for each row.  This is better suited to
  // CUDA code.
  CuArray<MatrixElement<Real> > elements_;
};


template<typename Real>
Real TraceMatSmat(const CuMatrixBase<Real> &A,
                  const CuSparseMatrix<Real> &B,
                  MatrixTransposeType trans = kNoTrans);



}  // namespace

#endif