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

	bool VideoWriter::WriteFrame(const Image<ImageFormat::RGB, uint8_t>& frame)
	{
		if (!IsOpen())
		{
			std::cerr << "Error: no video file is open." << std::endl;
			return false;
		}

		if (frame.Width() != impl->width || frame.Height() != impl->height)
		{
			std::cerr << "Error: frame size " << frame.Width() << "x" << frame.Height()
					  << " does not match the video size " << impl->width << "x" << impl->height << "." << std::endl;
			return false;
		}

		if (frame.Stride() == frame.Width() * 3)
		{
			// The pixels are already tightly packed, so no copy is needed.
			return EncodeFrame(frame.data, 3);
		}
		else
		{
			// stb_image_write expects tightly packed pixels, so a padded stride is not passed on.
			impl->packed_pixels.resize(static_cast<size_t>(impl->width) * impl->height * 3);

			for (int y = 0; y < impl->height; y++)
			{
				for (int x = 0; x < impl->width; x++)
				{
					const Pixel<ImageFormat::RGB, uint8_t> pix = frame.GetPixel(x, y);
					const size_t idx = (static_cast<size_t>(y) * impl->width + x) * 3;

					impl->packed_pixels[idx] = pix.r;
					impl->packed_pixels[idx + 1] = pix.g;
					impl->packed_pixels[idx + 2] = pix.b;
				}
			}

			return EncodeFrame(impl->packed_pixels.data(), 3);
		}
	}

	bool VideoWriter::WriteFrame(const Image<ImageFormat::GRAY, uint8_t>& frame)
	{
		if (!IsOpen())
		{
			std::cerr << "Error: no video file is open." << std::endl;
			return false;
		}

		if (frame.Width() != impl->width || frame.Height() != impl->height)
		{
			std::cerr << "Error: frame size " << frame.Width() << "x" << frame.Height()
					  << " does not match the video size " << impl->width << "x" << impl->height << "." << std::endl;
			return false;
		}

		if (frame.Stride() == frame.Width())
		{
			// The pixels are already tightly packed, so no copy is needed.
			return EncodeFrame(frame.data, 1);
		}
		else
		{
			impl->packed_pixels.resize(static_cast<size_t>(impl->width) * impl->height);

			for (int y = 0; y < impl->height; y++)
			{
				for (int x = 0; x < impl->width; x++)
				{
					const Pixel<ImageFormat::GRAY, uint8_t> pix = frame.GetPixel(x, y);
					impl->packed_pixels[y * impl->width + x] = pix.v;
				}
			}

			return EncodeFrame(impl->packed_pixels.data(), 1);
		}
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
