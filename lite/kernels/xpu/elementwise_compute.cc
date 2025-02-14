// Copyright (c) 2019 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "lite/kernels/xpu/elementwise_compute.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "lite/backends/xpu/xpu_header_sitter.h"
#include "lite/core/op_lite.h"
#include "lite/core/op_registry.h"

namespace paddle {
namespace lite {
namespace kernels {
namespace xpu {

template <typename T>
struct AddFunctor {
  inline int operator()(xdnn::Context* ctx,
                        const T* x,
                        const T* y,
                        T* z,
                        const std::vector<int>& xshape,
                        const std::vector<int>& yshape) const {
    return xdnn::broadcast_add<T>(ctx, x, y, z, xshape, yshape);
  }
};

template <typename T>
struct SubFunctor {
  inline int operator()(xdnn::Context* ctx,
                        const T* x,
                        const T* y,
                        T* z,
                        const std::vector<int>& xshape,
                        const std::vector<int>& yshape) const {
    return xdnn::broadcast_sub<T>(ctx, x, y, z, xshape, yshape);
  }
};

template <typename T>
struct MulFunctor {
  inline int operator()(xdnn::Context* ctx,
                        const T* x,
                        const T* y,
                        T* z,
                        const std::vector<int>& xshape,
                        const std::vector<int>& yshape) const {
    return xdnn::broadcast_mul<T>(ctx, x, y, z, xshape, yshape);
  }
};

template <typename T>
struct DivFunctor {
  inline int operator()(xdnn::Context* ctx,
                        const T* x,
                        const T* y,
                        T* z,
                        const std::vector<int>& xshape,
                        const std::vector<int>& yshape) const {
    return xdnn::broadcast_div<T>(ctx, x, y, z, xshape, yshape);
  }
};

template <typename T>
struct MaxFunctor {
  inline int operator()(xdnn::Context* ctx,
                        const T* x,
                        const T* y,
                        T* z,
                        const std::vector<int>& xshape,
                        const std::vector<int>& yshape) const {
    return xdnn::broadcast_max<T>(ctx, x, y, z, xshape, yshape);
  }
};

template <typename T>
struct MinFunctor {
  inline int operator()(xdnn::Context* ctx,
                        const T* x,
                        const T* y,
                        T* z,
                        const std::vector<int>& xshape,
                        const std::vector<int>& yshape) const {
    return xdnn::broadcast_min<T>(ctx, x, y, z, xshape, yshape);
  }
};

template <typename T>
struct ModFunctor {
  inline int operator()(xdnn::Context* ctx,
                        const T* x,
                        const T* y,
                        T* z,
                        const std::vector<int>& xshape,
                        const std::vector<int>& yshape) const {
    return xdnn::broadcast_mod<T>(ctx, x, y, z, xshape, yshape);
  }
};

template <typename T>
struct FloordivFunctor {
  inline int operator()(xdnn::Context* ctx,
                        const T* x,
                        const T* y,
                        T* z,
                        const std::vector<int>& xshape,
                        const std::vector<int>& yshape) const {
    return xdnn::broadcast_floordiv<T>(ctx, x, y, z, xshape, yshape);
  }
};

template <typename T>
struct PowFunctor {
  inline int operator()(xdnn::Context* ctx,
                        const T* x,
                        const T* y,
                        T* z,
                        const std::vector<int>& xshape,
                        const std::vector<int>& yshape) const {
    return xdnn::broadcast_pow<T>(ctx, x, y, z, xshape, yshape);
  }
};

void set_shape(int axis,
               std::vector<int>* larger_shape,
               std::vector<int>* smaller_shape,
               const DDimLite& larger_dim,
               const DDimLite& smaller_dim) {
  const int axis_tmp =
      (axis == -1 ? static_cast<int>(larger_dim.size() - smaller_dim.size())
                  : axis);
  for (size_t i = 0; i < larger_dim.size(); i++) {
    (*larger_shape)[i] = static_cast<int>(larger_dim[i]);
  }
  for (size_t i = 0; i < smaller_dim.size(); ++i) {
    (*smaller_shape)[i + axis_tmp] = static_cast<int>(smaller_dim[i]);
    CHECK_EQ(((*larger_shape)[i + axis_tmp] == (*smaller_shape)[i + axis_tmp] ||
              (*larger_shape)[i + axis_tmp] == 1 ||
              (*smaller_shape)[i + axis_tmp] == 1),
             true);
  }
}

template <class T, class Functor, PrecisionType PType>
void ElementwiseCompute<T, Functor, PType>::Run() {
  auto& param = this->template Param<param_t>();
  auto& ctx = this->ctx_->template As<XPUContext>();
  const Tensor* x = param.X;
  const Tensor* y = param.Y;

  auto& x_dim = x->dims();
  auto& y_dim = y->dims();

  std::vector<int> x_shape(param.Out->dims().size(), 1);
  std::vector<int> y_shape(param.Out->dims().size(), 1);

  if (x_dim.size() == y_dim.size()) {
    for (size_t i = 0; i < x_dim.size(); i++) {
      x_shape[i] = static_cast<int>(x_dim[i]);
      y_shape[i] = static_cast<int>(y_dim[i]);
      CHECK_EQ((x_shape[i] == y_shape[i] || x_shape[i] == 1 || y_shape[i] == 1),
               true);
    }
  } else if (x_dim.size() > y_dim.size()) {
    set_shape(param.axis, &x_shape, &y_shape, x_dim, y_dim);
  } else {
    set_shape(param.axis, &y_shape, &x_shape, y_dim, x_dim);
  }

  Functor elt_func;
  int ret = elt_func(ctx.GetRawContext(),
                     x->template data<T>(),
                     y->template data<T>(),
                     param.Out->template mutable_data<T>(TARGET(kXPU)),
                     x_shape,
                     y_shape);

  CHECK_EQ(ret, 0);
  return;
}

}  // namespace xpu
}  // namespace kernels
}  // namespace lite
}  // namespace paddle

namespace xpu = paddle::lite::kernels::xpu;
using AddFloat32 =
    xpu::ElementwiseCompute<float, xpu::AddFunctor<float>, PRECISION(kFloat)>;
using AddFloat16 = xpu::ElementwiseCompute<float16,
                                           xpu::AddFunctor<float16>,
                                           PRECISION(kFP16)>;
using AddInt32 =
    xpu::ElementwiseCompute<int, xpu::AddFunctor<int>, PRECISION(kFloat)>;
using AddInt64 = xpu::ElementwiseCompute<int64_t,
                                         xpu::AddFunctor<int64_t>,
                                         PRECISION(kFloat)>;

using SubFloat32 =
    xpu::ElementwiseCompute<float, xpu::SubFunctor<float>, PRECISION(kFloat)>;
using SubFloat16 = xpu::ElementwiseCompute<float16,
                                           xpu::SubFunctor<float16>,
                                           PRECISION(kFP16)>;
using SubInt32 =
    xpu::ElementwiseCompute<int, xpu::SubFunctor<int>, PRECISION(kFloat)>;
using SubInt64 = xpu::ElementwiseCompute<int64_t,
                                         xpu::SubFunctor<int64_t>,
                                         PRECISION(kFloat)>;
using MulFloat32 =
    xpu::ElementwiseCompute<float, xpu::MulFunctor<float>, PRECISION(kFloat)>;
using MulFloat16 = xpu::ElementwiseCompute<float16,
                                           xpu::MulFunctor<float16>,
                                           PRECISION(kFP16)>;
using MulInt64 = xpu::ElementwiseCompute<int64_t,
                                         xpu::MulFunctor<int64_t>,
                                         PRECISION(kFloat)>;

using DivFloat32 =
    xpu::ElementwiseCompute<float, xpu::DivFunctor<float>, PRECISION(kFloat)>;
using DivFloat16 = xpu::ElementwiseCompute<float16,
                                           xpu::DivFunctor<float16>,
                                           PRECISION(kFP16)>;

using MaxFloat32 =
    xpu::ElementwiseCompute<float, xpu::MaxFunctor<float>, PRECISION(kFloat)>;
using MaxFloat16 = xpu::ElementwiseCompute<float16,
                                           xpu::MaxFunctor<float16>,
                                           PRECISION(kFP16)>;
using MaxInt32 =
    xpu::ElementwiseCompute<int, xpu::MaxFunctor<int>, PRECISION(kFloat)>;

using MinFloat32 =
    xpu::ElementwiseCompute<float, xpu::MinFunctor<float>, PRECISION(kFloat)>;

using MinFloat16 = xpu::ElementwiseCompute<float16,
                                           xpu::MinFunctor<float16>,
                                           PRECISION(kFP16)>;
using MinInt32 =
    xpu::ElementwiseCompute<int, xpu::MinFunctor<int>, PRECISION(kFloat)>;

using ModFloat32 =
    xpu::ElementwiseCompute<float, xpu::ModFunctor<float>, PRECISION(kFloat)>;

using ModFloat16 = xpu::ElementwiseCompute<float16,
                                           xpu::ModFunctor<float16>,
                                           PRECISION(kFP16)>;
using ModInt32 =
    xpu::ElementwiseCompute<int, xpu::ModFunctor<int>, PRECISION(kFloat)>;

using FloordivFloat32 = xpu::ElementwiseCompute<float,
                                                xpu::FloordivFunctor<float>,
                                                PRECISION(kFloat)>;

using FloordivFloat16 = xpu::ElementwiseCompute<float16,
                                                xpu::FloordivFunctor<float16>,
                                                PRECISION(kFP16)>;

using FloordivInt32 =
    xpu::ElementwiseCompute<int, xpu::FloordivFunctor<int>, PRECISION(kFloat)>;

using PowFloat32 =
    xpu::ElementwiseCompute<float, xpu::PowFunctor<float>, PRECISION(kFloat)>;

using PowFloat16 = xpu::ElementwiseCompute<float16,
                                           xpu::PowFunctor<float16>,
                                           PRECISION(kFP16)>;

using PowInt32 =
    xpu::ElementwiseCompute<int, xpu::PowFunctor<int>, PRECISION(kFloat)>;

using PowInt64 = xpu::ElementwiseCompute<int64_t,
                                         xpu::PowFunctor<int64_t>,
                                         PRECISION(kFloat)>;

REGISTER_LITE_KERNEL(elementwise_add, kXPU, kFloat, kNCHW, AddFloat32, def)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU))})
    .Finalize();

REGISTER_LITE_KERNEL(
    elementwise_add, kXPU, kFP16, kNCHW, AddFloat16, DISABLE_XPU1_AddFloat16)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_add, kXPU, kFloat, kNCHW, AddInt32, int32)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_add, kXPU, kFloat, kNCHW, AddInt64, int64)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt64))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt64))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt64))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_sub, kXPU, kFloat, kNCHW, SubFloat32, def)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU))})
    .Finalize();

REGISTER_LITE_KERNEL(
    elementwise_sub, kXPU, kFP16, kNCHW, SubFloat16, DISABLE_XPU1_SubFloat16)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_sub, kXPU, kFloat, kNCHW, SubInt32, int32)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_sub, kXPU, kFloat, kNCHW, SubInt64, int64)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt64))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt64))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt64))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_mul, kXPU, kFloat, kNCHW, MulFloat32, def)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU))})
    .Finalize();

REGISTER_LITE_KERNEL(
    elementwise_mul, kXPU, kFP16, kNCHW, MulFloat16, DISABLE_XPU1_MulFloat16)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_mul, kXPU, kFloat, kNCHW, MulInt64, int64)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt64))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt64))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt64))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_div, kXPU, kFloat, kNCHW, DivFloat32, def)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU))})
    .Finalize();

REGISTER_LITE_KERNEL(
    elementwise_div, kXPU, kFP16, kNCHW, DivFloat16, DISABLE_XPU1_DivFloat16)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_max, kXPU, kFloat, kNCHW, MaxFloat32, def)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU))})
    .Finalize();

REGISTER_LITE_KERNEL(
    elementwise_max, kXPU, kFP16, kNCHW, MaxFloat16, DISABLE_XPU1_MaxFloat16)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_max, kXPU, kFloat, kNCHW, MaxInt32, int32)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_min, kXPU, kFloat, kNCHW, MinFloat32, def)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU))})
    .Finalize();

REGISTER_LITE_KERNEL(
    elementwise_min, kXPU, kFP16, kNCHW, MinFloat16, DISABLE_XPU1_MinFloat16)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_min, kXPU, kFloat, kNCHW, MinInt32, int32)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_mod, kXPU, kFloat, kNCHW, ModFloat32, def)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU))})
    .Finalize();

REGISTER_LITE_KERNEL(
    elementwise_mod, kXPU, kFP16, kNCHW, ModFloat16, DISABLE_XPU1_ModFloat16)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_mod, kXPU, kFloat, kNCHW, ModInt32, int32)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .Finalize();

REGISTER_LITE_KERNEL(
    elementwise_floordiv, kXPU, kFloat, kNCHW, FloordivFloat32, def)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_floordiv,
                     kXPU,
                     kFP16,
                     kNCHW,
                     FloordivFloat16,
                     DISABLE_XPU1_FloordivFloat16)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .Finalize();

REGISTER_LITE_KERNEL(
    elementwise_floordiv, kXPU, kFloat, kNCHW, FloordivInt32, int32)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_pow, kXPU, kFloat, kNCHW, PowFloat32, def)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFloat))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFloat))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFloat))})
    .Finalize();

REGISTER_LITE_KERNEL(
    elementwise_pow, kXPU, kFP16, kNCHW, PowFloat16, DISABLE_XPU1_PowFloat16)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kFP16))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_pow, kXPU, kFloat, kNCHW, PowInt32, int32)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt32))})
    .Finalize();

REGISTER_LITE_KERNEL(elementwise_pow, kXPU, kFloat, kNCHW, PowInt64, int64)
    .BindInput("X", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt64))})
    .BindInput("Y", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt64))})
    .BindOutput("Out", {LiteType::GetTensorTy(TARGET(kXPU), PRECISION(kInt64))})
    .Finalize();
