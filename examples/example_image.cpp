#include <PixelImage.hpp>
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    std::string in_dir = (argc > 1) ? argv[1] : "./tests/data";
    std::string out_dir = (argc > 2) ? argv[2] : ".";

    const std::string in_file = in_dir + "/image0.jpg";
    const std::string out_file = out_dir + "/output.jpg";

	// load the image
	qlm::Image<qlm::ImageFormat::RGB, uint8_t> in;
	if (!in.Read(in_file))
	{
		std::cout << "Failed to read the image: " << in_file << "\n";
		return -1;
	}
	// check alpha component
	bool alpha{ true };
	if (in.NumberOfChannels() == 3)
		alpha = false;

    qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> green(0, 255, 0);  // Green color

    // Set the red pixels to green
    for (int y = 0; y < in.Height(); y++)
    {
        for (int x = 0; x < in.Width(); x++)
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
    if (!in.Write(out_file, alpha))
    {
        std::cout << "Failed to save the image: " << out_file << "\n";
        return -1;
    }

    std::cout << "wrote " << out_file << "\n";
    return 0;
}