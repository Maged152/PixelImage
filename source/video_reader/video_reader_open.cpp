#include "video_reader_imp.hpp"

namespace qlm
{
	namespace
	{
		int ReadFromMemory(int64_t offset, void* buffer, size_t size, void* token)
		{
			const MemoryFile* file = static_cast<const MemoryFile*>(token);

			if (offset < 0 || size > file->size || static_cast<uint64_t>(offset) > file->size - size)
				return 1; // the request lies outside the file, tell minimp4 that it failed

			std::memcpy(buffer, file->data + offset, size);
			return 0;
		}
	}

	bool VideoReader::Open(const std::string& file_name)
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

		// An H.264 track holds NAL units and is decoded by OpenH264, while any other track is
		// expected to hold complete images that stb_image decodes (Motion-JPEG). The object type the
		// sample entry states is what picks the decoder, and it is the only thing that is checked.
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
}
