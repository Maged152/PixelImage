#include "video.hpp"
#include "minih264/minih264e.h"
#include "minimp4/minimp4.h"
#include "stb/stb_image_write.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace qlm
{
	
	// ---------------------------------------------------------------------------------
	// MP4 (ISO/IEC 14496-12) box writers.
	//
	// Only the boxes needed by a single Motion-JPEG video track are written. Because
	// every sample is an independent image, the sample table stays trivial: one chunk
	// holding all samples, a constant sample duration and no stss box (the absence of
	// stss means that every sample is a sync sample).
	// All integer fields in an MP4 file are big-endian.
	// ---------------------------------------------------------------------------------
	inline void AppendU8(std::vector<uint8_t>& out, uint8_t value)
	{
		out.push_back(value);
	}

	inline void AppendU16(std::vector<uint8_t>& out, uint16_t value)
	{
		out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
		out.push_back(static_cast<uint8_t>(value & 0xFF));
	}

	inline void AppendU32(std::vector<uint8_t>& out, uint32_t value)
	{
		out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
		out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
		out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
		out.push_back(static_cast<uint8_t>(value & 0xFF));
	}

	inline void AppendU64(std::vector<uint8_t>& out, uint64_t value)
	{
		AppendU32(out, static_cast<uint32_t>((value >> 32) & 0xFFFFFFFFu));
		AppendU32(out, static_cast<uint32_t>(value & 0xFFFFFFFFu));
	}

	inline void AppendBytes(std::vector<uint8_t>& out, const void* bytes, size_t size)
	{
		const uint8_t* first = static_cast<const uint8_t*>(bytes);
		out.insert(out.end(), first, first + size);
	}

	inline void AppendZeros(std::vector<uint8_t>& out, size_t count)
	{
		out.insert(out.end(), count, 0);
	}

	// Appends a box header and returns the position of its size field.
	inline size_t BeginBox(std::vector<uint8_t>& out, const char* type)
	{
		const size_t size_position = out.size();
		AppendU32(out, 0); // filled in by EndBox
		AppendBytes(out, type, 4);
		return size_position;
	}

	// As BeginBox, for boxes that carry a version and flags field.
	inline size_t BeginFullBox(std::vector<uint8_t>& out, const char* type, uint8_t version, uint32_t flags)
	{
		const size_t size_position = BeginBox(out, type);
		AppendU8(out, version);
		AppendU8(out, static_cast<uint8_t>((flags >> 16) & 0xFF));
		AppendU8(out, static_cast<uint8_t>((flags >> 8) & 0xFF));
		AppendU8(out, static_cast<uint8_t>(flags & 0xFF));
		return size_position;
	}

	inline void EndBox(std::vector<uint8_t>& out, size_t size_position)
	{
		const uint32_t size = static_cast<uint32_t>(out.size() - size_position);
		out[size_position] = static_cast<uint8_t>((size >> 24) & 0xFF);
		out[size_position + 1] = static_cast<uint8_t>((size >> 16) & 0xFF);
		out[size_position + 2] = static_cast<uint8_t>((size >> 8) & 0xFF);
		out[size_position + 3] = static_cast<uint8_t>(size & 0xFF);
	}
	
	// ---------------------------------------------------------------------------------
	// H.264 writing (VideoFormat::MP4_H264).
	//
	// minih264 encodes one frame at a time and reports every NAL unit of that frame
	// through a callback. The callback forwards the NAL units, start code included, to
	// minimp4's H.264 multiplexer, which collects the SPS/PPS into an avcC box, marks
	// the IDR frames as sync samples (stss) and builds the sample table. The track
	// timescale is 90000, so the duration of a frame is exact for the whole-number
	// frame rates the writer accepts (90000 / frame_rate).
	// ---------------------------------------------------------------------------------
	struct H264State
	{
		MP4E_mux_t* mux = nullptr;
		mp4_h26x_writer_t writer{};
		H264E_persist_t* encoder = nullptr;
		H264E_scratch_t* scratch = nullptr;
		std::vector<uint8_t> i420;    // frame in the planar 4:2:0 layout the encoder wants
		int frame_ticks = 0;          // duration of one frame, in 90 kHz units
		bool failed = false;          // set when a NAL unit is rejected
	};

	inline bool FileSeekTo(std::FILE* file, int64_t position)
	{
	#if defined(_MSC_VER) || defined(__MINGW32__)
		return _fseeki64(file, position, SEEK_SET) == 0;
	#else
		return std::fseek(file, static_cast<long>(position), SEEK_SET) == 0;
	#endif
	}


	// The file layout is
	//     ftyp | mdat (every JPEG sample) | moov (the index)
	// which is the same order FFmpeg writes without the +faststart option. The mdat size
	// field is patched, and the index appended, when the file is closed.
	struct VideoWriter::Impl
	{
		std::FILE* file = nullptr;
		std::string file_name;
		int width = 0;
		int height = 0;
		int frame_rate = 0;
		int quality = 90;
		int64_t mdat_size_position = 0;      // position of the mdat box size field
		uint64_t mdat_payload_position = 0;  // position of the first sample byte
		uint64_t mdat_payload_size = 0;
		std::vector<uint32_t> sample_sizes;
		std::vector<uint8_t> packed_pixels;
		std::vector<uint8_t> jpeg;

		// H.264 path
		VideoFormat format = VideoFormat::MP4_MJPEG;
		H264State h264;
		uint8_t* h264_encoder_storage = nullptr;  // raw allocations behind h264.encoder
		uint8_t* h264_scratch_storage = nullptr;  // raw allocations behind h264.scratch
		int h264_quantizer = 33;

		// Number of frames accepted by WriteFrame, both formats
		int frames_written = 0;
	};
}
