#include "video.hpp"
#include "stb/stb_image.h"
#include "minimp4/minimp4.h"
#include "codec_api.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <vector>

namespace qlm
{
	// A frame the decoder produced, in the packed layout the two conversion functions below
	// expect: the luma plane (width x height), then the two chroma planes of a 4:2:0 frame
	// (width / 2 x height / 2 each). It is the layout VideoWriter::ToI420() builds as well.
	struct PlanarFrame
	{
		std::vector<uint8_t> pixels;
		int width = 0;
		int height = 0;
	};

	namespace
	{
		// Limits a value to the range a channel has.
		inline uint8_t ClipToByte(const int value)
		{
			return static_cast<uint8_t>(std::clamp(value, 0, 255));
		}

		// Copies a frame the decoder produced out of its buffers, dropping the padding it keeps
		// between the rows. The caller checks that the frame is an 8-bit 4:2:0 one, the only format
		// the layout above holds, so only the shape of the planes is checked here.
		inline bool CopyDecoderFrame(unsigned char* const planes[3], const int strides[2], const int width,
							  const int height, PlanarFrame& frame)
		{
			if (width <= 0 || height <= 0 || width % 2 != 0 || height % 2 != 0 ||
				planes[0] == nullptr || planes[1] == nullptr || planes[2] == nullptr ||
				strides[0] < width || strides[1] < width / 2)
				return false;

			const int chroma_width = width / 2;
			const int chroma_height = height / 2;

			frame.pixels.resize(static_cast<size_t>(width) * height +
								2 * static_cast<size_t>(chroma_width) * chroma_height);
			frame.width = width;
			frame.height = height;

			uint8_t* luma = frame.pixels.data();
			uint8_t* blue = luma + static_cast<size_t>(width) * height;
			uint8_t* red = blue + static_cast<size_t>(chroma_width) * chroma_height;

			for (int y = 0; y < height; y++)
				std::memcpy(luma + static_cast<size_t>(y) * width,
							planes[0] + static_cast<size_t>(y) * strides[0], width);

			// The decoder keeps the three planes in three buffers of their own, row by row, and
			// every buffer carries the padding between its rows its stride states.
			for (int y = 0; y < chroma_height; y++)
			{
				std::memcpy(blue + static_cast<size_t>(y) * chroma_width,
							planes[1] + static_cast<size_t>(y) * strides[1], chroma_width);
				std::memcpy(red + static_cast<size_t>(y) * chroma_width,
							planes[2] + static_cast<size_t>(y) * strides[1], chroma_width);
			}

			return true;
		}

		// Converts a packed planar 4:2:0 frame into an RGB image. The H.264 writer converts its input
		// to BT.601 limited range before it encodes it, so that is what is inverted here; chroma is
		// taken from the chroma pixel the luma pixel belongs to.
		inline void CopyPlanarToRgbImage(const std::vector<uint8_t>& pixels, const int width, const int height,
								  Image<ImageFormat::RGB, uint8_t>& frame)
		{
			if (frame.Width() != width || frame.Height() != height)
				frame.Create(width, height);

			const int chroma_width = width / 2;
			const uint8_t* luma = pixels.data();
			const uint8_t* blue = luma + static_cast<size_t>(width) * height;
			const uint8_t* red = blue + static_cast<size_t>(chroma_width) * (height / 2);

			for (int y = 0; y < height; y++)
			{
				for (int x = 0; x < width; x++)
				{
					const int luma_value = luma[static_cast<size_t>(y) * width + x] - 16;
					const size_t chroma = static_cast<size_t>(y / 2) * chroma_width + x / 2;
					const int blue_value = blue[chroma] - 128;
					const int red_value = red[chroma] - 128;

					const int r = (298 * luma_value + 409 * red_value + 128) >> 8;
					const int g = (298 * luma_value - 100 * blue_value - 208 * red_value + 128) >> 8;
					const int b = (298 * luma_value + 516 * blue_value + 128) >> 8;

					frame.SetPixel(x, y, Pixel<ImageFormat::RGB, uint8_t>(ClipToByte(r), ClipToByte(g), ClipToByte(b)));
				}
			}
		}

		// Converts a packed planar 4:2:0 frame into a grayscale image: its luma plane is the picture,
		// and the chroma planes hold no brightness, so they are dropped.
		inline void CopyPlanarToGrayImage(const std::vector<uint8_t>& pixels, const int width, const int height,
								   Image<ImageFormat::GRAY, uint8_t>& frame)
		{
			if (frame.Width() != width || frame.Height() != height)
				frame.Create(width, height);

			for (int y = 0; y < height; y++)
			{
				for (int x = 0; x < width; x++)
					frame.SetPixel(x, y, Pixel<ImageFormat::GRAY, uint8_t>(pixels[static_cast<size_t>(y) * width + x]));
			}
		}

		// Size in bytes of the field that prefixes every NAL unit of a sample, which is the length
		// size stated in the avcC box (a value minimp4 does not hand out). The candidates are tried
		// from the widest one: the first that walks the whole sample as a series of length prefixed
		// NAL units is the size the sample uses. 0 means that the sample holds none at all.
		inline size_t NaluLengthSize(const uint8_t* sample, const size_t sample_bytes)
		{
			static constexpr size_t candidates[] = { 4, 2, 1 };

			for (const size_t length_size : candidates)
			{
				if (sample_bytes <= length_size)
					continue;

				size_t position = 0;
				bool valid = true;

				while (position + length_size < sample_bytes)
				{
					size_t nalu_bytes = 0;

					for (size_t i = 0; i < length_size; i++)
						nalu_bytes = (nalu_bytes << 8) | sample[position + i];

					// A NAL unit that is empty or that runs past the sample, and a header that
					// cannot start a NAL unit, mean that this is not the size the sample uses.
					// The type is only checked for being present: a track may carry the ones
					// the decoder ignores, and the walk that has to land exactly on the end
					// of the sample is what tells the sizes apart.
					const uint8_t header = sample[position + length_size];
					if (nalu_bytes == 0 || nalu_bytes > sample_bytes - position - length_size ||
						(header & 0x80) != 0 || (header & 0x1F) == 0)
					{
						valid = false;
						break;
					}

					position += length_size + nalu_bytes;
				}

				if (valid && position == sample_bytes)
					return length_size;
			}

			return 0;
		}

		// Converts decoded sample pixels (1, 2, 3 or 4 channels) into an RGB image.
		inline void CopyToRgbImage(const std::vector<uint8_t>& pixels, int width, int height, int channels,
							Image<ImageFormat::RGB, uint8_t>& frame)
		{
			if (frame.Width() != width || frame.Height() != height)
				frame.Create(width, height);

			for (int y = 0; y < height; y++)
			{
				for (int x = 0; x < width; x++)
				{
					const size_t idx = (static_cast<size_t>(y) * width + x) * channels;
					uint8_t r = pixels[idx];
					uint8_t g = pixels[idx];
					uint8_t b = pixels[idx];

					if (channels >= 3)
					{
						g = pixels[idx + 1];
						b = pixels[idx + 2];
					}

					frame.SetPixel(x, y, Pixel<ImageFormat::RGB, uint8_t>(r, g, b));
				}
			}
		}

		// Converts decoded sample pixels into a grayscale image.
		inline void CopyToGrayImage(const std::vector<uint8_t>& pixels, int width, int height, int channels,
							 Image<ImageFormat::GRAY, uint8_t>& frame)
		{
			if (frame.Width() != width || frame.Height() != height)
				frame.Create(width, height);

			for (int y = 0; y < height; y++)
			{
				for (int x = 0; x < width; x++)
				{
					const size_t idx = (static_cast<size_t>(y) * width + x) * channels;
					uint8_t v = pixels[idx];

					if (channels >= 3)
					{
						// BT.601 luma, the weighting the JPEG standard itself uses
						v = static_cast<uint8_t>((pixels[idx] * 299 + pixels[idx + 1] * 587 + pixels[idx + 2] * 114) / 1000);
					}

					frame.SetPixel(x, y, Pixel<ImageFormat::GRAY, uint8_t>(v));
				}
			}
		}

		// Text for the status a decoder call returned, used in the error messages.
		inline const char* H264StatusText(const int status)
		{
			switch (status)
			{
				case dsErrorFree: return "the decoder reported no error";
				case dsFramePending: return "the decoder needs more data before it can hand out a frame";
				case dsRefLost: return "the reference picture the frame needs is missing";
				case dsBitstreamError: return "the stream is not a valid H.264 stream";
				case dsDepLayerLost: return "a layer the frame depends on is missing";
				case dsNoParamSets: return "the decoder has no parameter sets the stream can use";
				case dsDataErrorConcealed: return "the decoder concealed an error in the frame";
				case dsRefListNullPtrs: return "the reference list of the frame holds no picture";
				case dsInvalidArgument: return "the decoder was given an argument it cannot use";
				case dsInitialOptExpected: return "the decoder was not initialized yet";
				case dsOutOfMemory: return "the decoder ran out of memory";
				case dsDstBufNeedExpan: return "the output buffer of the decoder is too small";
				default: return "the decoder rejected the stream";
			}
		}

		// Reports whether a status a decoder call returned says that the call did not go well. A
		// frame that is still pending with the decoder, and a frame whose error the decoder
		// concealed, are both normal outcomes of a call, so neither of them is one.
		inline bool H264StatusIsError(const int status)
		{
			return (status & ~(dsErrorFree | dsFramePending | dsDataErrorConcealed)) != 0;
		}
	}

	// The demuxer reads through this callback, so the file stays in memory.
	struct MemoryFile
	{
		const uint8_t* data = nullptr;
		size_t size = 0;
	};

	struct VideoReader::Impl
	{
		std::vector<uint8_t> file_data;
		std::vector<uint8_t> decoded;     // pixel data of the frame read last
		MemoryFile memory;                // token handed to minimp4, kept alive with the demuxer
		MP4D_demux_t demux{};
		bool demux_open = false;
		int track = -1;
		int width = 0;
		int height = 0;
		int decoded_width = 0;
		int decoded_height = 0;
		int decoded_channels = 0;
		int frame_count = 0;
		int frame_index = 0;
		unsigned timescale = 0;
		double frame_rate = 0.0;
		double duration = 0.0;
		double time = 0.0;
		VideoFormat format = VideoFormat::MP4_MJPEG;

		// An H.264 track is decoded by OpenH264, which cannot be asked for a single frame: the samples
		// are fed to it in order and it hands out the frames it produced. The frames of the samples
		// that come after the frame read last therefore wait here, so that reading forwards does not
		// decode anything twice.
		bool h264 = false;
		bool planar = false;           // the pixels in `decoded` are a packed planar 4:2:0 frame
		ISVCDecoder* h264_decoder = nullptr;
		std::vector<uint8_t> h264_annex_b;  // the access unit that is being built, in Annex B form
		std::deque<PlanarFrame> h264_decoded;
		int h264_next_sample = 0;      // index of the sample the decoder is fed next
		int h264_frames_stored = 0;    // frames the queue took since the decoder was last started
		bool h264_flushed = false;     // the end of the stream was already reported to the decoder
		bool h264_convertible = true;  // every frame the decoder returned could be converted
		int h264_status = 0;           // last status the decoder reported, when it reported a failure

		bool StartH264Decoder();
		bool OpenH264(size_t sample_bytes, const std::string& file_name);
		void CloseH264();
		bool DecodeH264Headers();
		bool DecodeH264Frame(int frame_index);
		bool DecodeH264Sample(int sample_index);
		void AppendH264Nalu(const uint8_t* begin, const uint8_t* end);
		bool FeedH264();
		bool StoreH264Picture(unsigned char* const planes[3], const SBufferInfo& info);
		bool FlushH264();
		bool RestartH264();

		// Index of the frame at the front of the queue of decoded frames.
		int DecodedFrameBase() const;

		// Stores the frame that was read last, in whichever layout the track uses.
		void StoreFrame(Image<ImageFormat::RGB, uint8_t>& frame) const;
		void StoreFrame(Image<ImageFormat::GRAY, uint8_t>& frame) const;
	};
}
