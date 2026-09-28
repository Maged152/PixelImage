#include "video.hpp"
#include "stb/stb_image.h"
#include "minimp4/minimp4.h"
#include "edge264.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <vector>

namespace qlm
{
	namespace
	{
		// The demuxer reads through this callback, so the file stays in memory.
		struct MemoryFile
		{
			const uint8_t* data = nullptr;
			size_t size = 0;
		};

		int ReadFromMemory(int64_t offset, void* buffer, size_t size, void* token)
		{
			const MemoryFile* file = static_cast<const MemoryFile*>(token);

			if (offset < 0 || size > file->size || static_cast<uint64_t>(offset) > file->size - size)
				return 1; // the request lies outside the file, tell minimp4 that it failed

			std::memcpy(buffer, file->data + offset, size);
			return 0;
		}

		// Converts decoded sample pixels (1, 2, 3 or 4 channels) into an RGB image.
		void CopyToRgbImage(const std::vector<uint8_t>& pixels, int width, int height, int channels,
							Image<ImageFormat::RGB, uint8_t>& frame)
		{
			if (frame.width != width || frame.height != height)
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
		void CopyToGrayImage(const std::vector<uint8_t>& pixels, int width, int height, int channels,
							 Image<ImageFormat::GRAY, uint8_t>& frame)
		{
			if (frame.width != width || frame.height != height)
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

		// An H.264 decoder decodes in order, so a frame that lies before the frame read last can only
		// be reached by decoding the track again. The frames of the samples that come after that frame
		// are kept, and this is how many of them are held at most: the oldest ones are dropped when
		// the queue grows past this.
		constexpr int kMaxDecodedFrames = 4;

		// Text for the status an edge264 call returned, used in the error messages.
		const char* H264StatusText(const int status)
		{
			switch (status)
			{
				case ENOBUFS: return "the decoder needs the frames it produced to be consumed";
				case ENOTSUP: return "the stream uses a feature the decoder does not support";
				case EBADMSG: return "the stream is not a valid H.264 stream";
				case ENOMEM: return "the decoder ran out of memory";
				default: return "the decoder rejected the stream";
			}
		}

		// Limits a value to the range a channel has.
		uint8_t ClipToByte(const int value)
		{
			return static_cast<uint8_t>(std::clamp(value, 0, 255));
		}

		// Size in bytes of the field that prefixes every NAL unit of a sample, which is the length
		// size stated in the avcC box (a value minimp4 does not hand out). The candidates are tried
		// from the widest one: the first that walks the whole sample as a series of length prefixed
		// NAL units is the size the sample uses. 0 means that the sample holds none at all.
		size_t NaluLengthSize(const uint8_t* sample, const size_t sample_bytes)
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

					// A NAL unit that is empty or that runs past the sample, and a header that cannot
					// start a NAL unit, mean that this is not the size the sample uses.
					const uint8_t header = sample[position + length_size];
					if (nalu_bytes == 0 || nalu_bytes > sample_bytes - position - length_size ||
						(header & 0x80) != 0 || (header & 0x1F) == 0 || (header & 0x1F) > 12)
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

		// A frame the decoder produced, in the packed layout the two conversion functions below
		// expect: the luma plane (width x height), then the two chroma planes of a 4:2:0 frame
		// (width / 2 x height / 2 each). It is the layout VideoWriter::ToI420() builds as well.
		struct PlanarFrame
		{
			std::vector<uint8_t> pixels;
			int width = 0;
			int height = 0;
		};

		// Copies a frame the decoder produced out of its buffers, dropping the padding it keeps
		// between the rows. Only an 8-bit 4:2:0 frame fits the layout above, so a frame of any other
		// format is reported as one this reader cannot convert.
		bool CopyDecoderFrame(const Edge264Frame& source, PlanarFrame& frame)
		{
			if (source.bit_depth_Y != 8 || source.bit_depth_C != 8 ||
				source.width_C != source.width_Y / 2 || source.height_C != source.height_Y / 2 ||
				source.samples[0] == nullptr || source.samples[1] == nullptr || source.samples[2] == nullptr)
				return false;

			const int width = source.width_Y;
			const int height = source.height_Y;
			const int chroma_width = source.width_C;
			const int chroma_height = source.height_C;

			frame.pixels.resize(static_cast<size_t>(width) * height +
								2 * static_cast<size_t>(chroma_width) * chroma_height);
			frame.width = width;
			frame.height = height;

			uint8_t* luma = frame.pixels.data();
			uint8_t* blue = luma + static_cast<size_t>(width) * height;
			uint8_t* red = blue + static_cast<size_t>(chroma_width) * chroma_height;

			for (int y = 0; y < height; y++)
				std::memcpy(luma + static_cast<size_t>(y) * width,
							source.samples[0] + static_cast<size_t>(y) * source.stride_Y, width);

			// The decoder keeps both chroma planes in one buffer, row by row: a row of Cb, then the
			// row of Cr that belongs to it, and both start a chroma stride apart.
			for (int y = 0; y < chroma_height; y++)
			{
				std::memcpy(blue + static_cast<size_t>(y) * chroma_width,
							source.samples[1] + static_cast<size_t>(y) * source.stride_C, chroma_width);
				std::memcpy(red + static_cast<size_t>(y) * chroma_width,
							source.samples[2] + static_cast<size_t>(y) * source.stride_C, chroma_width);
			}

			return true;
		}

		// Converts a packed planar 4:2:0 frame into an RGB image. The H.264 writer converts its input
		// to BT.601 limited range before it encodes it, so that is what is inverted here; chroma is
		// taken from the chroma pixel the luma pixel belongs to.
		void CopyPlanarToRgbImage(const std::vector<uint8_t>& pixels, const int width, const int height,
								  Image<ImageFormat::RGB, uint8_t>& frame)
		{
			if (frame.width != width || frame.height != height)
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
		void CopyPlanarToGrayImage(const std::vector<uint8_t>& pixels, const int width, const int height,
								   Image<ImageFormat::GRAY, uint8_t>& frame)
		{
			if (frame.width != width || frame.height != height)
				frame.Create(width, height);

			for (int y = 0; y < height; y++)
			{
				for (int x = 0; x < width; x++)
					frame.SetPixel(x, y, Pixel<ImageFormat::GRAY, uint8_t>(pixels[static_cast<size_t>(y) * width + x]));
			}
		}
	}

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

		// An H.264 track is decoded by edge264, which cannot be asked for a single frame: the samples
		// are fed to it in order and it hands out the frames it produced. The frames of the samples
		// that come after the frame read last therefore wait here, so that reading forwards does not
		// decode anything twice.
		bool h264 = false;
		bool planar = false;           // the pixels in `decoded` are a packed planar 4:2:0 frame
		Edge264Decoder* h264_decoder = nullptr;
		std::deque<PlanarFrame> h264_decoded;
		int h264_next_sample = 0;      // index of the sample the decoder is fed next
		int h264_next_frame = 0;       // index the next frame the decoder returns will have
		bool h264_flushed = false;     // the end of the stream was already reported to the decoder
		bool h264_convertible = true;  // every frame the decoder returned could be converted
		int h264_status = 0;           // last status edge264 reported, when it was not 0

		bool OpenH264(size_t sample_bytes, const std::string& file_name);
		void CloseH264();
		bool DecodeH264Headers();
		bool DecodeH264Frame(int frame_index);
		bool DecodeH264Sample(int sample_index);
		bool DecodeH264Nalu(const uint8_t* begin, const uint8_t* end);
		void DrainH264Frames();
		bool RestartH264();

		// Index of the frame at the front of the queue of decoded frames.
		int DecodedFrameBase() const;

		// Stores the frame that was read last, in whichever layout the track uses.
		void StoreFrame(Image<ImageFormat::RGB, uint8_t>& frame) const;
		void StoreFrame(Image<ImageFormat::GRAY, uint8_t>& frame) const;
	};

	VideoReader::VideoReader() : impl(std::make_unique<Impl>())
	{
	}

	VideoReader::~VideoReader()
	{
		Close();
	}

	VideoReader::VideoReader(VideoReader&& other) noexcept = default;

	VideoReader& VideoReader::operator=(VideoReader&& other) noexcept = default;

	void VideoReader::Close()
	{
		if (impl == nullptr)
			return;

		// The decoder holds the frames it produced, and the buffer of the file it was fed, so it goes
		// before the file itself does
		impl->CloseH264();

		// MP4D_close expects a demuxer that MP4D_open() filled successfully: its per-track
		// indexes are allocated with malloc(), so freeing them after a failed parse is not
		// safe. A failed open therefore keeps whatever minimp4 allocated.
		if (impl->demux_open)
		{
			MP4D_close(&impl->demux);
			impl->demux_open = false;
		}

		impl->file_data.clear();
		impl->decoded.clear();
		impl->memory = MemoryFile{};
		impl->track = -1;
		impl->width = 0;
		impl->height = 0;
		impl->decoded_width = 0;
		impl->decoded_height = 0;
		impl->decoded_channels = 0;
		impl->frame_count = 0;
		impl->frame_index = 0;
		impl->timescale = 0;
		impl->frame_rate = 0.0;
		impl->duration = 0.0;
		impl->time = 0.0;
		impl->format = VideoFormat::MP4_MJPEG;
	}

	bool VideoReader::IsOpen() const
	{
		return impl != nullptr && impl->demux_open && impl->track >= 0;
	}

	bool VideoReader::LoadFromFile(const std::string& file_name)
	{
		Close();

		if (impl == nullptr) // this object was moved from, so it owns nothing yet
			impl = std::make_unique<Impl>();

		std::ifstream input(file_name, std::ios::binary | std::ios::ate);
		if (!input.is_open())
		{
			std::cerr << "Error loading video file " << file_name << ": cannot open the file." << std::endl;
			return false;
		}

		const std::streamoff file_size = input.tellg();
		if (file_size <= 0)
		{
			std::cerr << "Error loading video file " << file_name << ": the file is empty." << std::endl;
			return false;
		}

		impl->file_data.resize(static_cast<size_t>(file_size));
		input.seekg(0);

		if (!input.read(reinterpret_cast<char*>(impl->file_data.data()), file_size))
		{
			std::cerr << "Error loading video file " << file_name << ": cannot read the file." << std::endl;
			impl->file_data.clear();
			return false;
		}

		input.close();

		impl->memory.data = impl->file_data.data();
		impl->memory.size = impl->file_data.size();

		if (!MP4D_open(&impl->demux, ReadFromMemory, &impl->memory, static_cast<int64_t>(impl->memory.size)))
		{
			std::cerr << "Error loading video file " << file_name << ": the MP4 container cannot be read." << std::endl;
			impl->file_data.clear();
			impl->memory = MemoryFile{};
			return false;
		}

		impl->demux_open = true;

		// Take the first video track that actually carries samples
		for (unsigned i = 0; i < impl->demux.track_count; i++)
		{
			if (impl->demux.track[i].handler_type == MP4D_HANDLER_TYPE_VIDE && impl->demux.track[i].sample_count > 0)
			{
				impl->track = static_cast<int>(i);
				break;
			}
		}

		if (impl->track < 0)
		{
			std::cerr << "Error loading video file " << file_name << ": no video track was found." << std::endl;
			Close();
			return false;
		}

		const MP4D_track_t& track = impl->demux.track[impl->track];

		// The first sample is where both codecs start from: stb_image decodes it to see whether the
		// track holds images at all, and the H.264 decoder needs it to produce the first frame.
		unsigned sample_bytes = 0;
		unsigned timestamp = 0;
		unsigned sample_duration = 0;
		const MP4D_file_offset_t offset = MP4D_frame_offset(&impl->demux, static_cast<unsigned>(impl->track), 0,
															 &sample_bytes, &timestamp, &sample_duration);

		if (sample_bytes == 0 || offset + sample_bytes > impl->file_data.size())
		{
			std::cerr << "Error loading video file " << file_name << ": the first video sample is out of range." << std::endl;
			Close();
			return false;
		}

		// An H.264 track holds NAL units and is decoded by edge264, while any other track is expected
		// to hold complete images that stb_image decodes (Motion-JPEG). The object type the sample
		// entry states is what picks the decoder, and it is the only thing that is checked.
		if (track.object_type_indication == MP4_OBJECT_TYPE_AVC)
		{
			impl->format = VideoFormat::MP4_H264;
			if (!impl->OpenH264(sample_bytes, file_name))
			{
				Close();
				return false;
			}
		}
		else if (track.object_type_indication == MP4_OBJECT_TYPE_HEVC)
		{
			std::cerr << "Error loading video file " << file_name
					  << ": the video track holds H.265 video, which VideoReader cannot decode." << std::endl;
			Close();
			return false;
		}
		else
		{
			impl->format = VideoFormat::MP4_MJPEG;
			int first_width = 0;
			int first_height = 0;
			int first_channels = 0;

			if (!stbi_info_from_memory(impl->file_data.data() + offset, static_cast<int>(sample_bytes),
									   &first_width, &first_height, &first_channels))
			{
				std::cerr << "Error loading video file " << file_name << ": the video track does not hold images that stb_image can decode ("
						  << stbi_failure_reason() << ")." << std::endl;
				Close();
				return false;
			}

			impl->width = first_width;
			impl->height = first_height;
		}

		impl->frame_count = static_cast<int>(track.sample_count);
		impl->frame_index = 0;
		impl->timescale = track.timescale;
		impl->time = 0.0;

		// Sample durations come from the stts box, which uses the media timescale
		impl->frame_rate = (impl->timescale > 0 && sample_duration > 0)
			? static_cast<double>(impl->timescale) / sample_duration
			: 0.0;

		const uint64_t track_duration = (static_cast<uint64_t>(track.duration_hi) << 32) | track.duration_lo;
		impl->duration = impl->timescale > 0 ? static_cast<double>(track_duration) / impl->timescale : 0.0;

		return true;
	}

	// Starts the decoder of an H.264 track and checks that it can produce the frame the first sample
	// holds. The parameter sets the container carries are the only headers the decoder ever sees,
	// because an MP4 track keeps them in its sample entry instead of in front of the samples.
	static int Edge264Log(const char* str, void* arg)
	{
		std::cout << "[edge264] " << str;
		return 0;
	}

	bool VideoReader::Impl::OpenH264(const size_t sample_bytes, const std::string& file_name)
	{
		// No threads: decoding runs on this thread, so the frames a sample completes are handed out
		// by the call that feeds it
		h264_decoder = edge264_alloc(0, Edge264Log, nullptr, 0, nullptr, nullptr, nullptr);
		if (h264_decoder == nullptr)
		{
			std::cerr << "Error loading video file " << file_name << ": the H.264 decoder cannot be allocated." << std::endl;
			return false;
		}

		h264 = true;

		if (!DecodeH264Headers())
		{
			std::cerr << "Error loading video file " << file_name << ": cannot decode the H.264 parameter sets of the track: "
					  << H264StatusText(h264_status) << "." << std::endl;
			return false;
		}

		if (sample_bytes == 0)
		{
			std::cerr << "Error loading video file " << file_name << ": the first video sample is empty." << std::endl;
			return false;
		}

		if (!DecodeH264Sample(0))
		{
			if (h264_status != 0)
				std::cerr << "Error loading video file " << file_name << ": cannot decode the first H.264 sample: "
						  << H264StatusText(h264_status) << "." << std::endl;
			return false;
		}

		if (!h264_convertible)
		{
			std::cerr << "Error loading video file " << file_name
					  << ": the H.264 track is not an 8-bit 4:2:0 stream, the only format VideoReader converts." << std::endl;
			return false;
		}

		h264_next_sample = 1;

		if (h264_decoded.empty())
		{
			// The H.264 DPB may hold the first frame until more samples arrive (reordering).
			// Decode more samples until a frame appears, or flush the decoder.
			while (h264_decoded.empty() && h264_next_sample < static_cast<int>(demux.track[track].sample_count))
			{
				if (!DecodeH264Sample(h264_next_sample))
				{
					if (h264_status != 0)
						std::cerr << "Error loading video file " << file_name << ": cannot decode H.264 sample "
								  << h264_next_sample << ": " << H264StatusText(h264_status) << "." << std::endl;
					return false;
				}

				h264_next_sample++;
			}

			// If still no frame, flush the decoder to force out any remaining frames
			if (h264_decoded.empty())
			{
				const uint8_t end_of_stream = 0;
				if (!DecodeH264Nalu(&end_of_stream, &end_of_stream))
				{
					std::cerr << "Error loading video file " << file_name
							  << ": flushing the H.264 decoder failed." << std::endl;
					return false;
				}
			}
		}

		if (h264_decoded.empty())
		{
			std::cerr << "Error loading video file " << file_name
					  << ": the H.264 decoder produced no frame for the first sample." << std::endl;
			return false;
		}

		width = h264_decoded.front().width;
		height = h264_decoded.front().height;

		return true;
	}

	// Releases the decoder and the frames it produced.
	void VideoReader::Impl::CloseH264()
	{
		if (h264_decoder != nullptr)
			edge264_free(&h264_decoder);

		h264_decoded.clear();
		h264 = false;
		planar = false;
		h264_next_sample = 0;
		h264_next_frame = 0;
		h264_flushed = false;
		h264_convertible = true;
		h264_status = 0;
	}

	// Feeds the decoder the SPS and the PPS of the track, which the demuxer hands out one at a time
	// until the list ends. They are what it needs before the first slice, and what edge264_flush()
	// drops, so a restart feeds them again.
	bool VideoReader::Impl::DecodeH264Headers()
	{
		for (int index = 0;; index++)
		{
			int sps_bytes = 0;
			const uint8_t* sps = static_cast<const uint8_t*>(MP4D_read_sps(&demux, static_cast<unsigned>(track), index, &sps_bytes));

			if (sps == nullptr)
				break;

			if (!DecodeH264Nalu(sps, sps + sps_bytes))
				return false;
		}

		for (int index = 0;; index++)
		{
			int pps_bytes = 0;
			const uint8_t* pps = static_cast<const uint8_t*>(MP4D_read_pps(&demux, static_cast<unsigned>(track), index, &pps_bytes));

			if (pps == nullptr)
				break;

			if (!DecodeH264Nalu(pps, pps + pps_bytes))
				return false;
		}

		return true;
	}

	// Puts the decoder back to the state OpenH264() left it in, which is the only way to reach a
	// frame that lies before the frames it produced already.
	bool VideoReader::Impl::RestartH264()
	{
		edge264_flush(h264_decoder);   // stops the decoding, drops the delayed frames, clears the state

		h264_decoded.clear();
		h264_next_sample = 0;
		h264_next_frame = 0;
		h264_flushed = false;
		h264_convertible = true;
		h264_status = 0;

		// The parameter sets went with the state that was cleared
		return DecodeH264Headers();
	}


	// Hands one NAL unit to the decoder and takes every frame it produced. edge264 asks for those
	// frames by returning ENOBUFS, and only continues with the NAL unit when it is handed the same
	// one again, which is what the loop does.
	bool VideoReader::Impl::DecodeH264Nalu(const uint8_t* begin, const uint8_t* end)
	{
		int status = 0;

		do
		{
			status = edge264_decode_NAL(h264_decoder, begin, end, nullptr, nullptr);
			DrainH264Frames();
		} while (status == ENOBUFS);

		// An empty NAL unit makes the frames the decoder still holds ready for output, and reports
		// that there was none left as ENODATA, which is not a failure
		if (status == 0 || (begin >= end && status == ENODATA))
			return true;

		h264_status = status;
		return false;
	}

	// Moves every frame the decoder finished into the queue. The frames have to be copied out of the
	// decoder buffers, because it cannot continue before it has been given them back.
	void VideoReader::Impl::DrainH264Frames()
	{
		Edge264Frame frame{};

		while (edge264_get_frame(h264_decoder, &frame, 0) == 0)
		{
			PlanarFrame planar;
			if (!CopyDecoderFrame(frame, planar))
			{
				h264_convertible = false;
				continue;   // keep draining, the decoder needs its frames back either way
			}

			h264_decoded.push_back(std::move(planar));
			h264_next_frame++;
		}
	}

	// Sends every NAL unit of one sample to the decoder. A sample of an MP4 H.264 track holds one or
	// more NAL units, each prefixed with its length; the size of that field is stated in the sample
	// entry, which the demuxer does not hand out, so it is read from the sample itself.
	bool VideoReader::Impl::DecodeH264Sample(const int sample_index)
	{
		unsigned sample_bytes = 0;
		unsigned timestamp = 0;
		unsigned sample_duration = 0;
		const MP4D_file_offset_t offset = MP4D_frame_offset(&demux, static_cast<unsigned>(track),
															static_cast<unsigned>(sample_index),
															&sample_bytes, &timestamp, &sample_duration);

		if (sample_bytes == 0 || offset + sample_bytes > file_data.size())
		{
			h264_status = 0;
			std::cerr << "Error: video sample " << sample_index << " is out of range." << std::endl;
			return false;
		}

		const uint8_t* sample = file_data.data() + offset;
		const size_t length_size = NaluLengthSize(sample, sample_bytes);

		if (length_size == 0)
		{
			h264_status = 0;
			std::cerr << "Error: video sample " << sample_index << " does not hold length prefixed H.264 NAL units." << std::endl;
			return false;
		}

		size_t position = 0;
		while (position < sample_bytes)
		{
			size_t nalu_bytes = 0;

			for (size_t i = 0; i < length_size; i++)
				nalu_bytes = (nalu_bytes << 8) | sample[position + i];

			if (!DecodeH264Nalu(sample + position + length_size, sample + position + length_size + nalu_bytes))
				return false;

			position += length_size + nalu_bytes;
		}

		return true;
	}


	// Decodes the frame at frame_index of an H.264 track. The decoder decodes in order, so every
	// sample up to the one that holds the frame is fed to it; the frames of the samples that come
	// after that one stay in the queue, for the reads that follow.
	bool VideoReader::Impl::DecodeH264Frame(const int frame_index)
	{
		// A frame that lies before the frames the queue holds can only be reached by decoding the
		// track from its first sample again
		if (frame_index < DecodedFrameBase())
		{
			if (!RestartH264())
			{
				std::cerr << "Error: cannot decode the H.264 track again: " << H264StatusText(h264_status) << "." << std::endl;
				return false;
			}
		}

		while (frame_index >= h264_next_frame)
		{
			if (h264_next_sample < frame_count)
			{
				if (!DecodeH264Sample(h264_next_sample))
				{
					if (h264_status != 0)
						std::cerr << "Error: cannot decode video sample " << h264_next_sample << ": "
								  << H264StatusText(h264_status) << "." << std::endl;
					return false;
				}

				h264_next_sample++;
			}
			else if (!h264_flushed)
			{
				// Reading past the last sample: an empty NAL unit makes the frames the decoder still
				// holds ready for output, and the last frames of the track come from there
				h264_flushed = true;

				const uint8_t end_of_stream = 0;
				if (!DecodeH264Nalu(&end_of_stream, &end_of_stream))
					return false;
			}
			else
			{
				std::cerr << "Error: the H.264 track ends before frame " << frame_index << "." << std::endl;
				return false;
			}

			if (!h264_convertible)
			{
				std::cerr << "Error: the H.264 track is not an 8-bit 4:2:0 stream, the only format VideoReader converts."
						  << std::endl;
				return false;
			}
		}

		const size_t position = static_cast<size_t>(frame_index - DecodedFrameBase());
		if (position >= h264_decoded.size())
		{
			std::cerr << "Error: cannot decode video frame " << frame_index << "." << std::endl;
			return false;
		}

		// The frame is copied out of the queue, which keeps it for the reads that follow
		const PlanarFrame& frame = h264_decoded[position];
		decoded = frame.pixels;
		decoded_width = frame.width;
		decoded_height = frame.height;
		decoded_channels = 0;
		planar = true;
		width = frame.width;
		height = frame.height;

		// The presentation time is that of the sample the frame belongs to, which is the sample of
		// the same index. minimp4 ignores the composition offsets, so a stream whose decoding order
		// differs from its presentation order, which needs B-frames, is only approximated.
		unsigned sample_bytes = 0;
		unsigned timestamp = 0;
		unsigned sample_duration = 0;
		MP4D_frame_offset(&demux, static_cast<unsigned>(track), static_cast<unsigned>(frame_index),
						  &sample_bytes, &timestamp, &sample_duration);
		time = timescale > 0 ? static_cast<double>(timestamp) / timescale : 0.0;

		// Drop the frames before the one that was just read: they are not needed any more, and
		// keeping them would let a long sequential read gather the entire track in memory
		while (h264_decoded.size() > 1 && DecodedFrameBase() < frame_index)
			h264_decoded.pop_front();

		return true;
	}

	// Index of the frame at the front of the queue of decoded frames.
	int VideoReader::Impl::DecodedFrameBase() const
	{
		return h264_next_frame - static_cast<int>(h264_decoded.size());
	}

	// Stores the frame that was read last in `frame`, in whichever layout the track uses: pixels that
	// stb_image decoded, or a packed planar 4:2:0 frame the H.264 decoder produced.
	void VideoReader::Impl::StoreFrame(Image<ImageFormat::RGB, uint8_t>& frame) const
	{
		if (planar)
			CopyPlanarToRgbImage(decoded, decoded_width, decoded_height, frame);
		else
			CopyToRgbImage(decoded, decoded_width, decoded_height, decoded_channels, frame);
	}

	void VideoReader::Impl::StoreFrame(Image<ImageFormat::GRAY, uint8_t>& frame) const
	{
		if (planar)
			CopyPlanarToGrayImage(decoded, decoded_width, decoded_height, frame);
		else
			CopyToGrayImage(decoded, decoded_width, decoded_height, decoded_channels, frame);
	}


	bool VideoReader::DecodeFrame(const int frame_index)
	{
		// Reading past the end of the track is not an error, so it stays silent
		if (!IsOpen() || frame_index < 0 || frame_index >= impl->frame_count)
			return false;

		// An H.264 track is decoded by the decoder instead, which keeps the frames it produced
		if (impl->h264)
			return impl->DecodeH264Frame(frame_index);

		unsigned sample_bytes = 0;
		unsigned timestamp = 0;
		unsigned sample_duration = 0;
		const MP4D_file_offset_t offset = MP4D_frame_offset(&impl->demux, static_cast<unsigned>(impl->track),
															 static_cast<unsigned>(frame_index),
															 &sample_bytes, &timestamp, &sample_duration);

		if (sample_bytes == 0 || offset + sample_bytes > impl->file_data.size())
		{
			std::cerr << "Error: video sample " << frame_index << " is out of range." << std::endl;
			return false;
		}

		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc* pixels = stbi_load_from_memory(impl->file_data.data() + offset, static_cast<int>(sample_bytes),
												&width, &height, &channels, 0);

		if (pixels == nullptr)
		{
			std::cerr << "Error: cannot decode video frame " << frame_index << ": " << stbi_failure_reason() << std::endl;
			return false;
		}

		impl->decoded.assign(pixels, pixels + static_cast<size_t>(width) * height * channels);
		stbi_image_free(pixels);

		impl->decoded_width = width;
		impl->decoded_height = height;
		impl->decoded_channels = channels;
		impl->width = width;
		impl->height = height;
		impl->time = impl->timescale > 0 ? static_cast<double>(timestamp) / impl->timescale : 0.0;

		return true;
	}

	VideoFormat VideoReader::Format() const
	{
		return impl != nullptr ? impl->format : VideoFormat::MP4_MJPEG;
	}

	int VideoReader::Width() const
	{
		return impl != nullptr ? impl->width : 0;
	}

	int VideoReader::Height() const
	{
		return impl != nullptr ? impl->height : 0;
	}

	int VideoReader::FrameCount() const
	{
		return impl != nullptr ? impl->frame_count : 0;
	}

	double VideoReader::FrameRate() const
	{
		return impl != nullptr ? impl->frame_rate : 0.0;
	}

	double VideoReader::Duration() const
	{
		return impl != nullptr ? impl->duration : 0.0;
	}

	double VideoReader::Time() const
	{
		return impl != nullptr ? impl->time : 0.0;
	}

	int VideoReader::FrameIndex() const
	{
		return impl != nullptr ? impl->frame_index : 0;
	}

	bool VideoReader::ReadFrame(Image<ImageFormat::RGB, uint8_t>& frame)
	{
		if (impl == nullptr || !DecodeFrame(impl->frame_index))
			return false;

		impl->StoreFrame(frame);
		impl->frame_index++;

		return true;
	}

	bool VideoReader::ReadFrame(Image<ImageFormat::GRAY, uint8_t>& frame)
	{
		if (impl == nullptr || !DecodeFrame(impl->frame_index))
			return false;

		impl->StoreFrame(frame);
		impl->frame_index++;

		return true;
	}

	bool VideoReader::ReadFrame(const int frame_index, Image<ImageFormat::RGB, uint8_t>& frame)
	{
		if (!DecodeFrame(frame_index))
			return false;

		impl->StoreFrame(frame);

		return true;
	}

	bool VideoReader::ReadFrame(const int frame_index, Image<ImageFormat::GRAY, uint8_t>& frame)
	{
		if (!DecodeFrame(frame_index))
			return false;

		impl->StoreFrame(frame);

		return true;
	}

	bool VideoReader::Seek(const int frame_index)
	{
		// Seeking to FrameCount() is allowed, it is the end of the track
		if (!IsOpen() || frame_index < 0 || frame_index > impl->frame_count)
			return false;

		impl->frame_index = frame_index;
		return true;
	}

	bool VideoReader::Rewind()
	{
		return Seek(0);
	}

	bool VideoReader::HasEnded() const
	{
		return !IsOpen() || impl->frame_index >= impl->frame_count;
	}
}
