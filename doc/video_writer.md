# Video writing

[`VideoWriter`](#videowriter) encodes one frame at a time and muxes an MP4 container. It is one of the two [video](video_reader.md) classes; its reading counterpart is [`VideoReader`](video_reader.md).

`VideoWriter` writes **H.264** by default (`VideoFormat::MP4_H264`): frames are encoded by the vendored `OpenH264` encoder and muxed into a standard `avc1` MP4 track by the vendored `minimp4`. That is what keeps video files small.

**Motion-JPEG** (`VideoFormat::MP4_MJPEG`) is the other option, and the one to pick when no video codec should be involved at all: every frame is an independent JPEG image written by `stb_image_write`, which makes every frame a random access point at the cost of file size.

Sources: [`include/video/video_writer.hpp`](../include/video/video_writer.hpp) · [`source/video_writer/`](../source/video_writer) · Runnable example: [`examples/example_video_writer.cpp`](../examples/example_video_writer.cpp)

**On this page** — [Supported container](#supported-container) · [`VideoFormat`](#videoformat) · [`VideoWriter`](#videowriter) · [`Open`](#open) · [`Close`](#close) · [`WriteFrame`](#writeframe) · [Example](#example) · [Known limitations](#known-limitations)

## Supported container

| | Writing |
|---|---|
| Container | MP4 / ISO base media (ISO/IEC 14496-12) |
| Codec | `avc1` (H.264, the default) or `jpeg` (Motion-JPEG) |
| Frame formats | `GRAY` or `RGB` with `uint8_t` |
| Audio | not written |
| Extensions | whatever name is passed to `Open` (`.mp4` is conventional) |

When writing Motion-JPEG (`VideoFormat::MP4_MJPEG`), every frame is stored as a self-contained JPEG still image.

When writing H.264 (`VideoFormat::MP4_H264`), input frames are converted to BT.601 planar I420 and encoded with a single-threaded H.264 encoder into Annex-B NAL units, packaged into standard `avc1` / `avcC` tracks. Frame dimensions for H.264 must be **even and at least 16x16**; the encoder codes them padded up to whole macroblocks and records the padding as frame cropping, so a frame reads back at exactly the size it was written as. At most 9,437,184 pixels (about 9.4 MP) per frame.

Every frame has one size: images of different sizes have to be placed on a common canvas before they are written, because the size is fixed by `Open`.

## `VideoFormat`

```cpp
namespace qlm
{
    enum class VideoFormat
    {
        MP4_MJPEG,  // Motion-JPEG frames in an MP4 container
        MP4_H264    // H.264 video in an MP4 container (the default; needs even sizes)
    };
}
```

## `VideoWriter`

```cpp
namespace qlm
{
    class VideoWriter
    {
    public:
        bool Open(const std::string& file_name, int frame_width, int frame_height, int frame_rate,
                  int quality = 90, VideoFormat format = VideoFormat::MP4_H264);

        void Close();
        bool IsOpen() const;

        bool WriteFrame(const Image<ImageFormat::RGB, uint8_t>& frame);
        bool WriteFrame(const Image<ImageFormat::GRAY, uint8_t>& frame);

        int Width() const;
        int Height() const;
        int FrameRate() const;
        int Quality() const;
        VideoFormat Format() const;
        int FrameCount() const;
    };
}
```

A writer owns an open file between `Open` and `Close`, so it is **movable but not copyable**.

### `Open`

| Parameter | Description |
|---|---|
| `file_name` | Path of the file to create; an existing file is truncated |
| `frame_width`, `frame_height` | Size of every frame; each side must be between 1 and 65535. For `MP4_H264`, both must be even, at least 16, and the frame at most 9437184 pixels |
| `frame_rate` | Frames per second, stored in the container exactly (an integer, e.g. 24, 25, 30, 60) |
| `quality` | `1` (smallest) to `100` (best); clamped, `90` by default. For `MP4_MJPEG`, maps directly to JPEG quality. For `MP4_H264`, maps linearly to QP 51 (quality 1) down to QP 10 (quality 100) |
| `format` | Container and codec combination (`MP4_H264` by default, or `MP4_MJPEG`) |

Returns `false` and prints to `std::cerr` when the size, the frame rate or the file itself is not usable.

### `Close`

Writes the index (`moov`) and closes the file. The destructor calls it too, so a file is finished when the writer goes out of scope. Until then the file holds the frames but **no index**, which means it cannot be read back yet.

### `WriteFrame`

| Overload | Content of the frame |
|---|---|
| `WriteFrame(const Image<RGB, uint8_t>&)` | Color frame: a three-component JPEG (`MP4_MJPEG`) or a 4:2:0 H.264 frame (`MP4_H264`) |
| `WriteFrame(const Image<GRAY, uint8_t>&)` | Grayscale frame: a single-component JPEG (`MP4_MJPEG`) or a 4:2:0 H.264 frame (`MP4_H264`) |

**Notes**

- Every frame must have the size given to `Open`, otherwise the call fails and prints to `std::cerr`.
- One call writes exactly one frame; a video of `n` frames needs `n` calls.
- Frames are written in the order they are passed. `MP4_H264` compresses across frames and starts a fresh prediction with a key frame every second, while `MP4_MJPEG` has no inter-frame compression at all.
- `FrameCount()` counts the frames accepted so far.
- A file is complete only once `Close` has written the index (`moov`), so a process that is killed leaves a file no player opens: the frames are there, but nothing states where they are. With `MP4_MJPEG` each of those frames is still a complete JPEG image.

## Example

Writing a slideshow — four images, one second each, at 30 frames per second:

```cpp
#include <PixelImage.hpp>
#include <iostream>

int main()
{
    const int frame_rate = 30;
    const int frames_per_image = frame_rate;   // one second of frames per image

    // Load the four images; they must all have the same size
    qlm::Image<qlm::ImageFormat::RGB, uint8_t> image0, image1, image2, image3;
    if (!image0.Read("image0.jpg") || !image1.Read("image1.jpg") ||
        !image2.Read("image2.jpg") || !image3.Read("image3.jpg"))
    {
        std::cerr << "Failed to read the images\n";
        return 1;
    }

    qlm::VideoWriter writer;
    if (!writer.Open("slideshow.mp4", image0.Width(), image0.Height(), frame_rate, 95))
    {
        std::cerr << "Failed to open the video file\n";
        return 1;
    }

    for (const auto* image : { &image0, &image1, &image2, &image3 })
    {
        for (int frame = 0; frame < frames_per_image; frame++)
        {
            if (!writer.WriteFrame(*image))
            {
                std::cerr << "Failed to write a frame\n";
                return 1;
            }
        }
    }

    writer.Close();   // writes the index; the file is complete now
    std::cout << "wrote slideshow.mp4 (" << writer.FrameCount() << " frames)\n";
    return 0;
}
```

The runnable version of this program is [`examples/example_video_writer.cpp`](../examples/example_video_writer.cpp); the tests build it and check its output byte for byte (see [Testing](../README.md#testing)).

## Known limitations

- **H.264 needs even sizes of at least 16x16, not macroblock multiples.** The encoder codes whole macroblocks and records the difference as frame cropping, so the frame reads back at exactly the size it was written as.
- **H.264 is built, not vendored as source.** `cmake/FetchDependencies.cmake` fetches OpenH264 and builds it with the Makefile the project ships, which needs GNU make and a shell — the `gnu_*` presets provide both, and any other generator is rejected with a message that says so.
- **No audio is written.**
- **Every frame has one size.** The size is fixed by `Open`; place differently sized images on a common canvas first.
- **4 GB per file.** The size field of the `mdat` box is 32 bits wide, so `WriteFrame` refuses the frame that would push the file past 4 GB.
- **A file is only complete after `Close`.** Copying the file before the writer is closed or destroyed gives frames without an index, which no player can open as a video.
- **Not thread-safe.** One instance, one thread.

## See also

- [`VideoReader`](video_reader.md) — reading the file back
- [Image](image.md) — loading, creating and saving the frames
- [Pixel formats](pixel_formats.md) — the channel layouts a frame can have
- [Concepts and types](concepts.md) — `pixel_t`, and why only `uint8_t` video frames are instantiated
