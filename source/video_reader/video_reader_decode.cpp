#include "video_reader_imp.hpp"

namespace qlm
{
	namespace
	{
		
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
		impl->planar = false;   // these pixels are what stb_image decoded, not a planar frame
		impl->width = width;
		impl->height = height;
		impl->time = impl->timescale > 0 ? static_cast<double>(timestamp) / impl->timescale : 0.0;

		return true;
	}
}
