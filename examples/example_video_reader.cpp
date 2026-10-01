#include <PixelImage.hpp>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

int main()
{
	const std::string input_video = "./tests/data/images_slide_show.mp4";
	const std::string output_video = "processed_slide_show.mp4";

	// 1. Open the input video
	qlm::VideoReader reader;
	if (!reader.Open(input_video))
	{
		std::cerr << "Failed to open input video: " << input_video << "\n";
		return -1;
	}

	std::cout << "Input Video: " << reader.Width() << "x" << reader.Height()
			  << " @ " << reader.FrameRate() << " fps, "
			  << "format: " << (reader.Format() == qlm::VideoFormat::MP4_MJPEG ? "MP4_MJPEG" : "MP4_H264") << ", "
			  << reader.FrameCount() << " frames ("
			  << reader.Duration() << " seconds)\n";

	// 2. Open the output video with matching dimensions and frame rate
	qlm::VideoWriter writer;
	const int frame_rate = reader.FrameRate() > 0.0 ? static_cast<int>(reader.FrameRate()) : 30;

	// Write output video as MP4_MJPEG (or MP4_H264 if width & height are multiples of 16)
	if (!writer.Open(output_video, reader.Width(), reader.Height(), frame_rate, 90, qlm::VideoFormat::MP4_H264))
	{
		std::cerr << "Failed to open output video: " << output_video << "\n";
		return -1;
	}

	// 3. Frame processing loop: Read -> Transform -> Write
	using ImageRGB = qlm::Image<qlm::ImageFormat::RGB, uint8_t>;
	ImageRGB frame;

	const int brightness_offset = 50; // Fixed value added to brighten each channel

	while (reader.ReadFrame(frame))
	{
		// Add fixed value to each pixel in the frame with clamping to 255
		for (int y = 0; y < frame.Height(); ++y)
		{
			for (int x = 0; x < frame.Width(); ++x)
			{
				auto pixel = frame.GetPixel(x, y);

				pixel.r = static_cast<uint8_t>(std::min(255, pixel.r + brightness_offset));
				pixel.g = static_cast<uint8_t>(std::min(255, pixel.g + brightness_offset));
				pixel.b = static_cast<uint8_t>(std::min(255, pixel.b + brightness_offset));

				frame.SetPixel(x, y, pixel);
			}
		}

		if (!writer.WriteFrame(frame))
		{
			std::cerr << "Failed writing frame " << writer.FrameCount() << "\n";
			return -1;
		}
	}

	// 4. Finalize
	writer.Close();
	reader.Close();

	std::cout << "Successfully processed " << writer.FrameCount()
			  << " frames into " << output_video << "\n";

	return 0;
}

