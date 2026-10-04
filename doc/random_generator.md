# Random generation

Three pieces work together to fill images with pseudo-random values:

| Component | Role |
|---|---|
| [`RandomGenerator<T>`](#randomgeneratort) | Owns the engine and the value range; produces channel values |
| [`RandomPixel<frmt, T>`](#randompixel) | Turns channel values into a `Pixel` of a given format |
| [`Image::RandomInit`](#imagerandominit) | Fills every visible pixel of an image |

The generator is a separate object rather than state hidden inside an image, so the same generator can be reused, copied to replay a sequence, or used directly as a standard generator functor.

Source: [`include/random_generator.hpp`](../include/random_generator.hpp) · Runnable example: [`examples/example1.cpp`](../examples/example1.cpp)

**On this page** — [`RandomGenerator<T>`](#randomgeneratort) · [`RandomPixel`](#randompixel) · [`Image::RandomInit`](#imagerandominit) · [Examples](#examples) · [Known limitations](#known-limitations)

## `RandomGenerator<T>`

A seeded generator of values of type `T`, bounded to a range. `T` must satisfy [`pixel_t`](concepts.md#concepts).

```cpp
template<pixel_t T>
class RandomGenerator
{
public:
    explicit RandomGenerator(const uint32_t seed,
                             const T min_val = std::numeric_limits<T>::lowest(),
                             const T max_val = std::numeric_limits<T>::max());

    RandomGenerator();

    void Reseed(uint32_t seed);
    void SetRange(T min_val, T max_val);
    T Min() const;
    T Max() const;
    T Next();
    T operator()();
};
```

The engine is `std::mt19937` and is not part of the public interface.

### Constructors

| Signature | Effect |
|---|---|
| `explicit RandomGenerator(uint32_t seed, T min_val = lowest, T max_val = max)` | Deterministically seeded. The default range is the **full range** of `T`. |
| `RandomGenerator()` | Seeded from `std::random_device`; non-deterministic. |

Both constructors normalize the bounds, so passing `min_val > max_val` is allowed and simply swaps them.

```cpp
qlm::RandomGenerator<uint8_t> full(42);            // 0 .. 255
qlm::RandomGenerator<uint8_t> mid(42, 100, 200);   // 100 .. 200
qlm::RandomGenerator<float> unit(42, 0.0f, 1.0f);  // 0.0 .. 1.0
qlm::RandomGenerator<uint8_t> entropy;             // random_device seeded
```

> **Note** — the seed constructor is `explicit`, so a bare seed cannot be passed where a `RandomGenerator` is expected.

### Public methods

| Method | Description |
|---|---|
| `void Reseed(uint32_t seed)` | Restarts the sequence from `seed`; the range is kept |
| `void SetRange(T min_val, T max_val)` | Replaces the range (swapping if reversed) without touching the engine state |
| `T Min() const` | Current lower bound |
| `T Max() const` | Current upper bound |
| `T Next()` | Next value in the range; advances the engine |
| `T operator()()` | Identical to `Next()`, so the generator satisfies the standard generator interface |

`Next()` is inclusive of `Min()` and `Max()` for integral types, and covers `[Min(), Max())` for floating-point types.

**Notes**

- Every call advances the internal state, so two consecutive `Next()` calls generally differ.
- For integral `T` the value is drawn with `std::uniform_int_distribution<int64_t>` and narrowed to `T` — `std::uniform_int_distribution` is only required to support `short`, `int`, `long` and `long long`, so `uint8_t` would not be portable.
- For floating-point `T` a `std::uniform_real_distribution<T>` is used.

### Determinism

| Guarantee | Scope |
|---|---|
| Same seed + same range → same sequence | Always |
| Same sequence across different standard libraries | Not guaranteed — the engine is standardized, the *distributions* are not |
| Copying a generator replays its sequence | Always — the engine state is copied |

```cpp
qlm::RandomGenerator<uint8_t> gen(1234, 0, 255);

const uint8_t first = gen.Next();
const uint8_t second = gen.Next();

qlm::RandomGenerator<uint8_t> snapshot = gen;   // copies the engine state
// snapshot.Next() == the value gen.Next() would return next

gen.Reseed(1234);                               // back to the beginning
// gen.Next() == first
```

## `RandomPixel`

Builds a single pixel by drawing one value per color channel from the generator.

```cpp
template<ImageFormat frmt, pixel_t T>
Pixel<frmt, T> RandomPixel(RandomGenerator<T>& gen, bool random_alpha = false);
```

| Parameter | Description |
|---|---|
| `gen` | Generator supplying the channel values; it is advanced by one draw per channel |
| `random_alpha` | When `true`, the alpha channel is drawn as well |

**Returns** — `Pixel<frmt, T>` with random color channels.

| Format | Channels drawn | Order of draws |
|---|---|---|
| `GRAY` | `v` | `v` |
| `RGB` | `r`, `g`, `b` | `r`, `g`, `b` |
| `HSV` | `h`, `s`, `v` | `h`, `s`, `v` |
| `HLS` | `h`, `l`, `s` | `h`, `l`, `s` |
| `YCrCb` | `y`, `cr`, `cb` | `y`, `cr`, `cb` |
| any format, `random_alpha` | plus `a` | after the color channels |

**Notes**

- By default alpha is left at `std::numeric_limits<T>::max()`, keeping the pixel fully opaque.
- The pixel starts as a default-constructed `Pixel`; the generator supplies the color channels only.
- `RandomPixel` performs **no** normalization: the hue channel of `HSV`/`HLS` receives the raw generated value, unlike the `% 360` wrap used by [pixel arithmetic](pixel.md#known-limitations). Give the generator a hue-appropriate range when filling those formats.

```cpp
qlm::RandomGenerator<uint8_t> gen(1234, 0, 255);

const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> pix =
    qlm::RandomPixel<qlm::ImageFormat::RGB, uint8_t>(gen);

const qlm::Pixel<qlm::ImageFormat::RGB, uint8_t> with_alpha =
    qlm::RandomPixel<qlm::ImageFormat::RGB, uint8_t>(gen, true);
```

## `Image::RandomInit`

Fills an existing image by calling `RandomPixel` for every visible pixel.

```cpp
void RandomInit(RandomGenerator<T>& gen, bool random_alpha = false);
void RandomInit(uint32_t seed, bool random_alpha = false);
void RandomInit();
```

| Overload | Seeding | Range | Deterministic |
|---|---|---|---|
| `RandomInit(gen, ...)` | The supplied generator | The generator's range | Yes, if `gen` was seeded explicitly |
| `RandomInit(seed, ...)` | `RandomGenerator<T>(seed)` | Full range of `T` | Yes |
| `RandomInit()` | `std::random_device` | Full range of `T` | No |

**Notes**

- The image must already be created — `Create(width, height)`, a constructor, or `Read`. Otherwise the call is a **silent no-op**.
- Only the `width` visible pixels of each row are drawn, so the number of engine draws does not depend on `stride`; with padded rows the padding keeps its previous value.
- `random_alpha = false` (the default) leaves every pixel opaque.
- Filling is deterministic for a given seed, range, `width`, `height` and `random_alpha`.

See [`RandomInit`](image.md#randominit) in the image documentation for the same material in context.

## Examples

Reproducible noise — the same seed always produces the same image:

```cpp
qlm::Image<qlm::ImageFormat::RGB, uint8_t> noise(256, 256);
noise.RandomInit(42);
noise.Write("noise.png", false);
```

A bounded range, for example a pastel tint:

```cpp
qlm::RandomGenerator<uint8_t> pastel(7, 180, 255);
qlm::Image<qlm::ImageFormat::RGB, uint8_t> img(256, 256);
img.RandomInit(pastel);
```

Floating-point images normally live in `[0, 1]`, so pass an explicit range:

```cpp
qlm::RandomGenerator<float> unit(1, 0.0f, 1.0f);
qlm::Image<qlm::ImageFormat::GRAY, float> img(128, 128);
img.RandomInit(unit);
```

Hue must stay inside a meaningful range for `HSV`/`HLS`:

```cpp
qlm::RandomGenerator<float> hue(1, 0.0f, 359.0f);
qlm::Image<qlm::ImageFormat::HSV, float> img(64, 64);
img.RandomInit(hue);          // hue is drawn raw, so the range defines the hue band
```

Sharing one generator across images keeps a single reproducible stream:

```cpp
qlm::RandomGenerator<uint8_t> gen(2026);

qlm::Image<qlm::ImageFormat::RGB, uint8_t> first(64, 64);
qlm::Image<qlm::ImageFormat::RGB, uint8_t> second(64, 64);

first.RandomInit(gen);        // draws 64 x 64 x 3 values
second.RandomInit(gen);       // continues the same stream
```

The generator can also be used as an ordinary generator functor:

```cpp
qlm::RandomGenerator<uint8_t> gen(2026);

std::vector<uint8_t> values(16);
std::generate_n(values.begin(), values.size(), gen);   // uses operator()
```

## Known limitations

- **Distribution mappings are not portable.** `std::mt19937` is fully specified, but `std::uniform_int_distribution` and `std::uniform_real_distribution` are not, so the same seed can produce different values with a different standard library (for example g++ versus MSVC). Reproducibility holds within a single toolchain.
- **The default range is the full range of `T`.** For floating-point channels that means values across the whole type range, including negatives — pass an explicit range such as `0.0f, 1.0f` for image-like data.
- **Hue is not wrapped.** `RandomPixel` draws `h` like any other channel, so `RandomInit(seed)` on an `HSV` or `HLS` image with a signed or floating-point channel type gives out-of-range hues.
- **One generator, one thread.** Concurrent use of the same `RandomGenerator` instance races; give each thread its own generator.
- **Uniform distributions only.** There is no normal, Poisson, or per-channel distribution support.

## See also

- [Image](image.md#randominit) — creating and filling images
- [Pixel](pixel.md) — how the generated channels behave in arithmetic
- [Pixel formats](pixel_formats.md) — the channel layouts being filled