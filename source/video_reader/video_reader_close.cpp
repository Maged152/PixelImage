#include "video_reader_imp.hpp"

namespace qlm
{
	namespace
	{
		

	}

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
}
