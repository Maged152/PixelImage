# PixelImage Documentation

A thin, type-safe wrapper over [stb](https://github.com/nothings/stb) `stb_image` / `stb_image_write` that exposes image data as a 2D array of strongly-typed pixels.

Everything lives in the `qlm` namespace.

**On this page** — [Requirements](#requirements) · [Quick start](#quick-start) · [Support at a glance](#support-at-a-glance) · [Documentation](#documentation)

## Requirements

- A C++20 compiler (the library uses concepts, `if constexpr`, and constrained templates).
- CMake 3.22 or newer to build.
- No external dependencies — stb is vendored under `dependencies/stb`.

## Quick start

```cpp
#include <PixelImage.hpp>
#include <iostream>

int main()
{
    // Load an image from disk
    qlm::Image<qlm::ImageFormat::RGB, uint8_t> img;
    if (!img.LoadFromFile("input.jpg"))
    {
        std::cout << "Failed to read the image\n";
        return -1;
    }

    // Invert every pixel
    for (int y = 0; y < img.Height(); y++)
    {
        for (int x = 0; x < img.Width(); x++)
        {
            const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> pix = img.GetPixel(x, y);
            img.SetPixel(x, y, qlm::Pixel<qlm::ImageFormat::RGB, uint8_t>(
                static_cast<uint8_t>(255 - pix.r),
                static_cast<uint8_t>(255 - pix.g),
                static_cast<uint8_t>(255 - pix.b)));
        }
    }

    img.SaveToFile("output.png", false);
    return 0;
}
```

To generate an image from scratch instead, create it and fill it with reproducible random pixels:

```cpp
qlm::Image<qlm::ImageFormat::RGB, uint8_t> noise(256, 256);
noise.RandomInit(42);                  // same seed -> same image
noise.SaveToFile("noise.png", false);
```

Building and consuming the library is covered in the [project README](../README.md#build--targets).

## Support at a glance

### Image formats

| `ImageFormat` | Pixel channels | Alpha channel | Channel type `T` examples |
|---|---|---|---|
| `GRAY` | `v` | yes (`a`) | `uint8_t`, `int16_t`, `float` |
| `RGB` | `r`, `g`, `b` | yes (`a`) | `uint8_t`, `int16_t`, `float` |
| `YCrCb` | `y`, `cr`, `cb` | stored in `a`, excluded from `NumberOfChannels()` | `uint8_t`, `float` |
| `HSV` | `h`, `s`, `v` | yes (`a`) | `uint8_t`, `float` |
| `HLS` | `h`, `l`, `s` | yes (`a`) | `uint8_t`, `float` |

Any `T` satisfying the `pixel_t` concept is accepted: `uint8_t`, `int16_t`, `uint16_t`, `int32_t`, or a floating-point type. See [Concepts and types](concepts.md).

### File I/O availability

File reading and writing are **only** instantiated for the following combinations. Using any other combination compiles but fails at link time.

| | `GRAY` | `RGB` |
|---|---|---|
| `uint8_t` | ✅ | ✅ |
| `int16_t` | ✅ | ✅ |
| other `T` (incl. `float`) | ❌ link error | ❌ link error |
| `YCrCb`, `HSV`, `HLS` | ❌ link error | ❌ link error |

Supported extensions: `bmp`, `png`, `jpg` / `jpeg`, `pgm` (1-component only). See [Image I/O](image.md#file-io).

### Random initialization

| Feature | Description |
|---|---|
| `qlm::RandomGenerator<T>` | Seeded value generator with a bounded range |
| `qlm::RandomPixel<frmt, T>(gen)` | Builds one random pixel for a format |
| `Image::RandomInit(seed, ...)` | Fills an existing image reproducibly |

See [Random generation](random_generator.md).

### Video I/O

Videos are read and written frame by frame. `VideoWriter` encodes H.264 (the default, through the `OpenH264` encoder) or Motion-JPEG (independent JPEG frames, which needs no video codec at all), and `VideoReader` decodes both — H.264 through the vendored `OpenH264` decoder, Motion-JPEG through `stb_image`.

| | `GRAY` | `RGB` |
|---|---|---|
| `uint8_t` | ✅ | ✅ |
| other `T` | ❌ link error | ❌ link error |

See [Video](video.md) for the container details, [example_video_writer](../examples/example_video_writer.cpp) for a slideshow built from images, and [example_video_reader](../examples/example_video_reader.cpp) for reading a video back and writing the frames after processing them.

## Documentation

| Page | Covers |
|---|---|
| [Concepts and types](concepts.md) | `ImageFormat`, `BorderType`, `BorderMode`, `pixel_t`, `arithmetic_t`, `wider_t`, `signed_t`, `cast_t` |
| [Pixel](pixel.md) | The `Pixel` interface shared by every format, plus the free pixel functions and operators |
| [Pixel formats](pixel_formats.md) | The five `Pixel` specializations: channels, constructors, `Set` overloads |
| [Image](image.md) | Memory model, constructors, pixel access, `Copy`, border handling, `LoadFromFile` / `SaveToFile` |
| [Random generation](random_generator.md) | `RandomGenerator`, `RandomPixel`, `Image::RandomInit`, determinism |
| [Video](video.md) | `VideoReader`, `VideoWriter`, the MP4 container, Motion-JPEG and H.264, frame timing |