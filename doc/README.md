# PixelImage Library Documentation

## Overview
This documentation provides an overview of the public methods and variables for the `Image` and `Pixel` classes, as well as related structs, enums, concepts, and type traits. For more detailed information, refer to the source code and comments within the code.

All data types here are under the namespace `qlm`.

## Data types & concepts

### BorderType Enum
The `BorderType` enum specifies the types of border handling.

#### Values
- `BORDER_CONSTANT`: Use a constant pixel value for borders.
- `BORDER_REPLICATE`: Replicate the edge pixels for borders.
- `BORDER_REFLECT`: Reflect the edge pixels for borders.

### ImageFormat Enum
The `ImageFormat` enum specifies the supported image formats.

#### Values
- `GRAY`: Grayscale format.
- `RGB`: RGB format.
- `YCrCb`: YCrCb format.
- `HSV`: HSV format.
- `HLS`: HLS format.

### BorderMode<frmt, T> Struct
The `BorderMode` struct specifies the border handling mode for an image.

#### Public Variables
- `BorderType border_type`: The type of border handling (constant, replicate, or reflect).
- `Pixel<frmt, T> border_pixel`: The pixel value to use for constant borders.

### Concepts
- `pixel_t`: Concept for supported pixel types (`uint8_t`, `int16_t`, `uint16_t`, `int32_t`, or floating-point types).
- `arithmetic_t`: Concept for arithmetic types.

### Type Traits
- `wider_t<T>`: Type trait to get the next larger data type.
- `signed_t<T>`: Type trait to make a type signed.
- `cast_t<T, T2>`: Type trait to upgrade a type for casting.

## Pixel<frmt, T> Class
The `Pixel` class represents a single pixel in an image. It is specialized for different image formats (`GRAY`, `RGB`, `HLS`, `HSV`, `YCrCb`).

### Common Functions
- `Arithmetic operators`: `+`, `-`, `*` and `/`
- `AbsDiff`: Computes the absolute difference between two pixels.
- `BlendColors`: Blends two colors based on a weight.
- `L2Norm`: Computes the L2 norm (Euclidean distance) between two pixels.

### Pixel<qlm::ImageFormat::GRAY, T> (GRAY Format)
The `Pixel` class template specialization for the `GRAY` format.

#### Public Variables
- `T v`: The grayscale value.
- `T a`: The alpha (transparency) value.

#### Constructors
- `Pixel()`: Default constructor. Initializes `v` to 0 and `a` to the maximum value.
- `Pixel(T gray)`: Initializes `v` to `gray` and `a` to the maximum value.
- `Pixel(T gray, T alpha)`: Initializes `v` to `gray` and `a` to `alpha`.
- `Pixel(const Pixel& other)`: Copy constructor.
- `Pixel(Pixel&& other) noexcept`: Move constructor.

#### Operators
- `Pixel& operator=(const Pixel& other)`: Copy assignment operator.
- `Pixel& operator=(Pixel&& other) noexcept`: Move assignment operator.
- `bool operator<(const Pixel& other) const`: Less-than comparison operator.
- `bool operator<=(const Pixel& other) const`: Less-than-or-equal comparison operator.
- `template<qlm::pixel_t T2> operator Pixel<qlm::ImageFormat::GRAY, T2>() const`: Cast operator to convert to another pixel type.

#### Public Methods
- `void Set(const T gray)`: Sets the grayscale value.
- `void Set(const T gray, const T alpha)`: Sets the grayscale and alpha values.
- `template<arithmetic_t T2> void MAC(const Pixel& other, const T2 coeff)`: Multiply-accumulate operation.
- `uint64_t SquaredEuclideanDistance(const Pixel& other) const`: Computes the squared Euclidean distance to another pixel.

### Pixel<qlm::ImageFormat::RGB, T> (RGB Format)
The `Pixel` class template specialization for the `RGB` format.

#### Public Variables
- `T r`: The red value.
- `T g`: The green value.
- `T b`: The blue value.
- `T a`: The alpha (transparency) value.

#### Constructors
- `Pixel()`: Default constructor. Initializes `r`, `g`, and `b` to 0 and `a` to the maximum value.
- `Pixel(T red, T green, T blue)`: Initializes `r` to `red`, `g` to `green`, `b` to `blue`, and `a` to the maximum value.
- `Pixel(T v)`: Initializes `r`, `g`, and `b` to `v` and `a` to the maximum value.
- `Pixel(T red, T green, T blue, T alpha)`: Initializes `r` to `red`, `g` to `green`, `b` to `blue`, and `a` to `alpha`.
- `Pixel(const Pixel& other)`: Copy constructor.
- `Pixel(Pixel&& other) noexcept`: Move constructor.

#### Operators
- `Pixel& operator=(const Pixel& other)`: Copy assignment operator.
- `Pixel& operator=(Pixel&& other) noexcept`: Move assignment operator.
- `bool operator<(const Pixel& other) const`: Less-than comparison operator.
- `bool operator<=(const Pixel& other) const`: Less-than-or-equal comparison operator.
- `bool operator==(const Pixel& other) const`: Equality comparison operator.
- `template<qlm::pixel_t T2> operator Pixel<qlm::ImageFormat::RGB, T2>() const`: Cast operator to convert to another pixel type.

#### Public Methods
- `void Set(T red, T green, T blue, T alpha = max_value)`: Sets the red, green, blue, and alpha values.
- `void Set(T v, T alpha = max_value)`: Sets the red, green, and blue values to `v` and the alpha value.
- `template<arithmetic_t T2> void MAC(const Pixel& other, const T2 coeff)`: Multiply-accumulate operation.
- `uint64_t SquaredEuclideanDistance(const Pixel& other)`: Computes the squared Euclidean distance to another pixel.

### Pixel<qlm::ImageFormat::HLS, T> (HLS Format)
The `Pixel` class template specialization for the `HLS` format.

#### Public Variables
- `T h`: The hue value.
- `T l`: The lightness value.
- `T s`: The saturation value.
- `T a`: The alpha (transparency) value.

#### Constructors
- `Pixel()`: Default constructor. Initializes `h`, `l`, and `s` to 0 and `a` to the maximum value.
- `Pixel(T hue, T lightness, T saturation)`: Initializes `h` to `hue`, `l` to `lightness`, `s` to `saturation`, and `a` to the maximum value.
- `Pixel(T hue, T lightness, T saturation, T alpha)`: Initializes `h` to `hue`, `l` to `lightness`, `s` to `saturation`, and `a` to `alpha`.
- `Pixel(const Pixel& other)`: Copy constructor.
- `Pixel(Pixel&& other) noexcept`: Move constructor.

#### Operators
- `Pixel& operator=(const Pixel& other)`: Copy assignment operator.
- `Pixel& operator=(Pixel&& other) noexcept`: Move assignment operator.

#### Public Methods
- `void Set(T hue, T lightness, T saturation, T alpha = max_value)`: Sets the hue, lightness, saturation, and alpha values.
- `void Set(T val, T alpha = max_value)`: Sets the hue, lightness, and saturation values to `val` and the alpha value.
- `template<arithmetic_t T2> void MAC(const Pixel& other, const T2 coeff)`: Multiply-accumulate operation.
- `uint64_t SquaredEuclideanDistance(const Pixel& other)`: Computes the squared Euclidean distance to another pixel.

### Pixel<qlm::ImageFormat::HSV, T> (HSV Format)
The `Pixel` class template specialization for the `HSV` format.

#### Public Variables
- `T h`: The hue value.
- `T s`: The saturation value.
- `T v`: The value (brightness) value.
- `T a`: The alpha (transparency) value.

#### Constructors
- `Pixel()`: Default constructor. Initializes `h`, `s`, and `v` to 0 and `a` to the maximum value.
- `Pixel(T hue, T saturation, T value)`: Initializes `h` to `hue`, `s` to `saturation`, `v` to `value`, and `a` to the maximum value.
- `Pixel(T hue, T saturation, T value, T alpha)`: Initializes `h` to `hue`, `s` to `saturation`, `v` to `value`, and `a` to `alpha`.
- `Pixel(const Pixel& other)`: Copy constructor.
- `Pixel(Pixel&& other) noexcept`: Move constructor.

#### Operators
- `Pixel& operator=(const Pixel& other)`: Copy assignment operator.
- `Pixel& operator=(Pixel&& other) noexcept`: Move assignment operator.

#### Public Methods
- `void Set(T hue, T saturation, T value, T alpha = max_value)`: Sets the hue, saturation, value, and alpha values.
- `void Set(T val, T alpha = max_value)`: Sets the hue, saturation, and value to `val` and the alpha value.
- `template<arithmetic_t T2> void MAC(const Pixel& other, const T2 coeff)`: Multiply-accumulate operation.
- `uint64_t SquaredEuclideanDistance(const Pixel& other)`: Computes the squared Euclidean distance to another pixel.

### Pixel<qlm::ImageFormat::YCrCb, T> (YCrCb Format)
The `Pixel` class template specialization for the `YCrCb` format.

#### Public Variables
- `T y`: The luminance value.
- `T cr`: The chroma red value.
- `T cb`: The chroma blue value.
- `T a`: The alpha (transparency) value.

#### Constructors
- `Pixel()`: Default constructor. Initializes `y`, `cr`, and `cb` to 0 and `a` to the maximum value.
- `Pixel(T luminance, T chromaR, T chromaB)`: Initializes `y` to `luminance`, `cr` to `chromaR`, `cb` to `chromaB`, and `a` to the maximum value.
- `Pixel(T luminance, T chromaR, T chromaB, T alpha)`: Initializes `y` to `luminance`, `cr` to `chromaR`, `cb` to `chromaB`, and `a` to `alpha`.
- `Pixel(const Pixel& other)`: Copy constructor.
- `Pixel(Pixel&& other) noexcept`: Move constructor.

#### Operators
- `Pixel& operator=(const Pixel& other)`: Copy assignment operator.
- `Pixel& operator=(Pixel&& other) noexcept`: Move assignment operator.

#### Public Methods
- `void Set(T luminance, T chromaR, T chromaB, T alpha = max_value)`: Sets the luminance, chroma red, chroma blue, and alpha values.
- `void Set(T val, T alpha = max_value)`: Sets the luminance, chroma red, and chroma blue values to `val` and the alpha value.
- `template<arithmetic_t T2> void MAC(const Pixel& other, const T2 coeff)`: Multiply-accumulate operation.
- `uint64_t SquaredEuclideanDistance(const Pixel& other)`: Computes the squared Euclidean distance to another pixel.

## Image<frmt, T> Class
The `Image` class represents an image which is a 2d array of `Pixel` and provides methods for loading, saving, and manipulating image data.

### Public Variables
- `int width`: The width of the image.
- `int stride`: The stride (number of pixels per row) of the image.
- `int height`: The height of the image.

### Constructors
- `Image()`: Default constructor. Initializes `width`, `height`, and `stride` to 0 and `data` to `nullptr`.
- `Image(int width, int height, int _stride = 0)`: Initializes the image with the specified width, height, and stride.
- `Image(int width, int height, Pixel<frmt, T>* data, int _stride = 0)`: Initializes the image with the specified width, height, data, and stride.
- `Image(const Image<frmt, T>& other)`: Copy constructor.
- `Image(Image<frmt, T>&& other) noexcept`: Move constructor.

### Operators
- `Image<frmt, T>& operator=(const Image<frmt, T>& other)`: Copy assignment operator.
- `Image<frmt, T>& operator=(Image<frmt, T>&& other) noexcept`: Move assignment operator.

### Public Methods
- `void create(int img_width, int img_height, int img_stride = 0)`: Creates an image with the specified width, height, and stride.
- `void create(int img_width, int img_height, Pixel<frmt, T> pix, int img_stride = 0)`: Creates an image with the specified width, height, pixel value, and stride.
- `void SetPixel(int x, int y, const Pixel<frmt, T>& pix)`: Sets the pixel at the specified (x, y) coordinates.
- `void SetPixel(int i, const Pixel<frmt, T>& pix)`: Sets the pixel at the specified index.
- `Pixel<frmt, T> GetPixel(int x, int y) const`: Gets the pixel at the specified (x, y) coordinates.
- `Pixel<frmt, T> GetPixel(int i) const`: Gets the pixel at the specified index.
- `void Copy(const Image<frmt, T>& in)`: Copies the data from another image.
- `Pixel<frmt, T> GetPixel(int x, int y, const BorderMode<frmt, T>& border_mode) const`: Gets the pixel at the specified (x, y) coordinates with border handling.
- `bool LoadFromFile(const std::string& file_name)`: Loads an image from a file.
- `bool SaveToFile(const std::string& file_name, bool alpha = true, int quality = 100)`: Saves the image to a file.
- `int NumerOfChannels() const`: Returns the number of channels in the image.