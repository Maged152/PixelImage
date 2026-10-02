# PixelImage
PixelImage is a C++ wrapper for the [stb](https://github.com/nothings/stb) library, designed to simplify image processing tasks. With PixelImage, you can easily read, write, and manipulate images while accessing internal pixels through intuitive methods.

## Features
- `Image I/O`: Read and write images in popular formats (JPEG, PNG, BMP, PGM).

- `Pixel Access`: Easily access and modify individual pixels.

- `Image Formats`: Supports grayscale, RGB, and other formats.

- `Random Initialization`: Seedable random image and pixel generation (`RandomGenerator`, `RandomPixel`, `Image::RandomInit`).

- `Video I/O`: Write and read MP4 video (`VideoWriter`, `VideoReader`) frame by frame, as H.264 or Motion-JPEG.

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
    bool alpha = (in.NumberOfChannels() == 4);

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
For more detailed information, check out the [documentation](./doc/README.md).

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

# Testing

Each example is tested with a golden-file check, driven by CTest.

## How it works

For every example, one test (`test_<example>`) does the following:

1. Deletes any old output, so a stale file can't give a false pass.
2. Runs the example: `<example> <tests/data> <build>/test_output/<example>`.
3. Computes the SHA256 of each produced file and of its golden copy in
   `tests/expected/`.
4. Passes if the hashes match. Fails otherwise, or if the example exits
   non-zero, or if an output is missing or empty.

| Test                        | Output checked          |
|-----------------------------|-------------------------|
| `test_example_image`        | `output.jpg`            |
| `test_example_video_writer` | `images_slide_show.mp4` |
| `test_example_video_reader` | `bright_car_30fps.mp4`  |

## Running the tests

Tests do not build anything, so build the examples first:

```bash
cmake --build <build_dir>
```

Run all tests:

```bash
ctest --test-dir <build_dir> --output-on-failure
```

Run a single test:

```bash
ctest --test-dir <build_dir> -R '^test_example_image$' --output-on-failure
```

Notes:

- `--test-dir` needs CMake 3.20+. On older versions use
  `cd <build_dir> && ctest ...`.
- With Visual Studio or Xcode, add `-C Debug` (or `Release`).
- Add `-V` to always show output, `-j4` to run in parallel.

## Updating the goldens

When an output changes intentionally:

```bash
cmake --build <build_dir> --target update_goldens
git diff --stat tests/expected    # review
git add tests/expected && git commit
```

This runs every example and overwrites its golden in `tests/expected/`.
It prints the size and SHA256 of each updated file.

## Debugging a failure

- Fresh outputs are in `<build_dir>/test_output/<example>/`.
- A mismatch prints the actual and expected SHA256 and size.
- Byte-exact hashes of media files can differ across machines or library
  versions (encoder version strings, timestamps, threading). If tests pass
  locally but fail in CI, check determinism first.
## Debugging a failure

- Fresh outputs are in `build/test_output/<example>/`.
- A mismatch prints both hashes. Compare the actual and expected files
  directly to see what changed.
- Byte-exact hashes of media files can differ across machines or library
  versions (encoder version strings, timestamps, threading). If tests pass
  locally but fail in CI, look at determinism first.

# Acknowledgments
[stb](https://github.com/nothings/stb): For providing an excellent header-only library for image I/O.

# Contributing
Contributions are welcome! Please open an issue or submit a pull request on GitHub.

# License
This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.