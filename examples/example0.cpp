#include <PixelImage.hpp>
#include <iostream>

int main()
{
    std::string file_name = "image.jpg";
	// load the image
	qlm::Image<qlm::ImageFormat::RGB, uint8_t> in;
	if (!in.LoadFromFile(file_name))
	{
		std::cout << "Failed to read the image\n";
		return -1;
	}
	// check alpha component
	bool alpha{ true };
	if (in.NumerOfChannels() == 3)
		alpha = false;

    qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> green(0, 255, 0);  // Green color

    // Set the red pixels to green
    for (int y = 0; y < in.height; y++)
    {
        for (int x = 0; x < in.width; x++)
        {
            const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> pix = in.GetPixel(x, y);
            const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> inverted_pix {
                                                                            static_cast<uint8_t>(255 - pix.r),
                                                                            static_cast<uint8_t>(255 - pix.g),
                                                                            static_cast<uint8_t>(255 - pix.b)
                                                                          };
            
                in.SetPixel(x, y, inverted_pix);
            
        }
    }

    // Save the image
    if (!in.SaveToFile("output.jpg", alpha))
    {
        std::cout << "Failed to save the image\n";
        return -1;
    }
}