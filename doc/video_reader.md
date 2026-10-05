# Video reading

[`VideoReader`](#videoreader) demuxes an MP4 container and decodes one frame at a time. It is one of the two [video](video_writer.md) classes; its writing counterpart is [`VideoWriter`](video_writer.md).

Which codec a file holds is stated in its sample entry, and `VideoReader` picks the decoder from that, so one reader reads both formats:

- **Motion-JPEG** — each sample is an independent JPEG image, decoded by `stb_image`.
- **H.264** (`avc1`) — NAL units decoded by the vendored `OpenH264` decoder. An H.265 (`hvc1`) track is rejected.

Sources: [`include/video/video_reader.hpp`](../include/video/video_reader.hpp) · [`source/video_reader/`](../source/video_reader) · Runnable example: [`examples/example_video_reader.cpp`](../examples/example_video_reader.cpp)

**On this page** — [Supported container](#supported-container) · [`VideoReader`](#videoreader) · [Access modes](#access-modes) · [Example](#example) · [Known limitations](#known-limitations)

## Supported container

| | Reading |
|---|---|
| Container | MP4 / ISO base media (ISO/IEC 14496-12) |
| Codec | `jpeg` (Motion-JPEG) or `avc1` (H.264) |
| Frame formats | `GRAY` or `RGB` with `uint8_t` (1, 2, 3 or 4 channel samples) |
| Audio | ignored |
| Extensions | `.mp4`, `.m4v`, and any other MP4 file |

Reading supports both Motion-JPEG (one-component grayscale and three-component JPEG samples) and H.264 (8-bit 4:2:0 streams decoded via `OpenH264`). A grayscale sample read into an `RGB` image is replicated over the three channels.

## `VideoReader`

```cpp
namespace qlm
{
    class VideoReader
    {
    public:
        bool Open(const std::string& file_name);
        void Close();
        bool IsOpen() const;

        VideoFormat Format() const;
        int Width() const;
        int Height() const;
        int FrameCount() const;
        double FrameRate() const;
        double Duration() const;
        double Time() const;
        int FrameIndex() const;

        bool ReadFrame(Image<ImageFormat::RGB, uint8_t>& frame);
        bool ReadFrame(Image<ImageFormat::GRAY, uint8_t>& frame);
        bool ReadFrame(int frame_index, Image<ImageFormat::RGB, uint8_t>& frame);
        bool ReadFrame(int frame_index, Image<ImageFormat::GRAY, uint8_t>& frame);

        bool Seek(int frame_index);
        bool Rewind();
        bool HasEnded() const;
    };
}
```

A reader owns the demuxer and the file it read, so it is **movable but not copyable**.

| Method | Description |
|---|---|
| `Open(file_name)` | Reads the file into memory, opens the first video track, picks the decoder from its sample entry and decodes the first frame |
| `Close()` | Releases the file and the demuxer |
| `IsOpen()` | Whether a track is open |
| `Format()` | Encoding format (`VideoFormat::MP4_MJPEG` or `VideoFormat::MP4_H264`) |
| `Width()`, `Height()` | Size of the track, taken from the first frame |
| `FrameCount()` | Number of samples in the track |
| `FrameRate()` | Frames per second, `0` when the container does not state a frame duration |
| `Duration()` | Length of the track in seconds |
| `Time()` | Presentation time of the frame read last, in seconds |
| `FrameIndex()` | Index of the frame the sequential cursor points at |
| `ReadFrame(frame)` | **Sequential access**: decodes the frame at the cursor and advances it |
| `ReadFrame(index, frame)` | **Random access**: decodes `index` without moving the cursor |
| `Seek(index)` | Moves the sequential cursor to a frame index |
| `Rewind()` | Moves the sequential cursor back to the first frame |
| `HasEnded()` | Whether the cursor passed the last frame |

## Access modes

**Sequential** reads walk the track from the beginning and stop at the end:

```cpp
qlm::Image<qlm::ImageFormat::RGB, uint8_t> frame;
while (reader.ReadFrame(frame))
{
    // ... process frame ...
}
```

**Random** access decodes a specific index and leaves the sequential cursor alone:

```cpp
reader.ReadFrame(45, frame);   // frame 45, whatever the cursor points at
```

For Motion-JPEG tracks, every frame is an independent image, so `Seek` and indexed `ReadFrame` are instantaneous. For H.264 tracks, frames are decoded in GOP order with an internal cache for sequential reads; seeking backwards transparently restarts decoding from the beginning of the track.

## Example

Reading a video, printing what it holds, and saving one frame:

```cpp
#include <PixelImage.hpp>
#include <iostream>

int main()
{
    qlm::VideoReader reader;
    if (!reader.Open("slideshow.mp4"))
    {
        std::cerr << "Failed to open the video\n";
        return 1;
    }

    std::cout << reader.Width() << "x" << reader.Height() << " @ "
              << reader.FrameRate() << " fps, " << reader.FrameCount()
              << " frames (" << reader.Duration() << " seconds)\n";

    qlm::Image<qlm::ImageFormat::RGB, uint8_t> frame;

    // Sequential: decode every frame until the track ends
    int processed = 0;
    while (reader.ReadFrame(frame))
    {
        // ... brighten, filter, rewrite ...
        processed++;
    }

    // Random access: frame 45 without touching the cursor again
    if (reader.ReadFrame(45, frame))
        frame.Write("frame45.jpg");

    std::cout << "processed " << processed << " frames\n";
    return 0;
}
```

The runnable version of this program is [`examples/example_video_reader.cpp`](../examples/example_video_reader.cpp); the tests build it and check its output byte for byte (see [Testing](../README.md#testing)).

## Known limitations

- **H.264 format support.** `VideoReader` decodes standard 8-bit 4:2:0 H.264 streams; 10-bit and 4:2:2/4:4:4 streams are rejected.
- **H.264 is built, not vendored as source.** `cmake/FetchDependencies.cmake` fetches OpenH264 and builds it with the Makefile the project ships, which needs GNU make and a shell — the `gnu_*` presets provide both, and any other generator is rejected with a message that says so.
- **No audio.** An audio track in a file that is read is ignored.
- **The whole file is read into memory.** `Open` keeps the container in memory, so very large videos need proportional RAM.
- **Errors are reported coarsely.** `ReadFrame` returning `false` means the end of the track or a sample that could not be decoded; the demuxer does not say which.
- **Not thread-safe.** One instance, one thread.

## See also

- [`VideoWriter`](video_writer.md) — writing a video file
- [Image](image.md) — loading, creating and saving the frames
- [Pixel formats](pixel_formats.md) — the channel layouts a frame can have
- [Concepts and types](concepts.md) — `pixel_t`, and why only `uint8_t` video frames are instantiated
