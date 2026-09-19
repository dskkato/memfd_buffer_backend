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

#ifndef SHARED_BUFFER__VISIBILITY_CONTROL_H_
#define SHARED_BUFFER__VISIBILITY_CONTROL_H_

#ifdef __cplusplus
extern "C"
{
#endif

#if defined _WIN32 || defined __CYGWIN__
  #ifdef __GNUC__
    #define SHARED_BUFFER_EXPORT __attribute__ ((dllexport))
    #define SHARED_BUFFER_IMPORT __attribute__ ((dllimport))
  #else
    #define SHARED_BUFFER_EXPORT __declspec(dllexport)
    #define SHARED_BUFFER_IMPORT __declspec(dllimport)
  #endif
  #ifdef SHARED_BUFFER_BUILDING_DLL
    #define SHARED_BUFFER_PUBLIC SHARED_BUFFER_EXPORT
  #else
    #define SHARED_BUFFER_PUBLIC SHARED_BUFFER_IMPORT
  #endif
  #define SHARED_BUFFER_LOCAL
#else
  #define SHARED_BUFFER_EXPORT __attribute__ ((visibility("default")))
  #define SHARED_BUFFER_IMPORT
  #if __GNUC__ >= 4
    #define SHARED_BUFFER_PUBLIC __attribute__ ((visibility("default")))
    #define SHARED_BUFFER_LOCAL  __attribute__ ((visibility("hidden")))
  #else
    #define SHARED_BUFFER_PUBLIC
    #define SHARED_BUFFER_LOCAL
  #endif
#endif

#ifdef __cplusplus
}
#endif

#endif  // SHARED_BUFFER__VISIBILITY_CONTROL_H_
