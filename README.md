# PixelImage

PixelImage is a C++ wrapper around three C libraries - [stb](https://github.com/nothings/stb), [minimp4](https://github.com/lieff/minimp4) and [OpenH264](https://github.com/cisco/openh264) - that provides one easy C++ API for images and video. Read and write pictures as strongly-typed pixels, generate random images, and encode or decode MP4 video frame by frame, all through `qlm::Image`, `qlm::VideoReader` and `qlm::VideoWriter`.

There are no external dependencies: stb and minimp4 are vendored, OpenH264 is fetched and built by CMake, and the library does all the work itself.

## Features

### Image

Read, write and manipulate images (JPEG, PNG, BMP, PGM) as `Image<Format, T>` - a 2D array of strongly-typed `Pixel` values with GRAY, RGB, YCrCb, HSV and HLS formats, pixel access, alpha handling and borders. See the [image documentation](./doc/image.md).

### Random generation

Seedable random images and pixels: `RandomGenerator<T>`, `RandomPixel` and `Image::RandomInit` fill any image reproducibly from a seed. See the [random generation documentation](./doc/random_generator.md).

### Read video

`VideoReader` demuxes MP4 files and decodes every frame - H.264 or Motion-JPEG - with sequential and random access, seeking and frame timing. See the [video reading documentation](./doc/video_reader.md).

### Write video

`VideoWriter` encodes frames as H.264 (the default) or Motion-JPEG and muxes them into an MP4 container, frame by frame. See the [video writing documentation](./doc/video_writer.md).

Full documentation starts at the [documentation index](./doc/README.md).

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

# Acknowledgments
[stb](https://github.com/nothings/stb): the image reading and writing library behind `Image::Read` and `Image::Write`.

[minimp4](https://github.com/lieff/minimp4): the MP4 muxer and demuxer behind `VideoWriter` and `VideoReader`.

[OpenH264](https://github.com/cisco/openh264): the H.264 encoder and decoder used for `VideoFormat::MP4_H264`.

# Contributing
Contributions are welcome! Please open an issue or submit a pull request on GitHub.

# License
This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.