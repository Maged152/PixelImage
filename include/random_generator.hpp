#pragma once

#include <random>
#include <limits>
#include <algorithm>
#include <cstdint>
#include <type_traits>
#include "random_generator.hpp"
#include <cmath>
#include "pixel/pixel_common.hpp"

namespace qlm
{
    // Generates pseudo-random values of type T, seeded by the user and bounded to a value range.
    template<pixel_t T>
    class RandomGenerator
    {
    private:
        using random_engine_t = std::mt19937;

        random_engine_t engine;
        T min_value;
        T max_value;

    public:
        // Deterministically seeded generator; the range defaults to the full range of T
        explicit RandomGenerator(const uint32_t seed,
                                 const T min_val = std::numeric_limits<T>::lowest(),
                                 const T max_val = std::numeric_limits<T>::max())
            : engine(seed),
              min_value(std::min(min_val, max_val)),
              max_value(std::max(min_val, max_val))
        {}

        // Non-deterministically seeded generator (std::random_device), full range of T
        RandomGenerator() : RandomGenerator(static_cast<uint32_t>(std::random_device{}()))
        {}

        // Re-seed the generator; the same seed and range reproduce the same sequence
        void Reseed(uint32_t seed)
        {
            engine.seed(seed);
        }

        // Set the value range (the arguments are swapped if min_val > max_val)
        void SetRange(T min_val, T max_val)
        {
            min_value = std::min(min_val, max_val);
            max_value = std::max(min_val, max_val);
        }

        T Min() const
        {
            return min_value;
        }

        T Max() const
        {
            return max_value;
        }

        // Generate the next random value within the range
        T Next()
        {
            if constexpr (std::is_floating_point_v<T>)
            {
                std::uniform_real_distribution<T> dist(min_value, max_value);
                return dist(engine);
            }
            else
            {
                // std::uniform_int_distribution is only required to support short/int/long/long long
                // (and their unsigned counterparts), so bridge through int64_t
                std::uniform_int_distribution<int64_t> dist(static_cast<int64_t>(min_value),
                                                            static_cast<int64_t>(max_value));
                return static_cast<T>(dist(engine));
            }
        }

        // Standard generator functor interface (usable with std::generate and friends)
        T operator()()
        {
            return Next();
        }
    };

    // Create a pixel whose color channels are random values taken from gen.
    // The alpha channel stays opaque unless random_alpha is true.
    template<ImageFormat frmt, pixel_t T>
    Pixel<frmt, T> RandomPixel(RandomGenerator<T>& gen, bool random_alpha = false)
    {
        Pixel<frmt, T> pix{};

        if constexpr (frmt == ImageFormat::GRAY)
        {
            pix.v = gen.Next();
        }
        else if constexpr (frmt == ImageFormat::RGB)
        {
            pix.r = gen.Next();
            pix.g = gen.Next();
            pix.b = gen.Next();
        }
        else if constexpr (frmt == ImageFormat::HLS)
        {
            pix.h = gen.Next();
            pix.l = gen.Next();
            pix.s = gen.Next();
        }
        else if constexpr (frmt == ImageFormat::HSV)
        {
            pix.h = gen.Next();
            pix.s = gen.Next();
            pix.v = gen.Next();
        }
        else // YCrCb
        {
            pix.y = gen.Next();
            pix.cr = gen.Next();
            pix.cb = gen.Next();
        }

        if (random_alpha)
            pix.a = gen.Next();

        return pix;
    }
}
