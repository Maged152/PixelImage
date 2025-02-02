#include "image.hpp"
#include "stb/stb_image.h"
#include <iostream>

namespace qlm
{
	template<ImageFormat frmt, pixel_t T>
	bool Image<frmt, T>::LoadFromFile(const std::string& file_name)
	{
		int w, h, n; // width, height, number of channels
		T* img_data{ nullptr }; //  pointer to the data

		if constexpr (std::is_same_v<T, uint8_t>) // U8
		{
			img_data = stbi_load(file_name.c_str(), &w, &h, &n, 0);
		}
		else if constexpr (std::is_same_v<T, int16_t>)  // S16
		{
			img_data = (T*)stbi_load_16(file_name.c_str(), &w, &h, &n, 0);
		}
		else // floating-point
		{
			img_data = stbi_loadf(file_name.c_str(), &w, &h, &n, 0);
		}

		if (img_data == nullptr)
		{
			std::cerr << "Error loading image file " << file_name << ": " << stbi_failure_reason() << std::endl;
			return false;
		}

		width = w;
		height = h;
		stride = width;
		num_of_channels = n;

		if ((frmt == ImageFormat::GRAY && num_of_channels > 2) || (frmt == ImageFormat::RGB && num_of_channels < 3))
		{
			std::cerr << "Error loading image file " << file_name 
					<< ": Number of channels (" << n << ") is not compatible with the image format (" 
					<< (frmt == ImageFormat::GRAY ? "GRAY" : "RGB") << ")." << std::endl;
			stbi_image_free(img_data);
			return false;
		}

		delete[] data;

		data = new Pixel<frmt, T>[stride * height];

		// copy data to image object
		for (int i = 0, idx = 0; i < width * height * n; i += n, idx++)
		{
			if constexpr (frmt == ImageFormat::GRAY)
			{
				// Grayscale image: use the first channel and optional alpha channel
				const T gray = img_data[i];
				const T alpha = (n == 2) ? img_data[i + 1] : std::numeric_limits<T>::max(); // Use max value if no alpha channel

				data[idx].Set(gray, alpha);
			}
			else
			{
				// RGB image: use the first three channels and optional alpha channel
				const T r = img_data[i];
				const T g = img_data[i + 1];
				const T b = img_data[i + 2];
				const T a = (n == 4) ? img_data[i + 3] : std::numeric_limits<T>::max(); // Use max value if no alpha channel

				data[idx].Set(r, g, b, a);
			}
		}

		stbi_image_free(img_data);
		return true;
	}

	template bool Image<ImageFormat::GRAY, uint8_t>::LoadFromFile(const std::string&);
	template bool Image<ImageFormat::RGB, uint8_t>::LoadFromFile(const std::string&);
	template bool Image<ImageFormat::GRAY, int16_t>::LoadFromFile(const std::string&);
	template bool Image<ImageFormat::RGB, int16_t>::LoadFromFile(const std::string&);
}

