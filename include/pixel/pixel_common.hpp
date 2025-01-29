#pragma once

#include <concepts>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace qlm
{
    // image formats
    enum class ImageFormat
    {
        GRAY,
        RGB,
        YCrCb,
        HSV,
        HLS
    };

    // Supported pixel types
    template <class T>
    concept pixel_t = std::same_as<T, uint8_t>  ||
                      std::same_as<T, int16_t>  ||
                      std::same_as<T, uint16_t> ||
                      std::same_as<T, int32_t>  ||
                      std::floating_point<T>;

    // Arithmetic data type
    template<typename T>
    concept arithmetic_t = std::is_arithmetic_v<T>;

    // Next larger data type
    template <typename T>
    struct wider  { using type = double; };

    template <> struct wider<uint8_t>  { using type = uint16_t; };
    template <> struct wider<int8_t>   { using type = int16_t; };
    template <> struct wider<uint16_t> { using type = uint32_t; };
    template <> struct wider<int16_t>  { using type = int32_t; };
    template <> struct wider<uint32_t> { using type = uint64_t; };
    template <> struct wider<int32_t>  { using type = int64_t; };
    template <> struct wider<float>    { using type = double; };

    template <typename T>
    using wider_t = typename wider<T>::type;

    // Make a type signed
    template<typename T>
    using signed_t = typename std::conditional<std::is_integral<T>::value, std::make_signed<T>, T>::type;

    // upgrade type
    template <pixel_t T, arithmetic_t T2>
    using cast_t =
        typename std::conditional<(std::is_floating_point_v<T2> || std::is_floating_point_v<T>), double,
        typename std::conditional<((sizeof(T) > sizeof(T2)) && (std::is_signed_v<T2> || std::is_signed_v<T>)), wider_t<signed_t<T>>,
        typename std::conditional<((sizeof(T) < sizeof(T2)) && (std::is_signed_v<T2> || std::is_signed_v<T>)), wider_t<signed_t<T2>>,
        typename std::conditional<(sizeof(T) > sizeof(T2)), wider_t<T>, wider_t<T2>
        >::type
        >::type
        >::type
    >::type;

    // Forward declaration of Pixel class
    template<ImageFormat frmt, pixel_t T>
    class Pixel;

    template<ImageFormat frmt, pixel_t T>
    void ApplyToChannels(auto&& func, Pixel<frmt, T>& result, const Pixel<frmt, T>& in1, const Pixel<frmt, T>& in2)
    {
        if constexpr (frmt == ImageFormat::GRAY)
        {
            result.v = func(in1.v, in2.v);
            result.a = func(in1.a, in2.a);
        }
        else if constexpr (frmt == ImageFormat::RGB)
        {
            result.r = func(in1.r, in2.r);
            result.g = func(in1.g, in2.g);
            result.b = func(in1.b, in2.b);
            result.a = func(in1.a, in2.a);
        }
        else if constexpr (frmt == ImageFormat::HLS)
        {
            result.h = func(in1.h, in2.h) % 360;
            result.l = func(in1.l, in2.l);
            result.s = func(in1.s, in2.s);
            result.a = func(in1.a, in2.a);
        }
        else
        {
            result.h = func(in1.h, in2.h) % 360;
            result.v = func(in1.v, in2.v);
            result.s = func(in1.s, in2.s);
            result.a = func(in1.a, in2.a);
        }
    }

     template<ImageFormat frmt, pixel_t T>
    void ApplyToChannels(auto&& func, Pixel<frmt, T>& result, const Pixel<frmt, T>& in)
    {
        if constexpr (frmt == ImageFormat::GRAY)
        {
            result.v = func(in.v);
            result.a = func(in.a);
        }
        else if constexpr (frmt == ImageFormat::RGB)
        {
            result.r = func(in.r);
            result.g = func(in.g);
            result.b = func(in.b);
            result.a = func(in.a);
        }
        else if constexpr (frmt == ImageFormat::HLS)
        {
            result.h = func(in.h) % 360;
            result.l = func(in.l);
            result.s = func(in.s);
            result.a = func(in.a);
        }
        else
        {
            result.h = func(in.h) % 360;
            result.v = func(in.v);
            result.s = func(in.s);
            result.a = func(in.a);
        }
    }

    // pixels operations
    // Pixel * num 
    template<qlm::ImageFormat frmt, qlm::pixel_t T, qlm::arithmetic_t T2>
    qlm::Pixel<frmt, T> operator*(const qlm::Pixel<frmt, T>& pix, const T2 num)
    {
        qlm::Pixel<frmt, T> result;

        auto mul_func = [num](const auto a) {
            return static_cast<T>(std::clamp<qlm::cast_t<T, T2>>(a * num, std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max()));
        };

        ApplyToChannels(mul_func, result, pix);
        return result;
    }

    // absolute difference
    template<qlm::ImageFormat frmt, qlm::pixel_t T>
    qlm::Pixel<frmt, T> AbsDiff(const qlm::Pixel<frmt, T>& in1, const qlm::Pixel<frmt, T>& in2)
    {
        qlm::Pixel<frmt, T> result;
        using type_t = qlm::wider_t<qlm::signed_t<T>>;

        auto diff_func = [](const auto a, const auto b) {
            return static_cast<T>(std::clamp<type_t>(std::abs(a - b), std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max()));
        };

        ApplyToChannels(diff_func, result, in1, in2);
        return result;
    }

    // blend pixels
    // Function to blend two colors based on the given weight (0 to 1)
    template <ImageFormat frmt, pixel_t T>
    Pixel<frmt, T> BlendColors(const Pixel<frmt, T>& color1, const Pixel<frmt, T>& color2, float weight)
    {
        weight = std::clamp(weight, 0.0f, 1.0f); // Ensure weight is valid

        return color1 * weight + color2 * (1 - weight);
    }

    // l2 norm
    template<qlm::ImageFormat frmt, qlm::pixel_t T>
    qlm::wider_t<qlm::signed_t<T>> L2Norm(const qlm::Pixel<frmt, T>& in1, const qlm::Pixel<frmt, T>& in2)
    {
        using type_t = qlm::wider_t<qlm::signed_t<T>>;

        type_t result{ 0 };

        auto diff_sq = [](auto a, auto b) 
        {
            type_t diff = static_cast<type_t>(a) - static_cast<type_t>(b);
            return diff * diff;
        };

        if constexpr (frmt == qlm::ImageFormat::GRAY)
        {
            result = diff_sq(in1.v, in2.v);
        }
        else if constexpr (frmt == qlm::ImageFormat::RGB)
        {
            result = diff_sq(in1.r, in2.r) + diff_sq(in1.g, in2.g) + diff_sq(in1.b, in2.b);
        }
        else if constexpr (frmt == qlm::ImageFormat::HLS)
        {
            result = diff_sq(in1.h, in2.h) + diff_sq(in1.l, in2.l) + diff_sq(in1.s, in2.s);
        }
        else
        {
            result = diff_sq(in1.h, in2.h) + diff_sq(in1.v, in2.v) + diff_sq(in1.s, in2.s);
        }

        return std::sqrt(result);
    }
}