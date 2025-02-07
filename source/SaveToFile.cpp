#include "image.hpp"
#include "stb/stb_image_write.h"
#include <fstream>
#include <iostream>

namespace qlm
{
	template<ImageFormat frmt, pixel_t T>
	bool Image<frmt, T>::SaveToFile(const std::string& file_name, bool alpha, int quality)
	{
		// I think stb_write supports only U8 !

		// Check if the data is valid
        if (data == nullptr || width <= 0 || height <= 0)
        {
            std::cerr << "Error: Invalid image data or dimensions." << std::endl;
            return false;
        }

		// capture the return value from the stb_image_write functions, which indicates whether the write operation was successful or not.
		int stb_status{ 0 };

		// Determine the number of components based on the image format and alpha flag
        const int comp = (frmt == ImageFormat::GRAY) ? 1 : 3;
        const int final_comp = alpha ? comp + 1 : comp;

		T* img_data = new T[width * height * final_comp];

		// copy pixel data to the output buffer
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				const Pixel<frmt, T> pix = GetPixel(x, y);
				const int idx = (y * width + x) * final_comp;

				if constexpr (frmt == ImageFormat::GRAY)
				{
					img_data[idx] = pix.v;

					if (alpha)
						img_data[idx + 1] = pix.a;
				}
				else
				{
					img_data[idx] = pix.r;
					img_data[idx + 1] = pix.g;
					img_data[idx + 2] = pix.b;
					
					if (alpha)
						img_data[idx + 3] = pix.a;
				}
			}
		}

		// Determine the file extension
		std::string ext = file_name.substr(file_name.find_last_of('.') + 1);
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

		if (ext == "bmp")
		{
			stb_status = stbi_write_bmp(file_name.c_str(), width, height, final_comp, reinterpret_cast<void*>(img_data));
		}
		else if (ext == "pgm" && final_comp == 1)
		{
			// Open the output file for writing
			std::ofstream output_file(file_name, std::ios::binary);
			if (output_file.is_open())
			{
				// Write the PGM header
				output_file << "P5\n" << width << " " << height << "\n" << std::numeric_limits<T>::max() << "\n";

				// Write the image data
				output_file.write(reinterpret_cast<char*>(img_data), width * height * sizeof(T));

				// Close the file
				output_file.close();

				stb_status = 1;
				
			}
		}
		else if (ext == "png")
		{
			int stride_in_bytes = final_comp * width * sizeof(T);
			stb_status = stbi_write_png(file_name.c_str(), width, height, final_comp, reinterpret_cast<void*>(img_data), stride_in_bytes);
		}
		else if (ext == "jpg" || ext == "jpeg")
		{
			stb_status = stbi_write_jpg(file_name.c_str(), width, height, final_comp, reinterpret_cast<void*>(img_data), quality);
		}
		else
		{
			std::cerr << "Error: Unsupported file extension '" << ext << "'." << std::endl;
		}

		delete[] img_data;

		return stb_status ? true : false;
	}

	template bool Image<ImageFormat::GRAY, uint8_t>::SaveToFile(const std::string&, bool , int);
	template bool Image<ImageFormat::RGB, uint8_t>::SaveToFile(const std::string&, bool, int);
	template bool Image<ImageFormat::GRAY, int16_t>::SaveToFile(const std::string&, bool, int);
	template bool Image<ImageFormat::RGB, int16_t>::SaveToFile(const std::string&, bool, int);
}