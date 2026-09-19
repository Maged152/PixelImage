# Concepts and types

The vocabulary types used across the library: image formats, border handling, and the concepts and type traits that constrain and convert channel types.

Source: [`include/pixel/pixel_common.hpp`](../include/pixel/pixel_common.hpp)

**On this page** — [`ImageFormat`](#imageformat) · [`BorderType`](#bordertype) · [`BorderMode`](#bordermode) · [Concepts](#concepts) · [Type traits](#type-traits)

## `ImageFormat`

The pixel layout of an image. Selected as the first template argument of both `Pixel` and `Image`, and fixed for the lifetime of those objects.

```cpp
enum class ImageFormat
{
    GRAY,
    RGB,
    YCrCb,
    HSV,
    HLS
};
```

| Value | Meaning | Channels | Alpha | `NumerOfChannels()` |
|---|---|---|---|---|
| `GRAY` | Grayscale | `v` | yes | 2 |
| `RGB` | Red, green, blue | `r`, `g`, `b` | yes | 4 |
| `YCrCb` | Luminance + chroma | `y`, `cr`, `cb` | stored in `a`, not counted | 3 |
| `HSV` | Hue, saturation, value | `h`, `s`, `v` | yes | 4 |
| `HLS` | Hue, lightness, saturation | `h`, `l`, `s` | yes | 4 |

The channel counts above are what `Image::SetNumChannels` assigns when an image is created or loaded; see [`NumerOfChannels()`](image.md#numerofchannels) for the exception after `LoadFromFile`.

> **Note** — `YCrCb` pixels do declare an `a` member, but it is deliberately excluded from the channel count, since no YCrCb alpha is defined by the format.

Hue is expressed in **degrees**. The channel-wise operations wrap it with `% 360`; see [known limitations](pixel.md#known-limitations).

## `BorderType`

`BorderType` is used by [`GetPixel(x, y, border_mode)`](image.md#getpixel) to resolve coordinates outside the image.

```cpp
enum class BorderType
{
    BORDER_CONSTANT,
    BORDER_REPLICATE,
    BORDER_REFLECT,
};
```

| Value | Behavior for an out-of-range `x` or `y` | Result for `x = -2` on a 3-wide row |
|---|---|---|
| `BORDER_CONSTANT` | Returns `BorderMode::border_pixel` unchanged | the border pixel |
| `BORDER_REPLICATE` | Clamps the index into `[0, size - 1]` | pixel at `x = 0` |
| `BORDER_REFLECT` | Mirrors the index back inside the row/column | pixel at `x = 1` |

`BORDER_REFLECT` also reflects indices past the far edge: for `x >= width` the index becomes `width - (x - width) - 1`. Reflection duplicates the edge sample, so a 3-wide row extends as `2 1 0 | 0 1 2 | 2 1`.

## `BorderMode`

Bundles a `BorderType` with the pixel value used by `BORDER_CONSTANT`, so border handling can be passed to `GetPixel` in one argument.

```cpp
template<ImageFormat frmt, pixel_t T>
struct BorderMode
{
    BorderType border_type = BorderType::BORDER_CONSTANT;
    Pixel<frmt, T> border_pixel{};
};
```

| Member | Type | Default | Description |
|---|---|---|---|
| `border_type` | `BorderType` | `BORDER_CONSTANT` | How out-of-range coordinates are resolved |
| `border_pixel` | `Pixel<frmt, T>` | default pixel (opaque black) | Value returned for `BORDER_CONSTANT` |

The `border_pixel` type must match the image's `frmt` and `T`, since `GetPixel` takes a `BorderMode<frmt, T>`.

```cpp
qlm::Image<qlm::ImageFormat::RGB, uint8_t> img(64, 64);

qlm::BorderMode<qlm::ImageFormat::RGB, uint8_t> border;
border.border_type = qlm::BorderType::BORDER_REPLICATE;

const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> pix = img.GetPixel(-5, 100, border);
// resolved to the nearest in-image pixel
```

## Concepts

| Concept | Definition | Used by |
|---|---|---|
| `pixel_t<T>` | `T` is `uint8_t`, `int16_t`, `uint16_t`, `int32_t`, or a floating-point type | `Pixel`, `Image`, `RandomGenerator`, format conversions |
| `arithmetic_t<T>` | `std::is_arithmetic_v<T>` | Scalar operands such as `Pixel * 2.0f`, `MAC` coefficients |

```cpp
template <class T>
concept pixel_t = std::same_as<T, uint8_t>  ||
                  std::same_as<T, int16_t>  ||
                  std::same_as<T, uint16_t> ||
                  std::same_as<T, int32_t>  ||
                  std::floating_point<T>;

template<typename T>
concept arithmetic_t = std::is_arithmetic_v<T>;
```

> **Note** — `pixel_t` deliberately excludes `int8_t`, `uint32_t`, `int64_t`, `bool`, and `char`. A channel must be one of the listed types; for floating point the full range is accepted.

## Type traits

| Trait | Yields | Purpose |
|---|---|---|
| `wider<T>` / `wider_t<T>` | The next larger integer type, or `double` for floating point | Intermediate type that cannot overflow when two channels are combined |
| `signed_t<T>` | The signed counterpart of an integral `T`, unchanged for floating point | Intermediate type for differences |
| `cast_t<T, T2>` | A promotion type able to hold both `T` and `T2` operands | Keeps pixel-with-pixel and pixel-with-scalar arithmetic exact before clamping back to `T` |

`wider_t` mappings:

| `T` | `uint8_t` | `int8_t` | `uint16_t` | `int16_t` | `uint32_t` | `int32_t` | `float` | anything else |
|---|---|---|---|---|---|---|---|---|
| `wider_t<T>` | `uint16_t` | `int16_t` | `uint32_t` | `int32_t` | `uint64_t` | `int64_t` | `double` | `double` |

```cpp
// L2Norm accumulates in a wide signed type, then returns it
template<qlm::ImageFormat frmt, qlm::pixel_t T>
qlm::wider_t<qlm::signed_t<T>> L2Norm(const qlm::Pixel<frmt, T>& in1, const qlm::Pixel<frmt, T>& in2);
```

These traits are implementation building blocks: results are always converted back to the channel type `T` with `std::clamp`, so intermediate arithmetic never wraps around.

## See also

- [Pixel](pixel.md) — arithmetic and free functions built on these types
- [Image](image.md) — buffers of `Pixel` with stride and border handling