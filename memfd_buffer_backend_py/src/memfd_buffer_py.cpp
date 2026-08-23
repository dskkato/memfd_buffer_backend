// Copyright 2026 Daisuke Kato
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

#include <pybind11/pybind11.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

#include "memfd_buffer/memfd_buffer_api.hpp"
#include "rosidl_buffer/buffer.hpp"

namespace py = pybind11;

namespace memfd_buffer_backend
{
namespace
{

rosidl::Buffer<std::uint8_t> * buffer_pointer(const py::object & owner)
{
  py::object get_pointer = py::module_::import("rosidl_buffer").attr("_get_buffer_ptr");
  const std::uintptr_t address = get_pointer(owner).cast<std::uintptr_t>();
  if (address == 0) {
    throw std::invalid_argument("rosidl buffer pointer must not be null");
  }
  return reinterpret_cast<rosidl::Buffer<std::uint8_t> *>(address);
}

py::object allocate_python_buffer(std::size_t byte_count)
{
  if (byte_count == 0) {
    throw py::value_error("byte_count must be greater than zero");
  }
  if (byte_count > static_cast<std::size_t>(std::numeric_limits<Py_ssize_t>::max())) {
    throw std::overflow_error("byte_count exceeds Python buffer protocol limits");
  }

  auto buffer = std::make_unique<rosidl::Buffer<std::uint8_t>>(allocate_buffer(byte_count));
  py::object take_buffer = py::module_::import("rosidl_buffer").attr("_take_buffer_from_ptr");
  py::object result = take_buffer(
    py::int_(reinterpret_cast<std::uintptr_t>(buffer.get())));
  // The ownership of the buffer is transferred to Python, so we can release it here.
  [[maybe_unused]] auto * released_buffer = buffer.release();
  return result;
}

class NativeReadAccess
{
public:
  explicit NativeReadAccess(py::object owner)
  : owner_(std::move(owner))
  {
    buffer_ = buffer_pointer(owner_);
    byte_count_ = buffer_->size();
    if (byte_count_ > static_cast<std::size_t>(std::numeric_limits<Py_ssize_t>::max())) {
      throw std::overflow_error("buffer size exceeds Python buffer protocol limits");
    }
    handle_ = from_input_buffer(*buffer_);
  }

  NativeReadAccess(const NativeReadAccess &) = delete;
  NativeReadAccess & operator=(const NativeReadAccess &) = delete;

  py::buffer_info buffer_info()
  {
    if (closed_) {
      throw py::buffer_error("read access is closed");
    }
    return py::buffer_info(
      handle_.get_ptr(), static_cast<Py_ssize_t>(byte_count_), true);
  }

  void close()
  {
    if (exports_ != 0) {
      throw py::buffer_error("cannot close read access while exported views are alive");
    }
    if (!closed_) {
      handle_ = ReadHandle();
      owner_ = py::none();
      buffer_ = nullptr;
      closed_ = true;
    }
  }

  bool closed() const {return closed_;}
  std::size_t byte_count() const {return byte_count_;}
  void add_export() {++exports_;}
  void remove_export() noexcept {if (exports_ != 0) {--exports_;}}

private:
  py::object owner_;
  rosidl::Buffer<std::uint8_t> * buffer_{nullptr};
  ReadHandle handle_;
  std::size_t byte_count_{0};
  std::size_t exports_{0};
  bool closed_{false};
};

class NativeWriteAccess
{
public:
  explicit NativeWriteAccess(py::object owner)
  : owner_(std::move(owner))
  {
    buffer_ = buffer_pointer(owner_);
    byte_count_ = buffer_->size();
    if (byte_count_ > static_cast<std::size_t>(std::numeric_limits<Py_ssize_t>::max())) {
      throw std::overflow_error("buffer size exceeds Python buffer protocol limits");
    }
    handle_ = from_output_buffer(*buffer_);
  }

  NativeWriteAccess(const NativeWriteAccess &) = delete;
  NativeWriteAccess & operator=(const NativeWriteAccess &) = delete;

  py::buffer_info buffer_info()
  {
    if (closed_) {
      throw py::buffer_error("write access is closed");
    }
    return py::buffer_info(
      handle_.get_ptr(), static_cast<Py_ssize_t>(byte_count_), false);
  }

  void close()
  {
    if (exports_ != 0) {
      throw py::buffer_error("cannot close write access while exported views are alive");
    }
    if (!closed_) {
      handle_ = WriteHandle();
      owner_ = py::none();
      buffer_ = nullptr;
      closed_ = true;
    }
  }

  bool closed() const {return closed_;}
  std::size_t byte_count() const {return byte_count_;}
  void add_export() {++exports_;}
  void remove_export() noexcept {if (exports_ != 0) {--exports_;}}

private:
  py::object owner_;
  rosidl::Buffer<std::uint8_t> * buffer_{nullptr};
  WriteHandle handle_;
  std::size_t byte_count_{0};
  std::size_t exports_{0};
  bool closed_{false};
};

template<typename AccessT>
struct BufferCallbacks
{
  static getbufferproc original_getbuffer;
  static releasebufferproc original_releasebuffer;

  static int getbuffer(PyObject * object, Py_buffer * view, int flags)
  {
    const int result = original_getbuffer(object, view, flags);
    if (result == 0) {
      try {
        py::cast<AccessT &>(py::handle(object)).add_export();
      } catch (...) {
        original_releasebuffer(object, view);
        PyErr_SetString(PyExc_BufferError, "failed to retain memfd buffer export");
        return -1;
      }
    }
    return result;
  }

  static void releasebuffer(PyObject * object, Py_buffer * view)
  {
    try {
      py::cast<AccessT &>(py::handle(object)).remove_export();
    } catch (...) {
      PyErr_Clear();
    }
    original_releasebuffer(object, view);
  }
};

template<typename AccessT>
getbufferproc BufferCallbacks<AccessT>::original_getbuffer = nullptr;

template<typename AccessT>
releasebufferproc BufferCallbacks<AccessT>::original_releasebuffer = nullptr;

template<typename AccessT>
void install_tracked_buffer_callbacks(const py::object & type_object)
{
  auto * type = reinterpret_cast<PyTypeObject *>(type_object.ptr());
  if (type->tp_as_buffer == nullptr || type->tp_as_buffer->bf_getbuffer == nullptr ||
    type->tp_as_buffer->bf_releasebuffer == nullptr)
  {
    throw std::runtime_error("pybind11 did not install buffer protocol callbacks");
  }
  BufferCallbacks<AccessT>::original_getbuffer = type->tp_as_buffer->bf_getbuffer;
  BufferCallbacks<AccessT>::original_releasebuffer = type->tp_as_buffer->bf_releasebuffer;
  type->tp_as_buffer->bf_getbuffer = &BufferCallbacks<AccessT>::getbuffer;
  type->tp_as_buffer->bf_releasebuffer = &BufferCallbacks<AccessT>::releasebuffer;
}

}  // namespace
}  // namespace memfd_buffer_backend

PYBIND11_MODULE(_memfd_buffer_py, module)
{
  using memfd_buffer_backend::NativeReadAccess;
  using memfd_buffer_backend::NativeWriteAccess;

  module.doc() = "Scoped Python buffer-protocol access to memfd-backed rosidl buffers";

  py::class_<NativeReadAccess> read_class(
    module, "_NativeReadAccess", py::buffer_protocol());
  read_class
  .def(py::init<py::object>())
  .def_buffer(&NativeReadAccess::buffer_info)
  .def("close", &NativeReadAccess::close)
  .def_property_readonly("closed", &NativeReadAccess::closed)
  .def_property_readonly("byte_count", &NativeReadAccess::byte_count);

  py::class_<NativeWriteAccess> write_class(
    module, "_NativeWriteAccess", py::buffer_protocol());
  write_class
  .def(py::init<py::object>())
  .def_buffer(&NativeWriteAccess::buffer_info)
  .def("close", &NativeWriteAccess::close)
  .def_property_readonly("closed", &NativeWriteAccess::closed)
  .def_property_readonly("byte_count", &NativeWriteAccess::byte_count);

  memfd_buffer_backend::install_tracked_buffer_callbacks<NativeReadAccess>(read_class);
  memfd_buffer_backend::install_tracked_buffer_callbacks<NativeWriteAccess>(write_class);

  module.def(
    "allocate_buffer", &memfd_buffer_backend::allocate_python_buffer, py::arg("byte_count"));
}
