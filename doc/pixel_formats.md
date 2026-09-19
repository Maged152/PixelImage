# Pixel formats

The five `Pixel` specializations. Each one is declared in its own header and exposes the same foundation — default/copy/move construction, assignment, the [cast operator](pixel.md#cast-operator), `Set`, `MAC` and `SquaredEuclideanDistance` — over a different set of channel members.

| Format | Header | Channels | Alpha counted in `NumerOfChannels()` |
|---|---|---|---|
| `GRAY` | [`pixel_GRAY.hpp`](../include/pixel/pixel_GRAY.hpp) | `v`, `a` | yes |
| `RGB` | [`pixel_RGB.hpp`](../include/pixel/pixel_RGB.hpp) | `r`, `g`, `b`, `a` | yes |
| `HLS` | [`pixel_HLS.hpp`](../include/pixel/pixel_HLS.hpp) | `h`, `l`, `s`, `a` | yes |
| `HSV` | [`pixel_HSV.hpp`](../include/pixel/pixel_HSV.hpp) | `h`, `s`, `v`, `a` | yes |
| `YCrCb` | [`pixel_YCrCb.hpp`](../include/pixel/pixel_YCrCb.hpp) | `y`, `cr`, `cb`, `a` | no |

> **Note** — the library does not convert between formats. `RGB` to `HSV` (or any other format pair) has no conversion function; only the *channel type* of a pixel can be converted, via the cast operator. Convert formats by reading and writing the channel members yourself.

**On this page** — [`GRAY`](#gray) · [`RGB`](#rgb) · [`HLS`](#hls) · [`HSV`](#hsv) · [`YCrCb`](#ycrcb) · [At-a-glance differences](#at-a-glance-differences)

## `GRAY`

Grayscale with a separate alpha channel.

```cpp
template<pixel_t T>
class Pixel<ImageFormat::GRAY, T>;
```

### Channels

| Member | Meaning | Default |
|---|---|---|
| `T v` | Grayscale intensity | `0` |
| `T a` | Alpha | `std::numeric_limits<T>::max()` (opaque) |

### Constructors

| Signature | Effect |
|---|---|
| `Pixel()` | `v = 0`, `a = max` |
| `Pixel(T gray)` | `v = gray`, `a = max` |
| `Pixel(T gray, T alpha)` | `v = gray`, `a = alpha` |
| `Pixel(const Pixel& other)` | Copy |
| `Pixel(Pixel&& other) noexcept` | Move |

### Operators

| Operator | Signature |
|---|---|
| Copy assignment | `Pixel& operator=(const Pixel& other)` |
| Move assignment | `Pixel& operator=(Pixel&& other) noexcept` |
| Less than | `bool operator<(const Pixel& other) const` |
| Less than or equal | `bool operator<=(const Pixel& other) const` |
| Cast | `template<pixel_t T2> operator Pixel<ImageFormat::GRAY, T2>() const` |

No `operator==` for this format.

### `Set` overloads

| Signature | Effect |
|---|---|
| `void Set(const T gray)` | Sets `v`; leaves `a` untouched |
| `void Set(const T gray, const T alpha)` | Sets both |

### Other methods

| Signature | Notes |
|---|---|
| `template<arithmetic_t T2> void MAC(const Pixel& other, const T2 coeff)` | Both channels clamp |
| `uint64_t SquaredEuclideanDistance(const Pixel& other) const` | Compares `v` only; the only `const` variant |

```cpp
qlm::Pixel<qlm::ImageFormat::GRAY, uint8_t> gray(128);
gray.Set(200, 128);                  // intensity 200, half transparent

const qlm::Pixel<qlm::ImageFormat::GRAY, uint8_t> limit(255);
gray.MAC(limit, 0.1f);               // every channel += limit * 0.1
```

## `RGB`

Red, green and blue color with alpha.

```cpp
template<pixel_t T>
class Pixel<ImageFormat::RGB, T>;
```

### Channels

| Member | Meaning | Default |
|---|---|---|
| `T r` | Red | `0` |
| `T g` | Green | `0` |
| `T b` | Blue | `0` |
| `T a` | Alpha | `std::numeric_limits<T>::max()` (opaque) |

### Constructors

| Signature | Effect |
|---|---|
| `Pixel()` | `r = g = b = 0`, `a = max` |
| `Pixel(T red, T green, T blue)` | Color set, `a = max` |
| `Pixel(T v)` | `r = g = b = v` (gray), `a = max` |
| `Pixel(T red, T green, T blue, T alpha)` | Everything set |
| `Pixel(const Pixel& other)` | Copy |
| `Pixel(Pixel&& other) noexcept` | Move |

### Operators

| Operator | Signature |
|---|---|
| Copy assignment | `Pixel& operator=(const Pixel& other)` |
| Move assignment | `Pixel& operator=(Pixel&& other) noexcept` |
| Less than | `bool operator<(const Pixel& other) const` |
| Less than or equal | `bool operator<=(const Pixel& other) const` |
| **Equality** | `bool operator==(const Pixel& other) const` |
| Cast | `template<pixel_t T2> operator Pixel<ImageFormat::RGB, T2>() const` |

`RGB` is the only specialization with `operator==`, which makes it the natural choice for pixel-exact comparisons.

### `Set` overloads

| Signature | Effect |
|---|---|
| `void Set(T red, T green, T blue, T alpha = max_value)` | Sets the color, optional alpha |
| `void Set(T v, T alpha = max_value)` | Sets `r = g = b = v`, optional alpha |

### Other methods

| Signature | Notes |
|---|---|
| `template<arithmetic_t T2> void MAC(const Pixel& other, const T2 coeff)` | Every channel clamps |
| `uint64_t SquaredEuclideanDistance(const Pixel& other)` | Compares `r`, `g`, `b` |

```cpp
qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> white(255);
qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> green(0, 255, 0);
qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> translucent(0, 255, 0, 128);

if (white == qlm::Pixel<qlm::ImageFormat::RGB, uint8_t>(255, 255, 255))   // true
{
    white.Set(0, 0, 0);              // now black, still opaque
}
```

## `HLS`

Hue, lightness and saturation with alpha.

```cpp
template<pixel_t T>
class Pixel<ImageFormat::HLS, T>;
```

### Channels

| Member | Meaning | Default |
|---|---|---|
| `T h` | Hue in degrees | `0` |
| `T l` | Lightness | `0` |
| `T s` | Saturation | `0` |
| `T a` | Alpha | `std::numeric_limits<T>::max()` (opaque) |

### Constructors

| Signature | Effect |
|---|---|
| `Pixel()` | `h = l = s = 0`, `a = max` |
| `Pixel(T hue, T lightness, T saturation)` | Color set, `a = max` |
| `Pixel(T hue, T lightness, T saturation, T alpha)` | Everything set |
| `Pixel(const Pixel& other)` | Copy |
| `Pixel(Pixel&& other) noexcept` | Move |

### Operators

| Operator | Signature |
|---|---|
| Copy assignment | `Pixel& operator=(const Pixel& other)` |
| Move assignment | `Pixel& operator=(Pixel&& other) noexcept` |
| Cast | `template<pixel_t T2> operator Pixel<ImageFormat::HLS, T2>() const` |

No comparison operators for this format.

### `Set` overloads

| Signature | Effect |
|---|---|
| `void Set(T hue, T lim, T sat, T alpha = max_value)` | Sets `h`, `l` and `s` |
| `void Set(T val, T alpha = max_value)` | Sets `h = s = l = val` |

> **Note** — the first `Set` overload takes its arguments as **(hue, lightness, saturation)**, matching the constructor. The second argument is named `lim` in the source.

### Other methods

| Signature | Notes |
|---|---|
| `template<arithmetic_t T2> void MAC(const Pixel& other, const T2 coeff)` | `h` wraps with `% 360`; `l` and `s` clamp |
| `uint64_t SquaredEuclideanDistance(const Pixel& other)` | Compares `h`, `l`, `s` |

> **Note** — with a floating-point channel type, `MAC` and the arithmetic operators do not compile because of the `% 360` hue wrap; see [known limitations](pixel.md#known-limitations).

```cpp
qlm::Pixel<qlm::ImageFormat::HLS, uint8_t> orange(30, 128, 255);
orange.Set(30, 200, 255, 128);       // hue 30, lightness 200, saturation 255, half transparent
```

## `HSV`

Hue, saturation and value with alpha.

```cpp
template<pixel_t T>
class Pixel<ImageFormat::HSV, T>;
```

### Channels

| Member | Meaning | Default |
|---|---|---|
| `T h` | Hue in degrees | `0` |
| `T s` | Saturation | `0` |
| `T v` | Value (brightness) | `0` |
| `T a` | Alpha | `std::numeric_limits<T>::max()` (opaque) |

### Constructors

| Signature | Effect |
|---|---|
| `Pixel()` | `h = s = v = 0`, `a = max` |
| `Pixel(T hue, T saturation, T value)` | Color set, `a = max` |
| `Pixel(T hue, T saturation, T value, T alpha)` | Everything set |
| `Pixel(const Pixel& other)` | Copy |
| `Pixel(Pixel&& other) noexcept` | Move |

### Operators

| Operator | Signature |
|---|---|
| Copy assignment | `Pixel& operator=(const Pixel& other)` |
| Move assignment | `Pixel& operator=(Pixel&& other) noexcept` |
| Cast | `template<pixel_t T2> operator Pixel<ImageFormat::HSV, T2>() const` |

No comparison operators for this format.

### `Set` overloads

| Signature | Effect |
|---|---|
| `void Set(T hue, T sat, T val, T alpha = max_value)` | Sets `h`, `s` and `v` |
| `void Set(T val, T alpha = max_value)` | Sets `h = s = v = val` |

### Other methods

| Signature | Notes |
|---|---|
| `template<arithmetic_t T2> void MAC(const Pixel& other, const T2 coeff)` | `h` wraps with `% 360`; `s` and `v` clamp |
| `uint64_t SquaredEuclideanDistance(const Pixel& other)` | Compares `h`, `s`, `v` |

```cpp
qlm::Pixel<qlm::ImageFormat::HSV, uint8_t> red(0, 255, 255);
red.Set(0, 255, 128, 255);           // hue 0, saturation 255, value 128
```

## `YCrCb`

Luminance and chrominance. The only format whose alpha member is excluded from the image channel count.

```cpp
template<pixel_t T>
class Pixel<ImageFormat::YCrCb, T>;
```

### Channels

| Member | Meaning | Default |
|---|---|---|
| `T y` | Luminance | `0` |
| `T cr` | Chroma red | `0` |
| `T cb` | Chroma blue | `0` |
| `T a` | Alpha (not counted by `NumerOfChannels()`) | `std::numeric_limits<T>::max()` (opaque) |

### Constructors

| Signature | Effect |
|---|---|
| `Pixel()` | `y = cr = cb = 0`, `a = max` |
| `Pixel(T luminance, T chromaR, T chromaB)` | Color set, `a = max` |
| `Pixel(T luminance, T chromaR, T chromaB, T alpha)` | Everything set |
| `Pixel(const Pixel& other)` | Copy |
| `Pixel(Pixel&& other) noexcept` | Move |

### Operators

| Operator | Signature |
|---|---|
| Copy assignment | `Pixel& operator=(const Pixel& other)` |
| Move assignment | `Pixel& operator=(Pixel&& other) noexcept` |
| Cast | `template<pixel_t T2> operator Pixel<ImageFormat::YCrCb, T2>() const` |

No comparison operators for this format.

### `Set` overloads

| Signature | Effect |
|---|---|
| `void Set(T Y, T Cr, T Cb, T alpha = max_value)` | Sets `y`, `cr` and `cb` |
| `void Set(T val, T alpha = max_value)` | Sets `y = cr = cb = val` |

### Other methods

| Signature | Notes |
|---|---|
| `template<arithmetic_t T2> void MAC(const Pixel& other, const T2 coeff)` | Every channel clamps |
| `uint64_t SquaredEuclideanDistance(const Pixel& other)` | Compares `y`, `cr`, `cb` |

> **Note** — [`L2Norm`](pixel.md#l2norm) does not compile for this format, and neither does file I/O; see the [support matrix](README.md#support-at-a-glance).

```cpp
qlm::Pixel<qlm::ImageFormat::YCrCb, uint8_t> yuv(128, 128, 128);
yuv.Set(128, 100, 150, 255);
```

## At-a-glance differences

| Feature | `GRAY` | `RGB` | `HLS` | `HSV` | `YCrCb` |
|---|---|---|---|---|---|
| Channel members | `v, a` | `r, g, b, a` | `h, l, s, a` | `h, s, v, a` | `y, cr, cb, a` |
| Default alpha | opaque | opaque | opaque | opaque | opaque |
| `Pixel(T)` single-value constructor | ✅ | ✅ | — | — | — |
| `Set(T val, ...)` single-value overload | ✅ | ✅ | ✅ | ✅ | ✅ |
| `operator==` | — | ✅ | — | — | — |
| `operator<`, `operator<=` | ✅ | ✅ | — | — | — |
| Hue wrapped by `% 360` | — | — | ✅ | ✅ | — |
| `SquaredEuclideanDistance() const` | ✅ | — | — | — | — |
| [`L2Norm`](pixel.md#l2norm) | ✅ | ✅ | ✅ | ✅ | ❌ |
| File I/O | ✅ | ✅ | — | — | — |

## See also

- [Pixel](pixel.md) — the shared interface, free functions and operators
- [Image](image.md) — the supported `ImageFormat` / `T` combinations for file I/O