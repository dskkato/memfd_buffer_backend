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

#ifndef MEMFD_BUFFER__VISIBILITY_CONTROL_H_
#define MEMFD_BUFFER__VISIBILITY_CONTROL_H_

#ifdef __cplusplus
extern "C"
{
#endif

#if defined _WIN32 || defined __CYGWIN__
  #ifdef __GNUC__
    #define MEMFD_BUFFER_EXPORT __attribute__ ((dllexport))
    #define MEMFD_BUFFER_IMPORT __attribute__ ((dllimport))
  #else
    #define MEMFD_BUFFER_EXPORT __declspec(dllexport)
    #define MEMFD_BUFFER_IMPORT __declspec(dllimport)
  #endif
  #ifdef MEMFD_BUFFER_BUILDING_DLL
    #define MEMFD_BUFFER_PUBLIC MEMFD_BUFFER_EXPORT
  #else
    #define MEMFD_BUFFER_PUBLIC MEMFD_BUFFER_IMPORT
  #endif
  #define MEMFD_BUFFER_LOCAL
#else
  #define MEMFD_BUFFER_EXPORT __attribute__ ((visibility("default")))
  #define MEMFD_BUFFER_IMPORT
  #if __GNUC__ >= 4
    #define MEMFD_BUFFER_PUBLIC __attribute__ ((visibility("default")))
    #define MEMFD_BUFFER_LOCAL  __attribute__ ((visibility("hidden")))
  #else
    #define MEMFD_BUFFER_PUBLIC
    #define MEMFD_BUFFER_LOCAL
  #endif
#endif

#ifdef __cplusplus
}
#endif

#endif  // MEMFD_BUFFER__VISIBILITY_CONTROL_H_
