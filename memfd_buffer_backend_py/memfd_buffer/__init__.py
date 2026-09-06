# Copyright 2026 Daisuke Kato
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Zero-copy Python access to memfd-backed ROS 2 buffers."""

import os
from pathlib import Path

from typing import Any
from typing import Optional

_dll_directory_handles = []


def _add_dependency_dll_directories() -> None:
    """Make dependent ROS DLLs visible to Python's Windows loader."""
    if os.name != 'nt':
        return

    prefixes = os.environ.get('AMENT_PREFIX_PATH', '').split(os.pathsep)
    package_prefix = Path(__file__).resolve().parents[3]
    prefixes.append(str(package_prefix.parent / 'memfd_buffer'))
    for prefix in prefixes:
        if not prefix:
            continue
        dll_directory = Path(prefix) / 'bin'
        if dll_directory.is_dir():
            _dll_directory_handles.append(os.add_dll_directory(str(dll_directory)))


_add_dependency_dll_directories()


from memfd_buffer._memfd_buffer_py import _NativeReadAccess  # noqa: E402
from memfd_buffer._memfd_buffer_py import _NativeWriteAccess  # noqa: E402
from memfd_buffer._memfd_buffer_py import allocate_buffer  # noqa: E402
from rosidl_buffer import Buffer  # noqa: E402


class _Access:
    """Own a native access lease and its directly-created memoryview."""

    def __init__(self, native: Any) -> None:
        self._native: Optional[Any] = native
        self._view: Optional[memoryview] = None

    @property
    def closed(self) -> bool:
        """Whether the native access lease has been released."""
        return self._native is None or self._native.closed

    @property
    def byte_count(self) -> int:
        """Return the number of payload bytes exposed by this access object."""
        if self._native is None:
            raise ValueError('access is closed')
        return self._native.byte_count

    def __enter__(self) -> memoryview:
        if self._native is None or self._native.closed:
            raise ValueError('access is closed')
        if self._view is not None:
            raise RuntimeError('access context is already entered')
        self._view = memoryview(self._native)
        return self._view

    def close(self) -> None:
        """Release the view and lease, rejecting any still-exported derived views."""
        if self._native is None:
            return
        if self._view is not None:
            self._view.release()
        self._native.close()
        self._view = None
        self._native = None

    def __exit__(self, _exc_type: Any, _exc_value: Any, _traceback: Any) -> bool:
        self.close()
        return False


class ReadAccess(_Access):
    """Scoped, read-only zero-copy access to a ROS buffer payload."""


class WriteAccess(_Access):
    """Scoped, writable zero-copy access to a memfd buffer payload."""


def read_buffer(buffer: Buffer) -> ReadAccess:
    """
    Acquire scoped read access.

    Access to a memfd-backed buffer is zero-copy. Other backends follow the
    C++ memfd promotion path and may copy into a temporary memfd allocation.
    """
    return ReadAccess(_NativeReadAccess(buffer))


def write_buffer(buffer: Buffer) -> WriteAccess:
    """Acquire scoped writable access to an existing memfd-backed buffer."""
    return WriteAccess(_NativeWriteAccess(buffer))


__all__ = [
    'Buffer',
    'ReadAccess',
    'WriteAccess',
    'allocate_buffer',
    'read_buffer',
    'write_buffer',
]
