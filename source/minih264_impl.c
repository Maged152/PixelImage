// MINIH264 - vendored from https://github.com/lieff/minih264 (CC0-1.0),
// pinned at commit b0baea7a80ef9d12da97301dd1099b8791b5ba43.
//
// Only the encoder (H264E_*) is used by this library, and only by the MP4_H264
// path of qlm::VideoWriter. The container around the encoded frames is written by
// the minimp4 multiplexer that is already vendored for reading.
//
// The encoder runs single threaded, so no thread pool has to be supplied.
//
// This translation unit is C on purpose: the encoder relies on implicit
// conversions from void*, which a C++ compiler rejects with -fpermissive.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define H264E_MAX_THREADS 0

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#define MINIH264_IMPLEMENTATION
#include "minih264/minih264e.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
