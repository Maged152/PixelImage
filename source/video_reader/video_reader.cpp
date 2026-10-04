#include "video_reader_imp.hpp"

namespace qlm
{
	VideoReader::VideoReader() : impl(std::make_unique<Impl>())
	{
	}

	VideoReader::~VideoReader()
	{
		Close();
	}

	VideoReader::VideoReader(VideoReader&& other) noexcept = default;

	VideoReader& VideoReader::operator=(VideoReader&& other) noexcept = default;

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
	
	bool VideoReader::IsOpen() const
	{
		return impl != nullptr && impl->demux_open && impl->track >= 0;
	}

	bool VideoReader::ReadFrame(Image<ImageFormat::RGB, uint8_t>& frame)
	{
		if (impl == nullptr || !DecodeFrame(impl->frame_index, frame))
			return false;

		impl->frame_index++;

		return true;
	}

	bool VideoReader::ReadFrame(Image<ImageFormat::GRAY, uint8_t>& frame)
	{
		if (impl == nullptr || !DecodeFrame(impl->frame_index, frame))
			return false;

		impl->frame_index++;

		return true;
	}

	bool VideoReader::ReadFrame(const int frame_index, Image<ImageFormat::RGB, uint8_t>& frame)
	{
		return DecodeFrame(frame_index, frame);
	}

	bool VideoReader::ReadFrame(const int frame_index, Image<ImageFormat::GRAY, uint8_t>& frame)
	{
		return DecodeFrame(frame_index, frame);
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
