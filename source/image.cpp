#include "image.hpp"
#include <algorithm>

namespace qlm
{
	// create image
	template<ImageFormat frmt, pixel_t T>
	void Image<frmt, T>::create(int img_width, int img_height, Pixel<frmt, T> pix, int img_stride)
	{
		width = img_width;
		height = img_height;
		stride = img_stride == 0 ? width : img_stride;

		SetNumChannels();

		if (data != nullptr)
			delete[] data;

		data = new Pixel<frmt, T>[stride * height];

		std::fill_n(data, stride * height, pix);
	}

	template<ImageFormat frmt, pixel_t T>
	void Image<frmt, T>::create(int img_width, int img_height, int img_stride)
	{
		width = img_width;
		height = img_height;
		stride = img_stride == 0 ? width : img_stride;

		SetNumChannels();

		if (data != nullptr)
			delete[] data;

		data = new Pixel<frmt, T>[stride * height];
	}

	// Get pixel
	template<ImageFormat frmt, pixel_t T>
	Pixel<frmt, T> Image<frmt, T>::GetPixel(int x, int y, const BorderMode<frmt, T>& border_mode) const
	{
		if (x >= 0 && x < width && y >= 0 && y < height)
		{
			// Not a border pixel
			return this->GetPixel(x, y);
		}
		else
		{
			// A border pixel
			switch (border_mode.border_type)
			{
				case qlm::BorderType::BORDER_CONSTANT:
				{
					return border_mode.border_pixel;
				}
				case qlm::BorderType::BORDER_REPLICATE:
				{
					int x_idx = std::clamp(x, 0, width - 1);
					int y_idx = std::clamp(y, 0, height - 1);
					return this->GetPixel(x_idx, y_idx);
				}
				case qlm::BorderType::BORDER_REFLECT:
				{
					int x_idx = this->ReflectBorderIndex(x, width);
					int y_idx = this->ReflectBorderIndex(y, height);
					return this->GetPixel(x_idx, y_idx);
				}
				default:
				{
					return Pixel<frmt, T>{};
				}
			}
		}
	}


	// explicit instantiation
	template void Image<ImageFormat::GRAY, uint8_t>::create(int, int, Pixel<ImageFormat::GRAY, uint8_t>, int);
	template void Image<ImageFormat::GRAY, int16_t>::create(int, int, Pixel<ImageFormat::GRAY, int16_t>, int);
	template void Image<ImageFormat::GRAY, int>::create(int, int, Pixel<ImageFormat::GRAY, int>, int);
	template void Image<ImageFormat::GRAY, float>::create(int, int, Pixel<ImageFormat::GRAY, float>, int);

	template void Image<ImageFormat::RGB, uint8_t>::create(int, int, Pixel<ImageFormat::RGB, uint8_t>, int);
	template void Image<ImageFormat::RGB, int16_t>::create(int, int, Pixel<ImageFormat::RGB, int16_t>, int);
	template void Image<ImageFormat::RGB, float>::create(int, int, Pixel<ImageFormat::RGB, float>, int);

	template void Image<ImageFormat::HLS, uint8_t>::create(int, int, Pixel<ImageFormat::HLS, uint8_t>, int);
	template void Image<ImageFormat::HLS, int16_t>::create(int, int, Pixel<ImageFormat::HLS, int16_t>, int);
	template void Image<ImageFormat::HLS, float>::create(int, int, Pixel<ImageFormat::HLS, float>, int);

	template void Image<ImageFormat::HSV, uint8_t>::create(int, int, Pixel<ImageFormat::HSV, uint8_t>, int);
	template void Image<ImageFormat::HSV, int16_t>::create(int, int, Pixel<ImageFormat::HSV, int16_t>, int);
	template void Image<ImageFormat::HSV, float>::create(int, int, Pixel<ImageFormat::HSV, float>, int);

	template void Image<ImageFormat::YCrCb, uint8_t>::create(int, int, Pixel<ImageFormat::YCrCb, uint8_t>, int);
	template void Image<ImageFormat::YCrCb, int16_t>::create(int, int, Pixel<ImageFormat::YCrCb, int16_t>, int);
	template void Image<ImageFormat::YCrCb, float>::create(int, int, Pixel<ImageFormat::YCrCb, float>, int);

	// -------------------------------------------------------------------------------------------------------------
	template void Image<ImageFormat::GRAY, uint8_t>::create(int, int, int);
	template void Image<ImageFormat::GRAY, int16_t>::create(int, int, int);
	template void Image<ImageFormat::GRAY, int32_t>::create(int, int, int);
	template void Image<ImageFormat::GRAY, float>::create(int, int, int);
	template void Image<ImageFormat::RGB, uint8_t>::create(int, int, int);
	template void Image<ImageFormat::RGB, int16_t>::create(int, int, int);
	template void Image<ImageFormat::RGB, float>::create(int, int, int);
	template void Image<ImageFormat::HLS, uint8_t>::create(int, int, int);
	template void Image<ImageFormat::HLS, int16_t>::create(int, int, int);
	template void Image<ImageFormat::HLS, float>::create(int, int, int);
	template void Image<ImageFormat::HSV, uint8_t>::create(int, int, int);
	template void Image<ImageFormat::HSV, int16_t>::create(int, int, int);
	template void Image<ImageFormat::HSV, float>::create(int, int, int);
	template void Image<ImageFormat::YCrCb, uint8_t>::create(int, int, int);
	template void Image<ImageFormat::YCrCb, int16_t>::create(int, int, int);
	template void Image<ImageFormat::YCrCb, float>::create(int, int, int);
	// ------------------------------------------------------------------------------------------------------------
	template Pixel<ImageFormat::GRAY, uint8_t> Image<ImageFormat::GRAY, uint8_t>::GetPixel(int, int, const BorderMode<ImageFormat::GRAY, uint8_t>&) const;
	template Pixel<ImageFormat::GRAY, int16_t> Image<ImageFormat::GRAY, int16_t>::GetPixel(int, int, const BorderMode<ImageFormat::GRAY, int16_t>&) const;
	template Pixel<ImageFormat::GRAY, int32_t> Image<ImageFormat::GRAY, int32_t>::GetPixel(int, int, const BorderMode<ImageFormat::GRAY, int32_t>&) const;
	template Pixel<ImageFormat::GRAY, float> Image<ImageFormat::GRAY, float>::GetPixel(int, int, const BorderMode<ImageFormat::GRAY, float>&) const;
	template Pixel<ImageFormat::RGB, uint8_t> Image<ImageFormat::RGB, uint8_t>::GetPixel(int, int, const BorderMode<ImageFormat::RGB, uint8_t>&) const;
	template Pixel<ImageFormat::RGB, int16_t> Image<ImageFormat::RGB, int16_t>::GetPixel(int, int, const BorderMode<ImageFormat::RGB, int16_t>&) const;
	template Pixel<ImageFormat::RGB, float> Image<ImageFormat::RGB, float>::GetPixel(int, int, const BorderMode<ImageFormat::RGB, float>&) const;

	// -------------------------------------------------------------------------------------------------------------
}