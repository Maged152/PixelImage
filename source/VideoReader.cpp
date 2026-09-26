#include "video_reader.hpp"
#include "stb/stb_image.h"
#include "minimp4/minimp4.h"

#include <cstdint>
#include <cstring>
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

		// The frames are decoded by stb_image, so the format requirement is satisfied when
		// the first sample turns out to be an image. Nothing else is checked.
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

	bool VideoReader::DecodeFrame(const int frame_index)
	{
		// Reading past the end of the track is not an error, so it stays silent
		if (!IsOpen() || frame_index < 0 || frame_index >= impl->frame_count)
			return false;

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

		CopyToRgbImage(impl->decoded, impl->decoded_width, impl->decoded_height, impl->decoded_channels, frame);
		impl->frame_index++;

		return true;
	}

	bool VideoReader::ReadFrame(Image<ImageFormat::GRAY, uint8_t>& frame)
	{
		if (impl == nullptr || !DecodeFrame(impl->frame_index))
			return false;

		CopyToGrayImage(impl->decoded, impl->decoded_width, impl->decoded_height, impl->decoded_channels, frame);
		impl->frame_index++;

		return true;
	}

	bool VideoReader::ReadFrame(const int frame_index, Image<ImageFormat::RGB, uint8_t>& frame)
	{
		if (!DecodeFrame(frame_index))
			return false;

		CopyToRgbImage(impl->decoded, impl->decoded_width, impl->decoded_height, impl->decoded_channels, frame);

		return true;
	}

	bool VideoReader::ReadFrame(const int frame_index, Image<ImageFormat::GRAY, uint8_t>& frame)
	{
		if (!DecodeFrame(frame_index))
			return false;

		CopyToGrayImage(impl->decoded, impl->decoded_width, impl->decoded_height, impl->decoded_channels, frame);

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
