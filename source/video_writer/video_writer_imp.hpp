#include "video.hpp"
#include "codec_api.h"
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
	// OpenH264 encodes one frame at a time and reports the access unit of that frame as a
	// list of layers, each holding a list of NAL units that include their start code. Every
	// NAL unit is forwarded to minimp4's H.264 multiplexer, which collects the SPS/PPS into
	// an avcC box, marks the IDR frames as sync samples (stss) and builds the sample table.
	// The track timescale is 90000, so the duration of a frame is exact for the whole-number
	// frame rates the writer accepts (90000 / frame_rate).
	//
	// The encoder codes whole macroblocks and records the size it was given as frame
	// cropping in the SPS, so a frame that is not a multiple of 16 wide or high is coded
	// padded and decoded back at the size it was written as.
	// ---------------------------------------------------------------------------------
	struct H264State
	{
		MP4E_mux_t* mux = nullptr;
		mp4_h26x_writer_t writer{};
		ISVCEncoder* encoder = nullptr;
		SSourcePicture picture{};     // where the frame passed to EncodeFrame lives
		std::vector<uint8_t> i420;    // frame in the planar 4:2:0 layout the encoder wants
		int coded_width = 0;          // i420 width, and the luma stride: the width rounded up to 16
		int coded_height = 0;         // i420 height, in luma rows
		int frame_ticks = 0;          // duration of one frame, in 90 kHz units
		int frame_index = 0;          // counts the frames, for the timestamps the encoder is told
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
		int h264_quantizer = 33;

		// Number of frames accepted by WriteFrame, both formats
		int frames_written = 0;
	};
}
