// MINIMP4 - vendored from https://github.com/lieff/minimp4 (CC0-1.0),
// pinned at commit 5a212a1 (the MP4D_* hardening series).
//
// Only the demuxer (MP4D_*) is used by this library. Motion-JPEG tracks need a
// 'jpeg' sample entry, which minimp4's muxer does not write, so video files are
// written by source/VideoWriter.cpp instead.

#define MINIMP4_IMPLEMENTATION

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif

#include "minimp4/minimp4.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
