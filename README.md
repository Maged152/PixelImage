# PixelImage
PixelImage is a C++ wrapper for the [stb](https://github.com/nothings/stb) library, designed to simplify image processing tasks. With PixelImage, you can easily read, write, and manipulate images while accessing internal pixels through intuitive methods.

## Features
- `Image I/O`: Read and write images in popular formats (JPEG, PNG, BMP, PGM).

- `Pixel Access`: Easily access and modify individual pixels.

- `Image Formats`: Supports grayscale, RGB, and other formats.

- `Alpha Channel Handling`: Automatically detects and handles alpha channels.

- `Simple API`: Designed for ease of use while maintaining flexibility.

# Example
Here’s a quick example to demonstrate how to use PixelImage:

```c++
#include "image.hpp"
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
    
	// Check alpha component
    bool alpha = (in.NumerOfChannels() == 4);

    /*
         Perform any operations on the image
    */

    // Save the image
    if (!in.SaveToFile("output.jpg", alpha))
    {
        std::cout << "Failed to save the image\n";
        return -1;
    }

    return 0;
}
```
For more detailed information, check out the [documentation](./doc).

# Build & Targets

## Configure 
    $ cmake -S <source_dir> -B <build_dir>

You can use `presets`

    $ cmake -S <source_dir> --preset <preset_name>

To know the existing presets

    $ cmake -S <source_dir> --list-presets

## Build
    $ cmake --build <build_dir>

## Install
    $ cmake --install <build_dir> --prefix <install_dir>

# Acknowledgments
[stb](https://github.com/nothings/stb): For providing an excellent header-only library for image I/O.

# Contributing
Contributions are welcome! Please open an issue or submit a pull request on GitHub.

# License
This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.