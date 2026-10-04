#include "video_reader_imp.hpp"

namespace qlm
{
	bool VideoReader::Impl::DecodeMjpegSample(const int frame_index, const uint8_t*& pixels, int& out_width, int& out_height, int& out_channels)
	{
		unsigned sample_bytes = 0;
		unsigned timestamp = 0;
		unsigned sample_duration = 0;
		const MP4D_file_offset_t offset = MP4D_frame_offset(&demux, static_cast<unsigned>(track),
											static_cast<unsigned>(frame_index),
											&sample_bytes, &timestamp, &sample_duration);

		if (sample_bytes == 0 || offset + sample_bytes > file_data.size())
		{
			std::cerr << "Error: video sample " << frame_index << " is out of range." << std::endl;
			return false;
		}

		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc* decoded = stbi_load_from_memory(file_data.data() + offset, static_cast<int>(sample_bytes),
											&width, &height, &channels, 0);

		if (decoded == nullptr)
		{
			std::cerr << "Error: cannot decode video frame " << frame_index << ": " << stbi_failure_reason() << std::endl;
			return false;
		}

		// The caller converts these pixels and then releases the buffer stb_image allocated.
		this->width = width;
		this->height = height;
		this->time = timescale > 0 ? static_cast<double>(timestamp) / timescale : 0.0;

		pixels = decoded;
		out_width = width;
		out_height = height;
		out_channels = channels;
		return true;
	}
}
