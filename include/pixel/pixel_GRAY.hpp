#pragma once

#include "pixel_common.hpp"

namespace qlm
{
    // Specialization for GRAY format
    template<pixel_t T>
    class Pixel<ImageFormat::GRAY, T>
    {
    public:
        T v, a;

    private:
        static constexpr T min_value = std::numeric_limits<T>::lowest();
        static constexpr T max_value = std::numeric_limits<T>::max();

    public:
        Pixel() : v(0), a(max_value) {}
        Pixel(T gray) : v(gray), a(max_value) {}
        Pixel(T gray, T alpha) : v(gray), a(alpha) {}
        // Copy constructor
        Pixel(const Pixel& other) : v(other.v), a(other.a) {}
        // Move constructor
        Pixel(Pixel&& other) noexcept : v(std::move(other.v)), a(std::move(other.a)) {}

    public:
        // Assignment operator
        Pixel& operator=(const Pixel& other)
        {
            if (this != &other)
            {
                v = other.v;
                a = other.a;
            }
            return *this;
        }
        
        // Move operator
        Pixel& operator=(Pixel&& other) noexcept
        {
            if (this != &other)
            {
                v = std::move(other.v);
                a = std::move(other.a);
            }
            return *this;
        }

        // < comparison operator
        bool operator<(const Pixel& other) const
        {
            return (v < other.v && a < other.a);
        }
        // <= comparison operator
        bool operator<=(const Pixel& other) const
        {
            return (v <= other.v && a <= other.a);
        }
        // Cast operator
        template<qlm::pixel_t T2>
        operator Pixel<qlm::ImageFormat::GRAY, T2>() const
        {
            Pixel<qlm::ImageFormat::GRAY, T2> res;

            T2 min_val = std::numeric_limits<T2>::lowest();
            T2 max_val = std::numeric_limits<T2>::max();

            res.v = static_cast<T2>(std::clamp<qlm::cast_t<T, T2>>(v, min_val, max_val));
            res.a = static_cast<T2>(std::clamp<qlm::cast_t<T, T2>>(a, min_val, max_val));

            return res;
        }

    public:
        void Set(const T gray)
        {
            v = gray;
        }

        void Set(const T gray, const T alpha)
        {
            v = gray;
            a = alpha;
        }

        template<arithmetic_t T2>
        void MAC(const Pixel& other, const T2 coeff)
        {
            v = static_cast<T>(std::clamp<qlm::cast_t<T, T2>>(v + other.v * coeff, min_value, max_value));
            a = static_cast<T>(std::clamp<qlm::cast_t<T, T2>>(a + other.a * coeff, min_value, max_value));
        }

        uint64_t SquaredEuclideanDistance(const Pixel& other) const
        {
            int64_t diff = (int64_t)v - (int64_t)other.v;
            return static_cast<uint64_t>(diff * diff);

        }
    };
}