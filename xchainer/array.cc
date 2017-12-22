#include "xchainer/array.h"

#include <cassert>
#include <cstring>

#ifdef XCHAINER_ENABLE_CUDA
#include <cuda.h>
#include <cuda_runtime.h>
#endif  // XCHAINER_ENABLE_CUDA

#include "xchainer/array_repr.h"
#ifdef XCHAINER_ENABLE_CUDA
#include "xchainer/cuda/cuda_runtime.h"
#endif  // XCHAINER_ENABLE_CUDA
#include "xchainer/device.h"
#include "xchainer/op_node.h"

namespace xchainer {

namespace {

std::shared_ptr<void> AllocateCudaManaged(const void* src_ptr, size_t size) {
#ifdef XCHAINER_ENABLE_CUDA
    void* ptr = nullptr;
    cuda::CheckError(cudaMallocManaged(&ptr, size, cudaMemAttachGlobal));
    std::memcpy(ptr, src_ptr, size);
    return std::shared_ptr<void>(ptr, ::cudaFree);
#else
    (void)src_ptr;
    (void)size;
    assert(0);
    return std::shared_ptr<void>();
#endif  // XCHAINER_ENABLE_CUDA
}

}  // namespace

Array::Array(const Shape& shape, Dtype dtype, std::shared_ptr<void> data, int64_t offset)
    : shape_(shape), is_contiguous_(true), dtype_(dtype), data_(nullptr), offset_(offset), node_(std::make_shared<ArrayNode>()) {
    Device device = GetCurrentDevice();
    if (device == MakeDevice("cuda")) {
        // CUDA
        size_t size = static_cast<size_t>(shape_.total_size() * GetElementSize(dtype));
        data_ = AllocateCudaManaged(data.get(), size);
    } else {
        // CPU
        data_ = std::move(data);
    }
}

Array& Array::IAdd(const Array& rhs) {
    Add(rhs, *this);
    return *this;
}

Array& Array::IMul(const Array& rhs) {
    Mul(rhs, *this);
    return *this;
}

Array Array::Add(const Array& rhs) const {
    Array out = {shape_, dtype_, std::make_unique<uint8_t[]>(total_bytes())};
    Add(rhs, out);
    return out;
}

Array Array::Mul(const Array& rhs) const {
    Array out = {shape_, dtype_, std::make_unique<uint8_t[]>(total_bytes())};
    Mul(rhs, out);
    return out;
}

template <typename T>
void Array::Add(const Array& rhs, Array& out) const {
    const Array& lhs = *this;
    auto total_size = shape_.total_size();
    const T* ldata = static_cast<const T*>(lhs.data().get());
    const T* rdata = static_cast<const T*>(rhs.data().get());
    T* odata = static_cast<T*>(out.data().get());
    for (decltype(total_size) i = 0; i < total_size; i++) {
        odata[i] = ldata[i] + rdata[i];
    }

    std::shared_ptr<const ArrayNode> lhs_node = node();
    std::shared_ptr<const ArrayNode> rhs_node = rhs.node();
    std::shared_ptr<ArrayNode> out_node = out.CreateNewNode();
    std::shared_ptr<OpNode> op_node = std::make_shared<OpNode>("add", std::vector<std::shared_ptr<const ArrayNode>>{lhs_node, rhs_node});
    out_node->set_next_node(op_node);
}

template <typename T>
void Array::Mul(const Array& rhs, Array& out) const {
    const Array& lhs = *this;
    auto total_size = shape_.total_size();
    const T* ldata = static_cast<const T*>(lhs.data().get());
    const T* rdata = static_cast<const T*>(rhs.data().get());
    T* odata = static_cast<T*>(out.data().get());
    for (decltype(total_size) i = 0; i < total_size; i++) {
        odata[i] = ldata[i] * rdata[i];
    }

    std::shared_ptr<const ArrayNode> lhs_node = node();
    std::shared_ptr<const ArrayNode> rhs_node = rhs.node();
    std::shared_ptr<ArrayNode> out_node = out.CreateNewNode();
    std::shared_ptr<OpNode> op_node = std::make_shared<OpNode>("mul", std::vector<std::shared_ptr<const ArrayNode>>{lhs_node, rhs_node});
    out_node->set_next_node(op_node);
}

void Array::Add(const Array& rhs, Array& out) const {
    // TODO: dtype conversion
    CheckEqual(dtype_, rhs.dtype());
    // TODO: broadcasting
    CheckEqual(shape_, rhs.shape());
    switch (dtype_) {
        case Dtype::kBool:
            Add<bool>(rhs, out);
            break;
        case Dtype::kInt8:
            Add<int8_t>(rhs, out);
            break;
        case Dtype::kInt16:
            Add<int16_t>(rhs, out);
            break;
        case Dtype::kInt32:
            Add<int32_t>(rhs, out);
            break;
        case Dtype::kInt64:
            Add<int64_t>(rhs, out);
            break;
        case Dtype::kUInt8:
            Add<uint8_t>(rhs, out);
            break;
        case Dtype::kFloat32:
            Add<float>(rhs, out);
            break;
        case Dtype::kFloat64:
            Add<double>(rhs, out);
            break;
        default:
            assert(0);  // should never be reached
    }
}

void Array::Mul(const Array& rhs, Array& out) const {
    // TODO: dtype conversion
    CheckEqual(dtype_, rhs.dtype());
    // TODO: broadcasting
    CheckEqual(shape_, rhs.shape());
    switch (dtype_) {
        case Dtype::kBool:
            Mul<bool>(rhs, out);
            break;
        case Dtype::kInt8:
            Mul<int8_t>(rhs, out);
            break;
        case Dtype::kInt16:
            Mul<int16_t>(rhs, out);
            break;
        case Dtype::kInt32:
            Mul<int32_t>(rhs, out);
            break;
        case Dtype::kInt64:
            Mul<int64_t>(rhs, out);
            break;
        case Dtype::kUInt8:
            Mul<uint8_t>(rhs, out);
            break;
        case Dtype::kFloat32:
            Mul<float>(rhs, out);
            break;
        case Dtype::kFloat64:
            Mul<double>(rhs, out);
            break;
        default:
            assert(0);  // should never be reached
    }
}

std::string Array::ToString() const { return ArrayRepr(*this); }

namespace {

void DebugDumpComputationalGraph(std::ostream& os, const ArrayNode& array_node, int indent) {
    static const char kIndentChar = ' ';

    os << std::string(static_cast<size_t>(indent * 2), kIndentChar) << "ArrayNode<" << &array_node << ">" << std::endl;

    std::shared_ptr<const OpNode> op = array_node.next_node();
    if (op) {
        os << std::string(static_cast<size_t>((indent + 1) * 2), kIndentChar) << "Op<" << op->name() << ">" << std::endl;
        for (const std::shared_ptr<const ArrayNode>& next_node : op->next_nodes()) {
            DebugDumpComputationalGraph(os, *next_node, static_cast<size_t>(indent + 2));
        }
    }
}

}  // namespace

void DebugDumpComputationalGraph(std::ostream& os, const Array& array, int indent) {
    DebugDumpComputationalGraph(os, *array.node(), indent);
}

}  // namespace xchainer
