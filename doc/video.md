# Video

Two classes read and write video files, one frame at a time:

| Component | Role |
|---|---|
| [`VideoReader`](#videoreader) | Demuxes an MP4 container (minimp4) and decodes every frame with `stb_image` |
| [`VideoWriter`](#videowriter) | Encodes every frame (Motion-JPEG via `stb_image_write` or H.264 via `minih264`) and muxes an MP4 container |

The default track format is **Motion-JPEG**, where every frame is an independent JPEG image. That keeps the reader and the writer small — and makes every frame a random access point — at the cost of file size.

For significantly smaller files, `VideoWriter` also supports **H.264** (`VideoFormat::MP4_H264`), encoded by vendored `minih264` and muxed into a standard `avc1` MP4 track using `minimp4`.

Sources: [`include/video_reader.hpp`](../include/video_reader.hpp) · [`include/video_writer.hpp`](../include/video_writer.hpp) · Runnable example: [`examples/example_video_writer.cpp`](../examples/example_video_writer.cpp)

**On this page** — [Supported container](#supported-container) · [`VideoWriter`](#videowriter) · [`VideoReader`](#videoreader) · [Examples](#examples) · [Known limitations](#known-limitations)

## Supported container

| | Reading | Writing |
|---|---|---|
| Container | MP4 / ISO base media (ISO/IEC 14496-12) | MP4 / ISO base media |
| Codec | any single JPEG sample, `jpeg` sample entry | `jpeg` (Motion-JPEG, default) or `avc1` (H.264) |
| Frame formats | `GRAY` or `RGB` with `uint8_t` (1, 2, 3 or 4 channel samples) | `GRAY` or `RGB` with `uint8_t` |
| Audio | ignored | not written |
| Extensions | `.mp4`, `.m4v`, and any other MP4 file | whatever name is passed to `Open` (`.mp4` is conventional) |

When writing Motion-JPEG (`VideoFormat::MP4_MJPEG`), every frame is stored as a self-contained JPEG still image.

When writing H.264 (`VideoFormat::MP4_H264`), input frames are converted to BT.601 planar I420 and encoded with a single-threaded H.264 encoder into Annex-B NAL units, packaged into standard `avc1` / `avcC` tracks. Frame dimensions for H.264 **must be multiples of 16** (macroblock constraint).

Reading supports both one-component (grayscale) and three-component JPEG samples; a grayscale sample read into an `RGB` image is replicated over the three channels.

> **Note** — `VideoReader` decodes frames using `stb_image`, which supports JPEG images only. Files written with `VideoFormat::MP4_H264` cannot be decoded by `VideoReader`; use an external player (e.g. VLC, MPV) or standard media decoders to view them.

## `VideoWriter`

```cpp
namespace qlm
{
    enum class VideoFormat
    {
        MP4_MJPEG,  // Motion-JPEG frames in an MP4 container (default)
        MP4_H264    // H.264 video in an MP4 container (requires width & height % 16 == 0)
    };

    class VideoWriter
    {
    public:
        bool Open(const std::string& file_name, int frame_width, int frame_height, int frame_rate,
                  int quality = 90, VideoFormat format = VideoFormat::MP4_MJPEG);

        void Close();
        bool IsOpen() const;

        bool WriteFrame(const Image<ImageFormat::RGB, uint8_t>& frame);
        bool WriteFrame(const Image<ImageFormat::GRAY, uint8_t>& frame);

        int Width() const;
        int Height() const;
        int FrameRate() const;
        int Quality() const;
        int FrameCount() const;
    };
}
```

A writer owns an open file between `Open` and `Close`, so it is **movable but not copyable**.

### `Open`

| Parameter | Description |
|---|---|
| `file_name` | Path of the file to create; an existing file is truncated |
| `frame_width`, `frame_height` | Size of every frame; each side must be between 1 and 65535. For `MP4_H264`, both must also be multiples of 16 |
| `frame_rate` | Frames per second, stored in the container exactly (an integer, e.g. 24, 25, 30, 60) |
| `quality` | `1` (smallest) to `100` (best); clamped, `90` by default. For `MP4_MJPEG`, maps directly to JPEG quality. For `MP4_H264`, maps linearly to QP 51 (quality 1) down to QP 10 (quality 100) |
| `format` | Container and codec combination (`MP4_H264` by default, or `MP4_MJPEG`) |

Returns `false` and prints to `std::cerr` when the size, the frame rate or the file itself is not usable.

### `Close`

Writes the index (`moov`) and closes the file. The destructor calls it too, so a file is finished when the writer goes out of scope. Until then the file holds the frames but **no index**, which means it cannot be read back yet.

### `WriteFrame`

```cpp
qlm::VideoWriter writer;
writer.Open("movie.mp4", 320, 240, 30);

qlm::Image<qlm::ImageFormat::RGB, uint8_t> frame(320, 240);
// ... draw into frame ...

writer.WriteFrame(frame);   // one frame, one JPEG image
writer.Close();
```

| Overload | Content of the frame |
|---|---|
| `WriteFrame(const Image<RGB, uint8_t>&)` | Three-component JPEG |
| `WriteFrame(const Image<GRAY, uint8_t>&)` | Single-component (grayscale) JPEG |

**Notes**

- Every frame must have the size given to `Open`, otherwise the call fails and prints to `std::cerr`.
- One call writes exactly one frame; a video of `n` frames needs `n` calls.
- Frames are written in the order they are passed; there is no inter-frame compression and no motion estimation.
- `FrameCount()` counts the frames accepted so far.
- The file stays playable while it is being written — a frame that is fully written is a complete JPEG image — and a process that is killed leaves a file with valid frames but no index.


## `VideoReader`

```cpp
namespace qlm
{
    class VideoReader
    {
    public:
        bool LoadFromFile(const std::string& file_name);
        void Close();
        bool IsOpen() const;

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
| `LoadFromFile(file_name)` | Reads the file into memory, opens the first video track and stops at the first frame |
| `Close()` | Releases the file and the demuxer |
| `IsOpen()` | Whether a track is open |
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

Because every frame is an independent image, `Seek` and the indexed `ReadFrame` are exact: no other frame has to be decoded to reach one.

## Examples

Writing a slideshow — four images, one second each, at 30 frames per second:

```cpp
const int frame_rate = 30;
const int frames_per_image = frame_rate;   // one second of frames per image

qlm::VideoWriter writer;
writer.Open("slideshow.mp4", width, height, frame_rate, 95);

for (const auto& canvas : canvases)                 // one canvas per image
{
    for (int frame = 0; frame < frames_per_image; frame++)
        writer.WriteFrame(canvas);
}

writer.Close();     // writes the index, the file is complete now
```

Reading it back and jumping to a known time:

```cpp
qlm::VideoReader reader;
reader.LoadFromFile("slideshow.mp4");

std::cout << reader.FrameCount() << " frames, " << reader.FrameRate()
          << " fps, " << reader.Duration() << " s\n";

qlm::Image<qlm::ImageFormat::RGB, uint8_t> frame;
reader.ReadFrame(45, frame);       // frame 45, the middle of the second image at 30 fps
```

## Known limitations

- **Motion-JPEG playback by `VideoReader`.** `VideoReader` decodes frames using `stb_image`, which only supports JPEG images; it cannot decode H.264 video tracks. Use an external player (e.g. VLC, MPV) for H.264 files.
- **H.264 macroblock alignment.** Writing with `VideoFormat::MP4_H264` requires both frame width and frame height to be integer multiples of 16.
- **No audio.** An audio track in a file that is read is ignored, and none is written.
- **Every frame has one size.** Images of different sizes have to be placed on a common canvas before they are written; the size is fixed by `Open`.
- **The whole file is read into memory.** `VideoReader::LoadFromFile` keeps the container in memory, so very large videos need proportional RAM.
- **Errors are reported coarsely.** `ReadFrame` returning `false` means the end of the track or a sample that could not be decoded; the demuxer does not say which.
- **4 GB per file when writing.** The size field of the `mdat` box is 32 bits wide, so `WriteFrame` refuses the frame that would push the file past 4 GB.
- **Not thread-safe.** One instance, one thread.
- **A file is only complete after `Close`.** Copying the file before the writer is closed or destroyed gives frames without an index, which no player can open as a video.

## See also

- [Image](image.md) — loading, creating and saving the frames
- [Pixel formats](pixel_formats.md) — the channel layouts a frame can have
- [Concepts and types](concepts.md) — `pixel_t`, and why only `uint8_t` video frames are instantiated
