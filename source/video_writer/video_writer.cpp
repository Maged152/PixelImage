#include "video_writer_imp.hpp"

namespace qlm
{
	VideoWriter::VideoWriter() : impl(std::make_unique<Impl>())
	{
	}

	VideoWriter::~VideoWriter()
	{
		Close();
	}

	VideoWriter::VideoWriter(VideoWriter&& other) noexcept = default;

	VideoWriter& VideoWriter::operator=(VideoWriter&& other) noexcept
	{
		if (this != &other)
		{
			// The file this object has been writing must be finished before its handle is
			// dropped, otherwise the index (moov) would never be written and the file
			// would be left without one.
			Close();
			impl = std::move(other.impl);
		}

		return *this;
	}

	bool VideoWriter::IsOpen() const
	{
		return impl != nullptr && impl->file != nullptr;
	}

	int VideoWriter::Width() const
	{
		return impl != nullptr ? impl->width : 0;
	}

	int VideoWriter::Height() const
	{
		return impl != nullptr ? impl->height : 0;
	}

	int VideoWriter::FrameRate() const
	{
		return impl != nullptr ? impl->frame_rate : 0;
	}

	int VideoWriter::Quality() const
	{
		return impl != nullptr ? impl->quality : 0;
	}

	VideoFormat VideoWriter::Format() const
	{
		return impl != nullptr ? impl->format : VideoFormat::MP4_MJPEG;
	}

	int VideoWriter::FrameCount() const
	{
		return impl != nullptr ? impl->frames_written : 0;
	}

	bool VideoWriter::ValidateFrame(const int frame_width, const int frame_height) const
	{
		if (!IsOpen())
		{
			std::cerr << "Error: no video file is open." << std::endl;
			return false;
		}

		if (frame_width != impl->width || frame_height != impl->height)
		{
			std::cerr << "Error: frame size " << frame_width << "x" << frame_height
					  << " does not match the video size " << impl->width << "x" << impl->height << "." << std::endl;
			return false;
		}

		return true;
	}

	bool VideoWriter::WriteFrame(const Image<ImageFormat::RGB, uint8_t>& frame)
	{
		if (!ValidateFrame(frame.Width(), frame.Height()))
			return false;

		// Image stores RGBA pixels while the encoders want tightly packed RGB,
		// so the alpha channel is stripped here. Rows are read directly
		// through the friend access to frame.data (no per-pixel bounds check).
		impl->packed_pixels.resize(static_cast<size_t>(impl->width) * impl->height * 3);

		const int stride = frame.Stride();
		for (int y = 0; y < impl->height; y++)
		{
			const Pixel<ImageFormat::RGB, uint8_t>* row = frame.data + static_cast<size_t>(y) * stride;
			uint8_t* dst = impl->packed_pixels.data() + static_cast<size_t>(y) * impl->width * 3;

			for (int x = 0; x < impl->width; x++)
			{
				dst[x * 3] = row[x].r;
				dst[x * 3 + 1] = row[x].g;
				dst[x * 3 + 2] = row[x].b;
			}
		}

		return EncodeFrame(impl->packed_pixels.data(), 3);
	}

	bool VideoWriter::WriteFrame(const Image<ImageFormat::GRAY, uint8_t>& frame)
	{
		if (!ValidateFrame(frame.Width(), frame.Height()))
			return false;

		// Image stores gray+alpha pairs while the encoders want packed luma,
		// so the alpha channel is stripped here.
		impl->packed_pixels.resize(static_cast<size_t>(impl->width) * impl->height);

		const int stride = frame.Stride();
		for (int y = 0; y < impl->height; y++)
		{
			const Pixel<ImageFormat::GRAY, uint8_t>* row = frame.data + static_cast<size_t>(y) * stride;
			uint8_t* dst = impl->packed_pixels.data() + static_cast<size_t>(y) * impl->width;

			for (int x = 0; x < impl->width; x++)
				dst[x] = row[x].v;
		}

		return EncodeFrame(impl->packed_pixels.data(), 1);
	}	

	bool VideoWriter::EncodeFrame(const void* pixels, int components)
	{
		if (!IsOpen())
			return false;

		if (impl->format == VideoFormat::MP4_H264)
			return EncodeH264(pixels, components);
		else
			return EncodeMJPEG(pixels, components);
	}
}
