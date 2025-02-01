#pragma once
#include "pixel_common.hpp"

namespace qlm
{
    // Specialization for YCrCb format
    template<pixel_t T>
    class Pixel<ImageFormat::YCrCb, T>
    {
    public:
        T y, cr, cb, a;

    private:
        const T min_value = std::numeric_limits<T>::lowest();
        const T max_value = std::numeric_limits<T>::max();

    public:
        Pixel() : y(0), cr(0), cb(0), a(max_value) {}
        Pixel(T luminance, T chromaR, T chromaB) : y(luminance), cr(chromaR), cb(chromaB), a(max_value) {}
        Pixel(T luminance, T chromaR, T chromaB, T alpha) : y(luminance), cr(chromaR), cb(chromaB), a(alpha) {}
        
        // Copy constructor
        Pixel(const Pixel& other) : y(other.y), cr(other.cr), cb(other.cb), a(other.a) {}
        // Move constructor
        Pixel(Pixel&& other) noexcept : y(std::move(other.y)), cr(std::move(other.cr)), cb(std::move(other.cb)), a(std::move(other.a)) {}
    
    public:
        // Assignment operator
        Pixel& operator=(const Pixel& other)
        {
            if (this != &other)
            {
                y = other.y;
                cr = other.cr;
                cb = other.cb;
                a = other.a;
            }
            return *this;
        }
        // Move operator
        Pixel& operator=(Pixel&& other) noexcept
        {
            if (this != &other)
            {
                y = std::move(other.y);
                cr = std::move(other.cr);
                cb = std::move(other.cb);
                a = std::move(other.a);
            }
            return *this;
        }
       
        // Cast operator
        template<qlm::pixel_t T2>
        operator Pixel<qlm::ImageFormat::YCrCb, T2>() const
        {
            Pixel<qlm::ImageFormat::YCrCb, T2> res;

            T2 min_val = std::numeric_limits<T2>::lowest();
            T2 max_val = std::numeric_limits<T2>::max();

            res.y = static_cast<T2>(std::clamp<qlm::cast_t<T, T2>>(y, min_val, max_val));
            res.cr = static_cast<T2>(std::clamp<qlm::cast_t<T, T2>>(cr, min_val, max_val));
            res.cb = static_cast<T2>(std::clamp<qlm::cast_t<T, T2>>(cb, min_val, max_val));
            res.a = static_cast<T2>(std::clamp<qlm::cast_t<T, T2>>(a, min_val, max_val));

            return res;
        }
        
    public:
        void Set(T Y, T Cr, T Cb, T alpha = max_value)
        {
            y = Y;
            cr = Cr;
            cb = Cb;
            a = alpha;
        }

        void Set(T val, T alpha = max_value)
        {
            y = val;
            cr = val;
            cb = val;
            a = alpha;
        }

        template<arithmetic_t T2>
        void MAC(const Pixel& other, const T2 coeff)
        {
            y = static_cast<T>(std::clamp<qlm::cast_t<T, T2>>(y + other.y * coeff, min_value, max_value));
            cr = static_cast<T>(std::clamp<qlm::cast_t<T, T2>>(cr + other.cr * coeff, min_value, max_value));
            cb = static_cast<T>(std::clamp<qlm::cast_t<T, T2>>(cb + other.cb * coeff, min_value, max_value));
            a = static_cast<T>(std::clamp<qlm::cast_t<T, T2>>(a + other.a * coeff, min_value, max_value));
        }

        uint64_t SquaredEuclideanDistance(const Pixel& other)
        {
            int64_t diff_y = (int64_t)y - (int64_t)other.y;
            int64_t diff_cr = (int64_t)cr - (int64_t)other.cr;
            int64_t diff_cb = (int64_t)cb - (int64_t)other.cb;

            return static_cast<uint64_t>(diff_y * diff_y + diff_cr * diff_cr + diff_cb * diff_cb);

        }
    };
}