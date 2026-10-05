# Pixel

`Pixel<frmt, T>` is the color value of a single image point. `frmt` is an [`ImageFormat`](concepts.md#imageformat) selecting the channel layout; `T` is the channel type, constrained by [`pixel_t`](concepts.md#concepts).

The class has one specialization per format, so the set of channel members, constructors, and `Set` overloads differs between formats. This page documents the interface they all share and the free functions that operate on pixels; the exact per-format API is in [Pixel formats](pixel_formats.md).

Source: [`include/pixel/pixel_common.hpp`](../include/pixel/pixel_common.hpp) · Per-format specializations: [`pixel_GRAY.hpp`](../include/pixel/pixel_GRAY.hpp) · [`pixel_RGB.hpp`](../include/pixel/pixel_RGB.hpp) · [`pixel_HLS.hpp`](../include/pixel/pixel_HLS.hpp) · [`pixel_HSV.hpp`](../include/pixel/pixel_HSV.hpp) · [`pixel_YCrCb.hpp`](../include/pixel/pixel_YCrCb.hpp)

**On this page** — [Common interface](#common-interface) · [Comparison operators](#comparison-operators) · [Cast operator](#cast-operator) · [Free functions](#free-functions) · [Arithmetic operators](#arithmetic-operators) · [Known limitations](#known-limitations)

## Common interface

Every specialization provides:

| Member | Description |
|---|---|
| Default constructor | Zeroes the color channels and sets alpha to `std::numeric_limits<T>::max()`, i.e. **opaque** |
| Copy constructor | Copies every channel |
| Move constructor | `noexcept` move of every channel |
| `operator=` (copy / move) | Self-assignment safe, assigns every channel |
| Cast operator | Converts to a `Pixel` of the same format with a different channel type, **clamping** to the target range |
| `void Set(...)` | Assigns channel values; overloads differ per format |
| `void MAC(const Pixel& other, const T2 coeff)` | Multiply-accumulate: adds `other * coeff` to the pixel in place |
| `uint64_t SquaredEuclideanDistance(const Pixel& other)` | Sum of squared differences over the color channels |

### `MAC`

```cpp
template<arithmetic_t T2>
void MAC(const Pixel& other, const T2 coeff);
```

Adds `other * coeff` to the pixel in place. Each channel is computed in a promoted type and clamped back into `T`; the hue channel of `HSV` and `HLS` is wrapped with `% 360` instead of clamped.

```cpp
qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> acc(10, 10, 10);
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> step(100, 100, 100);

acc.MAC(step, 0.5f);   // (60, 60, 60)
```

### `SquaredEuclideanDistance`

```cpp
uint64_t SquaredEuclideanDistance(const Pixel& other) const;   // GRAY
uint64_t SquaredEuclideanDistance(const Pixel& other);         // other formats
```

Returns the sum of squared channel differences as `uint64_t`. Only the color channels take part — the alpha channel is **ignored**. Differences are computed in `int64_t`, so the result cannot overflow for the supported channel types.

| Format | Channels compared |
|---|---|
| `GRAY` | `v` |
| `RGB` | `r`, `g`, `b` |
| `HSV` | `h`, `s`, `v` |
| `HLS` | `h`, `l`, `s` |
| `YCrCb` | `y`, `cr`, `cb` |

> **Note** — the method is `const` only on the `GRAY` specialization.

## Comparison operators

| Operator | `GRAY` | `RGB` | `HLS` | `HSV` | `YCrCb` |
|---|---|---|---|---|---|
| `operator<` | ✅ | ✅ | — | — | — |
| `operator<=` | ✅ | ✅ | — | — | — |
| `operator==` | — | ✅ | — | — | — |

```cpp
bool operator< (const Pixel& other) const;
bool operator<=(const Pixel& other) const;
bool operator==(const Pixel& other) const;   // RGB only
```

> **Note** — `operator<` and `operator<=` are **component-wise conjunctions** (`r < other.r && g < other.g && ...`), not a lexicographic ordering. They are not a strict weak ordering, so pixels cannot be sorted with them. `operator==` compares every channel, including alpha.

## Cast operator

```cpp
template<qlm::pixel_t T2>
operator Pixel<frmt, T2>() const;
```

Converts the pixel to another channel type of the same format. Every channel is computed in [`cast_t<T, T2>`](concepts.md#type-traits) and then **clamped** into the range of `T2` — an implicit conversion that must be requested explicitly:

```cpp
qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> pix8(200, 100, 50);
qlm::Pixel<qlm::ImageFormat::RGB, float> pixf = pix8;          // ok: implicit cast operator
qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> back = pixf;        // ok

// Clamping, not scaling:
qlm::Pixel<qlm::ImageFormat::RGB, float> dark(0.4f, 0.4f, 0.4f);
qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> quantized = dark;   // (0, 0, 0)
```

## Free functions

### `ApplyToChannels`

Applies a callable to every channel of a pixel, so custom per-channel operations do not need format-specific code.

```cpp
// Combine two pixels channel by channel; the result has the channel type of in1
template<ImageFormat frmt, pixel_t T, pixel_t T2>
Pixel<frmt, T> ApplyToChannels(auto&& func, const Pixel<frmt, T>& in1, const Pixel<frmt, T2>& in2);

// Transform one pixel channel by channel
template<ImageFormat frmt, pixel_t T>
Pixel<frmt, T> ApplyToChannels(auto&& func, const Pixel<frmt, T>& in);
```

| Parameter | Description |
|---|---|
| `func` | Callable invoked as `func(channel_of_in1, channel_of_in2)` or `func(channel)` |
| `in1`, `in2` | Source pixels; `in2` may use a different channel type in the two-pixel overload |

**Returns** — `Pixel<frmt, T>`, the format of `in1`.

**Notes**

- The alpha channel is passed to `func` like any other channel.
- Hue is wrapped with `% 360` (see [known limitations](#known-limitations)).
- `func` should return the channel type of `in1`; no clamping is applied for you.

**Example**

```cpp
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> pix(10, 20, 30);
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> other_pix(200, 30, 100);

// Halve every channel - note this also halves alpha
const auto half = [](const auto c) { return static_cast<uint8_t>(c / 2); };
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> dimmed = qlm::ApplyToChannels(half, pix);

// Take the brightest channel of two pixels
const auto max_of = [](const auto a, const auto b) { return std::max(a, b); };
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> brighter =
    qlm::ApplyToChannels(max_of, pix, other_pix);
```

### `AbsDiff`

```cpp
template<qlm::ImageFormat frmt, qlm::pixel_t T>
qlm::Pixel<frmt, T> AbsDiff(const qlm::Pixel<frmt, T>& in1, const qlm::Pixel<frmt, T>& in2);
```

Absolute difference of every channel, computed in `wider_t<signed_t<T>>` so that a subtraction cannot wrap around, then clamped into `T`.

```cpp
const qlm::Pixel<qlm::ImageFormat::GRAY, uint8_t> a(200);
const qlm::Pixel<qlm::ImageFormat::GRAY, uint8_t> b(50);
const qlm::Pixel<qlm::ImageFormat::GRAY, uint8_t> diff = qlm::AbsDiff(a, b);   // 150
```

### `BlendColors`

```cpp
template <ImageFormat frmt, pixel_t T>
Pixel<frmt, T> BlendColors(const Pixel<frmt, T>& color1, const Pixel<frmt, T>& color2, float weight);
```

Linear interpolation between two colors: `color1 * weight + color2 * (1 - weight)`.

| Parameter | Description |
|---|---|
| `color1` | Weighted with `weight` |
| `color2` | Weighted with `1 - weight` |
| `weight` | Blend factor, clamped into `[0, 1]` |

**Returns** — `Pixel<frmt, T>`: `color1` when `weight == 1`, `color2` when `weight == 0`.

**Notes** — each interpolation step is quantized back to `T`, so integer channels accumulate rounding.

```cpp
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> black(0, 0, 0);
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> white(255, 255, 255);

const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> gray =
    qlm::BlendColors(white, black, 0.5f);   // (128, 128, 128)
```

### `L2Norm`

```cpp
template<qlm::ImageFormat frmt, qlm::pixel_t T>
qlm::wider_t<qlm::signed_t<T>> L2Norm(const qlm::Pixel<frmt, T>& in1, const qlm::Pixel<frmt, T>& in2);
```

Euclidean distance between two pixels: the square root of the sum of squared channel differences, accumulated in a wide signed type.

| Format | Channels included |
|---|---|
| `GRAY` | `v` |
| `RGB` | `r`, `g`, `b` |
| `HSV` | `h`, `s`, `v` |
| `HLS` | `h`, `l`, `s` |
| `YCrCb` | ❌ does not compile |

**Notes** — the alpha channel is ignored, which makes this a color-only distance.

```cpp
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> a(0, 0, 0);
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> b(3, 4, 0);
const auto distance = qlm::L2Norm(a, b);   // 5
```

## Arithmetic operators

```cpp
// pixel with pixel (the right operand may use a different channel type)
template<qlm::ImageFormat frmt, qlm::pixel_t T, qlm::pixel_t T2>
qlm::Pixel<frmt, T> operator+(const qlm::Pixel<frmt, T>& in1, const qlm::Pixel<frmt, T2>& in2);
// ... and the same shape for operator-, operator* and operator/

// pixel with scalar
template<qlm::ImageFormat frmt, qlm::pixel_t T, qlm::arithmetic_t T2>
qlm::Pixel<frmt, T> operator*(const qlm::Pixel<frmt, T>& pix, const T2 num);
// ... and the same shape for operator+ and operator/
```

| Left | Right | Exists |
|---|---|---|
| `Pixel<frmt, T>` | `Pixel<frmt, T2>` | `+`, `-`, `*`, `/` |
| `Pixel<frmt, T>` | arithmetic `T2` | `+`, `*`, `/` |
| arithmetic `T2` | `Pixel<frmt, T>` | ❌ not defined |

> **Note** — there is no `Pixel - scalar` operator; subtract a pixel or apply a negative scale instead.

**Notes**

- Every operation is implemented on top of [`ApplyToChannels`](#applytochannels), so **alpha participates** just like a color channel.
- Each channel is computed in a promoted type and clamped into `[std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max()]`. For unsigned `T` the lower bound is `0`, so underflow saturates instead of wrapping.
- The hue channel of `HSV` and `HLS` is wrapped with `% 360`.
- Dividing by zero is undefined for integral channels; guard the divisor.

```cpp
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> pix(10, 20, 30);

const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> brighter = pix + 50;      // (60, 70, 80)
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> halved   = pix / 2;       // (5, 10, 15)

const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> dark(100);
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> saturates = dark - pix;   // (90, 80, 70)
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> clamped   = pix - dark;   // (0, 0, 0)

// Mixing channel types: 16-bit operand, 8-bit result
const qlm::Pixel<qlm::ImageFormat::RGB, int16_t> wide(1000, 1000, 1000);
const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> mixed = pix + wide;       // clamped to (255, 255, 255)
```

## Known limitations

- **Floating-point hue channels do not compile for channel-wise operations.** `ApplyToChannels` wraps hue with `func(h) % 360`, and `%` is ill-formed for floating-point operands, so arithmetic operators, `AbsDiff`, `BlendColors` and `MAC` on `Pixel<HSV, float>` / `Pixel<HLS, float>` fail to compile:
  ```
  error: invalid operands of types 'float' and 'int' to binary 'operator%'
  ```
  Use an integer channel type such as `uint8_t` for `HSV`/`HLS` images, or perform the math directly on the channel members.
- **`L2Norm` does not compile for `YCrCb`** — it reads `h`, `v` and `s`, which that specialization does not have. Chain `AbsDiff` and `SquaredEuclideanDistance` instead.
- **Distance functions ignore alpha.** `SquaredEuclideanDistance` and `L2Norm` compare color channels only.
- **`operator<` / `operator<=` are component-wise conjunctions**, not a lexicographic ordering; they cannot be used to sort pixels. `operator==` exists for `RGB` only.
- **The cast operator clamps instead of scaling.** Converting `float` `0.4f` to `uint8_t` yields `0`, not `102`; normalize explicitly when converting between ranges.
- **Alpha is a full participant.** Constructors, `ApplyToChannels`, `AbsDiff` and the arithmetic operators all treat `a` like any other channel — a "halve the pixel" operation will also halve its opacity.

## See also

- [Pixel formats](pixel_formats.md) — channel members, constructors and `Set` overloads per format
- [Image](image.md) — storing and accessing pixels in a buffer
- [Random generation](random_generator.md) — `RandomPixel` builds a pixel from a `RandomGenerator`