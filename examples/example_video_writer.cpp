#include <PixelImage.hpp>
#include <cstdint>
#include <iostream>
#include <string>


int main()
{
	const int frame_rate = 30;                       // frames per second
	const double seconds_per_image = 1.0;            // how long every image stays on screen
	const int frames_per_image =frame_rate * seconds_per_image;
	const double tolerance = 8.0;                    // allowed mean absolute error per channel
	const std::string file_name = "images_slide_show.mp4";

	// 1. Read the images.
	using ImageRGB = qlm::Image<qlm::ImageFormat::RGB, uint8_t>;
	ImageRGB image0, image1, image2, image3;

	if (!image0.LoadFromFile("image0.jpg"))
	{
		std::cout << "Failed to read the image0 \n";
		return -1;
	}

	if (!image1.LoadFromFile("image1.jpg"))
	{
		std::cout << "Failed to read the image1 \n";
		return -1;
	}

	if (!image2.LoadFromFile("image2.jpg"))
	{
		std::cout << "Failed to read the image2 \n";
		return -1;
	}

	if (!image3.LoadFromFile("image3.jpg"))
	{
		std::cout << "Failed to read the image3 \n";
		return -1;
	}

	// 2. Frames of a video should have one size
	const int video_width = image0.Width();
	const int video_height = image0.Height();

	if (image1.Width() != video_width || image1.Height() != video_height ||
		image2.Width() != video_width || image2.Height() != video_height ||
		image3.Width() != video_width || image3.Height() != video_height)
	{
		std::cout << "All images must have the same size\n";
		return -1;
	}

	std::cout << "video size: " << video_width << "x" << video_height << ", " << frame_rate
			  << " fps, " << seconds_per_image << " s per image\n";

	

	// 3. Write the slideshow: the same frame is written frames_per_image times in a row
	qlm::VideoWriter writer;

	if (!writer.Open(file_name, video_width, video_height, frame_rate, 95))
	{
		std::cout << "Failed to open the video file\n";
		return -1;
	}

	const auto WriteSecond = [&](const ImageRGB& image)
	{
		for (int frame = 0; frame < frames_per_image; frame++)
		{
			if (!writer.WriteFrame(image))
			{
				std::cout << "Failed to write a frame\n";
				return false;
			}
		}

		return true;
	};

	if (!WriteSecond(image0) || !WriteSecond(image1) || !WriteSecond(image2) || !WriteSecond(image3))
	{
		std::cout << "Failed to write the slideshow\n";
		return -1;
	}

	writer.Close(); // writes the index and closes the file
	

	std::cout << "wrote " << file_name << ", play it with any MP4 player\n";
}
