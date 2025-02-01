#pragma once

#include "pixel.hpp"
#include <string>


namespace qlm
{
	enum class BorderType
	{
		BORDER_CONSTANT,
		BORDER_REPLICATE,
		BORDER_REFLECT,
	};

	template<ImageFormat frmt, pixel_t T>
	struct BorderMode
	{
		BorderType border_type = BorderType::BORDER_CONSTANT;
		Pixel<frmt, T> border_pixel{};
	};

	template<ImageFormat frmt, pixel_t T>
	class Image
	{
	private:
		int num_of_channels;
		Pixel<frmt, T>* data;

	public:
		int width;
		int stride;
		int height;

	private:
		int ReflectBorderIndex(int idx, int max_idx) const
		{
			int reflect_idx = idx;
			if (idx < 0)
			{
				reflect_idx = -idx - 1;
			}
			else if (idx >= max_idx)
			{
				reflect_idx = max_idx - (idx - max_idx) - 1;
			}
			return reflect_idx;
		}

		void SetNumChannels()
		{
			if constexpr (frmt == ImageFormat::GRAY)
				num_of_channels = 2; // Gray + Alpha
			else if constexpr (frmt == ImageFormat::RGB)
				num_of_channels = 4; // RGB + Alpha
			else if constexpr (frmt == ImageFormat::YCrCb)
				num_of_channels = 3; // YCrCb (no alpha)
			else if constexpr (frmt == ImageFormat::HSV || frmt == ImageFormat::HLS)
				num_of_channels = 4; // HSV/HLS + Alpha
		}

	public:
		Image(): data(nullptr), width(0), height(0), stride(0)
		{
			SetNumChannels();
		}

		Image(int width, int height, int _stride = 0) : width(width), height(height)
		{
			stride = _stride == 0 ? width : _stride;
			data = new Pixel<frmt, T>[stride * height];
			SetNumChannels();
		}
		
		Image(int width, int height, Pixel<frmt, T>* data, int _stride = 0) : width(width), height(height), data(data)
		{
			stride = _stride == 0 ? width : _stride;
			SetNumChannels();
		}

		~Image()
		{
			if (data != nullptr)
			{
				delete[] data;
				data = nullptr;
			}
			width = 0;
			height = 0;
			stride = 0;
		}

		Image(const Image<frmt, T>& other) : width(other.width), height(other.height), stride(other.stride), num_of_channels(other.num_of_channels)
		{
			data = new Pixel<frmt, T>[stride * height];
			std::memcpy(data, other.data, stride * height * sizeof(Pixel<frmt, T>));
		}

		Image(Image<frmt, T>&& other) noexcept : width(other.width), height(other.height), stride(other.stride), num_of_channels(other.num_of_channels), data(other.data)
		{
			other.width = 0;
			other.height = 0;
			other.stride = 0;
			other.data = nullptr;
		}

		Image<frmt, T>& operator=(const Image<frmt, T>& other)
		{
			// Check for self-assignment
			if (this == &other)
				return *this;

			// Deallocate existing data
			if (data != nullptr)
				delete[] data;

			// Copy the other object's width ,height and number of channels
			width = other.width;
			height = other.height;
			stride = other.stride;
			num_of_channels = other.num_of_channels;

			// Allocate new memory and copy the other object's data
			data = new Pixel<frmt, T>[stride * height];
			std::memcpy(data, other.data, stride * height * sizeof(Pixel<frmt, T>));

			return *this;
		}

		Image<frmt, T>& operator=(Image<frmt, T>&& other) noexcept
		{
			if (this != &other)
			{
				if (data != nullptr)
					delete[] data;

				width = other.width;
				height = other.height;
				stride = other.stride;
				data = other.data;
				num_of_channels = other.num_of_channels;

				other.width = 0;
				other.height = 0;
				other.stride = 0;
				other.data = nullptr;
			}
			return *this;
		}

	public:
		void create(int img_width, int img_height, int img_stride = 0);

		void create(int img_width, int img_height, Pixel<frmt, T> pix, int img_stride = 0);
		
		void SetPixel(int x, int y, const Pixel<frmt, T> &pix)
		{
			if (x >= 0 && x < width && y >= 0 && y < height)
			{
				data[y * stride + x] = pix;
			}
		}

		void SetPixel(int i, const Pixel<frmt, T>& pix)
		{
			if (i >= 0 && i < width * height)
			{
				data[i] = pix;
			}	
		}

		Pixel<frmt, T> GetPixel(int x, int y) const
		{
			if (x >= 0 && x < width && y >= 0 && y < height)
			{
				return data[y * stride + x];
			}

			return Pixel<frmt, T>{};
		}

		Pixel<frmt, T> GetPixel(int i) const
		{
			if (i >= 0 && i < width * height)
			{
				return data[i];
			}
			return Pixel<frmt, T>{};
		}

		void Copy(const Image<frmt, T>& in)
		{
			if (stride == in.stride)
			{
				std::memcpy(data, in.data, stride * height * sizeof(Pixel<frmt, T>));
			}
			else
			{
				for (int y = 0; y < height; y++)
				{
					std::memcpy(&data[y * stride], &in.data[y * in.stride], width * sizeof(Pixel<frmt, T>));
				}
			}
		}

		Pixel<frmt, T> GetPixel(int x, int y, const BorderMode<frmt, T>& border_mode) const;

		bool LoadFromFile(const std::string& file_name);

		bool SaveToFile(const std::string& file_name, bool alpha = true,int quality = 100);

		int NumerOfChannels() const
		{
			return num_of_channels;
		}
	};	
}
