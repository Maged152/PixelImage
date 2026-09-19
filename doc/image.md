# Image

`Image<frmt, T>` owns a contiguous buffer of [`Pixel<frmt, T>`](pixel.md) and provides creation, pixel access, border handling and file I/O. `frmt` is an [`ImageFormat`](concepts.md#imageformat) and `T` is a [`pixel_t`](concepts.md#concepts) channel type.

Sources: [`include/image.hpp`](../include/image.hpp) · [`source/LoadFromFile.cpp`](../source/LoadFromFile.cpp) · [`source/SaveToFile.cpp`](../source/SaveToFile.cpp)

**On this page** — [Memory model](#memory-model) · [Public variables](#public-variables) · [Constructors](#constructors) · [Creating and filling](#creating-and-filling) · [Pixel access](#pixel-access) · [File I/O](#file-io) · [`NumerOfChannels`](#numerofchannels) · [Known limitations](#known-limitations)

## Memory model

The pixels live in a single heap array of `stride * height` elements, allocated with `new Pixel<frmt, T>[stride * height]`. Because `Pixel` has a user-provided default constructor, every element starts as an opaque black pixel.

`stride` is the number of pixels per row and is never smaller than `width`. When a `stride` larger than `width` is requested, the extra pixels at the end of each row are **padding**: they are part of the allocation, but they are not addressable through the coordinate-based accessors and they are skipped when saving.

```text
stride = 6, width = 4, height = 3      padding cells are allocated but unused
+---+---+---+---+---+---+
| 0 | 1 | 2 | 3 | p | p |   row 0
+---+---+---+---+---+---+
| 0 | 1 | 2 | 3 | p | p |   row 1
+---+---+---+---+---+---+
| 0 | 1 | 2 | 3 | p | p |   row 2
+---+---+---+---+---+---+
  ^-----------^   ^---^
     width        stride - width
```

A pixel at `(x, y)` is stored at index `y * stride + x`.

> **Note** — the raw-pointer constructor takes **ownership** of the buffer. The destructor always calls `delete[] data`, so the buffer must come from `new Pixel<frmt, T>[...]` and must not be freed elsewhere.

## Public variables

| Variable | Type | Description |
|---|---|---|
| `width` | `int` | Number of visible pixels per row |
| `stride` | `int` | Pixels per row in memory (`>= width`) |
| `height` | `int` | Number of rows |

`width`, `stride` and `height` are public and directly readable; a default-constructed image has all three set to `0`.

## Constructors

| Signature | Behavior |
|---|---|
| `Image()` | Empty image: `data == nullptr`, `width = height = stride = 0` |
| `Image(int width, int height, int _stride = 0)` | Allocates the buffer; `stride` defaults to `width` |
| `Image(int width, int height, Pixel<frmt, T>* data, int _stride = 0)` | Adopts an existing buffer — **takes ownership** |
| `Image(const Image<frmt, T>& other)` | Deep copy: allocates a new buffer and copies `stride * height` pixels |
| `Image(Image<frmt, T>&& other) noexcept` | Moves the buffer and resets the source to empty |

```cpp
// Owning an image you create
qlm::Image<qlm::ImageFormat::RGB, uint8_t> a(640, 480);

// Explicit stride: rows are padded to 1024 pixels
qlm::Image<qlm::ImageFormat::RGB, uint8_t> padded(640, 480, 1024);

// Adopting a buffer: the image now owns it
qlm::Pixel<qlm::ImageFormat::GRAY, uint8_t>* buffer =
    new qlm::Pixel<qlm::ImageFormat::GRAY, uint8_t>[256 * 256];
qlm::Image<qlm::ImageFormat::GRAY, uint8_t> adopted(256, 256, buffer);
// do not `delete[] buffer` - `adopted` will
```

### Assignment

| Operator | Behavior |
|---|---|
| `Image& operator=(const Image& other)` | Self-assignment safe; releases the old buffer, then deep-copies |
| `Image& operator=(Image&& other) noexcept` | Releases the old buffer, then steals the source buffer |

## Creating and filling

### `Create`

```cpp
void Create(int img_width, int img_height, int img_stride = 0);
void Create(int img_width, int img_height, Pixel<frmt, T> pix, int img_stride = 0);
```

| Parameter | Description |
|---|---|
| `img_width`, `img_height` | New dimensions |
| `pix` | Value used to fill **every** allocated pixel, padding included |
| `img_stride` | Pixels per row; `0` (the default) means `img_width` |

**Returns** — nothing.

**Notes**

- Any previously owned buffer is released before the new one is allocated.
- Without the `pix` overload the buffer is default-constructed: every pixel is opaque black.
- The arguments are not validated.

```cpp
qlm::Image<qlm::ImageFormat::RGB, uint8_t> img;
img.Create(320, 240);                              // opaque black
img.Create(320, 240, qlm::Pixel<qlm::ImageFormat::RGB, uint8_t>(255, 0, 0), 384);   // red, padded rows
```

### `RandomInit`

```cpp
void RandomInit(RandomGenerator<T>& gen, bool random_alpha = false);
void RandomInit(uint32_t seed, bool random_alpha = false);
void RandomInit();
```

Fills the image with pixels produced by [`RandomPixel`](random_generator.md#randompixel). See [Random generation](random_generator.md) for seeding, ranges and determinism.

| Overload | Seeding | Range |
|---|---|---|
| `RandomInit(gen, ...)` | The supplied generator | The generator's range |
| `RandomInit(seed, ...)` | `RandomGenerator<T>` constructed with `seed` | Full range of `T` |
| `RandomInit()` | `std::random_device` | Full range of `T` |

**Notes**

- The image must already have been created: if `data == nullptr`, or `width`/`height` are not positive, the call returns immediately and changes nothing.
- Only the `width` visible pixels of each row are filled; with a padded stride the padding keeps its previous value.
- The alpha channel stays at `std::numeric_limits<T>::max()` (opaque) unless `random_alpha` is `true`.
- The hue channel of `HSV`/`HLS` is drawn from the generator range like any other channel — it is **not** wrapped into `[0, 360)`, so give the generator a hue-appropriate range.

```cpp
qlm::Image<qlm::ImageFormat::RGB, uint8_t> noise(256, 256);
noise.RandomInit(42);                      // reproducible: the same seed gives the same image

qlm::RandomGenerator<uint8_t> gen(7, 200, 255);   // pale, low-contrast noise
noise.RandomInit(gen);
```

## Pixel access

### `SetPixel`

```cpp
void SetPixel(int x, int y, const Pixel<frmt, T>& pix);
void SetPixel(int i, const Pixel<frmt, T>& pix);
```

| Parameter | Description |
|---|---|
| `x`, `y` | Column and row of the target pixel |
| `i` | Flat index used directly into the buffer: `data[i]` |
| `pix` | New value |

**Notes** — out-of-range coordinates are **silently ignored**; no exception is thrown and no error is reported.

> **Note** — the index overload validates `i` against `width * height`. For an image with padding that is wrong: index `width` is already a padding cell, not the first pixel of row 1.

### `GetPixel`

```cpp
Pixel<frmt, T> GetPixel(int x, int y) const;
Pixel<frmt, T> GetPixel(int i) const;
Pixel<frmt, T> GetPixel(int x, int y, const BorderMode<frmt, T>& border_mode) const;
```

| Parameter | Description |
|---|---|
| `x`, `y` | Column and row to read |
| `i` | Flat index read directly from the buffer |
| `border_mode` | How to resolve coordinates outside the image, see [`BorderMode`](concepts.md#bordermode) |

**Returns** — the pixel by value.

**Notes**

- Without `border_mode`, out-of-range reads return a default-constructed `Pixel` — opaque black, which is indistinguishable from a black pixel. Use `border_mode` or check the coordinates yourself.
- The index overload validates `i` against `width * height`, with the same padding caveat as `SetPixel`.
- The border-aware overload resolves each axis independently and falls back to the clamped or reflected pixel; only `BORDER_CONSTANT` returns `border_pixel`.

```cpp
qlm::Image<qlm::ImageFormat::RGB, uint8_t> img(64, 64);
img.SetPixel(0, 0, qlm::Pixel<qlm::ImageFormat::RGB, uint8_t>(255, 255, 0));

const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> pix = img.GetPixel(0, 0);
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> safe = img.GetPixel(-1, -1);   // opaque black

qlm::BorderMode<qlm::ImageFormat::RGB, uint8_t> border;
border.border_type = qlm::BorderType::BORDER_REFLECT;
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> reflected = img.GetPixel(-2, 0, border);
```

### `Copy`

```cpp
void Copy(const Image<frmt, T>& in);
```

Copies pixel data into this image. When both images share the same `stride`, a single `memcpy` of `stride * height` pixels is used; otherwise the copy is done row by row for `width` pixels per row.

**Notes**

- Dimensions are **not** validated. `in` must be at least as large as `*this`, otherwise the copy reads out of bounds.
- Copies the pixel data only; `width`, `height` and `stride` of the destination are unchanged.

```cpp
qlm::Image<qlm::ImageFormat::GRAY, uint8_t> src(128, 128);
qlm::Image<qlm::ImageFormat::GRAY, uint8_t> dst(128, 128);
src.RandomInit(1);
dst.Copy(src);
```

## File I/O

### `LoadFromFile`

```cpp
bool LoadFromFile(const std::string& file_name);
```

Decodes the file with stb (`stbi_load` for `uint8_t`, `stbi_load_16` for `int16_t`, `stbi_loadf` for floating-point channels), then replaces this image's buffer with the decoded data.

| Parameter | Description |
|---|---|
| `file_name` | Path to the image, including the extension |

**Returns** — `true` on success, `false` on failure (the reason is printed to `std::cerr`).

**Notes**

- On success `stride` is set to `width`, so a loaded image never has padding.
- `NumerOfChannels()` reports the channel count of the **file**, not of the format; see [`NumerOfChannels`](#numerofchannels).
- Channel compatibility is checked: `GRAY` requires at least 1 channel, `RGB` at least 3. Incompatible files fail and leave the image unchanged.
- Alpha is taken from the 4th channel for `RGB`, and from the channel after the first for `GRAY` when the file has 2 or 4 channels. Otherwise alpha is set to `std::numeric_limits<T>::max()`.
- The previous buffer is released only after a successful decode.

```cpp
qlm::Image<qlm::ImageFormat::RGB, uint8_t> img;
if (!img.LoadFromFile("photo.jpg"))
{
    return -1;   // details already printed to std::cerr
}
```

### `SaveToFile`

```cpp
bool SaveToFile(const std::string& file_name, bool alpha = true, int quality = 100);
```

| Parameter | Description |
|---|---|
| `file_name` | Output path; the extension selects the encoder |
| `alpha` | When `true`, an additional channel is written: `2` components for `GRAY`, `4` for `RGB` |
| `quality` | JPEG quality, `0`–`100`; ignored by the other encoders |

**Returns** — `true` on success, `false` if the image is invalid, the extension is unsupported, or the encoder failed.

**Notes**

- The extension is matched case-insensitively.
- `alpha = true` is the default, which writes a 2-component `GRAY` or 4-component `RGB` file. Pass `false` for a plain grayscale or RGB output.
- Each encoder is called with `final_comp = (alpha ? comp + 1 : comp)` components, where `comp` is `1` for `GRAY` and `3` for every other format. Whether the target container supports that component count is not validated.

| Extension | Encoder | Notes |
|---|---|---|
| `.png` | `stbi_write_png` | Row stride computed as `components * width * sizeof(T)` |
| `.bmp` | `stbi_write_bmp` | |
| `.jpg`, `.jpeg` | `stbi_write_jpg` | `quality` applies here only |
| `.pgm` | PGM writer (P5) | Only when exactly 1 component is written, i.e. `GRAY` with `alpha = false` |
| anything else | — | Prints an error and returns `false` |

```cpp
qlm::Image<qlm::ImageFormat::GRAY, uint8_t> img(256, 256);
img.RandomInit(7);

img.SaveToFile("noise.png");            // 2 components: gray + alpha
img.SaveToFile("noise.png", false);     // 1 component
img.SaveToFile("noise.pgm", false);     // PGM requires 1 component
img.SaveToFile("noise.jpg", false, 90); // JPEG quality 90
```

### Supported instantiations

`LoadFromFile` and `SaveToFile` are declared for every `Image<frmt, T>` but only **defined** for these four combinations. Any other combination fails at link time:

| | `uint8_t` | `int16_t` | other `T` |
|---|---|---|---|
| `GRAY` | ✅ | ✅ | ❌ |
| `RGB` | ✅ | ✅ | ❌ |
| `YCrCb`, `HSV`, `HLS` | ❌ | ❌ | ❌ |

```text
undefined reference to `qlm::Image<(qlm::ImageFormat)3, unsigned char>::SaveToFile(...)'
                      // (ImageFormat)3 == HSV
```

## `NumerOfChannels`

```cpp
int NumerOfChannels() const;
```

Returns the number of channels stored in `num_of_channels`. The value has two different origins:

| Created by | Reported value | Origin |
|---|---|---|
| Constructor, `Create`, or assignment | Format-derived: `GRAY` 2, `RGB` 4, `YCrCb` 3, `HSV`/`HLS` 4 | `SetNumChannels` |
| `LoadFromFile` | The channel count reported by the file, e.g. `3` for a JPEG loaded into an `RGB` image | `stbi_load` output |

> **Note** — the method is spelled `NumerOfChannels`, without the `b` in "Number". That is the actual API name.

> **Note** — the two meanings above are the reason the [project README](../README.md) example tests `NumerOfChannels() == 3` to decide whether a loaded image has an alpha channel.

```cpp
qlm::Image<qlm::ImageFormat::RGB, uint8_t> img(64, 64);
img.NumerOfChannels();          // 4 (RGB + alpha)

img.LoadFromFile("photo.jpg");  // a 3-channel JPEG
img.NumerOfChannels();          // 3
```

## Known limitations

- **File I/O is only instantiated for `GRAY`/`RGB` with `uint8_t`/`int16_t`.** Calling `LoadFromFile` or `SaveToFile` on any other combination compiles and then fails to link.
- **The stb writers are 8-bit oriented.** Saving an `int16_t` image writes 16-bit samples into containers that expect 8-bit ones, so the output is not meaningful. The source notes this explicitly (`// I think stb_write supports only U8 !`).
- **`Copy` does not validate dimensions** and reads out of bounds when `in` is smaller than the destination.
- **Index-based access does not understand stride.** `GetPixel(int)` and `SetPixel(int, ...)` bound-check against `width * height` and index the buffer directly, so they are only consistent for images without padding.
- **`RandomInit` fails silently** on an image that was never created.
- **The raw-pointer constructor takes ownership.** Passing a stack array, a buffer owned by a `unique_ptr`, or the same buffer to two `Image` objects leads to `delete[]` on memory the image does not own.
- **Dimensions are not validated.** Negative or zero dimensions produce an invalid allocation instead of an error.
- **No format conversion.** `HSV`, `HLS` and `YCrCb` images cannot be read from or written to files, and there is no conversion to or from `RGB`.

## See also

- [Pixel](pixel.md) · [Pixel formats](pixel_formats.md) — the pixel values stored in the buffer
- [Concepts and types](concepts.md) — `BorderMode` and `ImageFormat`
- [Random generation](random_generator.md) — `RandomInit` and `RandomGenerator` in detail