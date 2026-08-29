#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace kloudformant
{

/** Iterative radix-2 FFT.

    Small enough to carry, and carrying it is the point: the DSP core stays free
    of JUCE, so the tests and the measurement harness build and run on any
    machine with a C++ compiler and nothing else. The plugin does not need a
    faster transform than this -- at Normal tracking it is one 4096-point
    forward and one inverse every 512 samples, which is well under a percent of
    a core at 48 kHz.

    Twiddles are precomputed per size and the bit-reversal permutation is a
    table, so process() does no trigonometry and allocates nothing.
*/
class Fft
{
public:
    void prepare (int fftSize)
    {
        if (size == fftSize)
            return;

        size = fftSize;
        const auto n = (size_t) size;

        // log2 of the size; the caller is required to pass a power of two.
        order = 0;
        while ((1 << order) < size)
            ++order;

        reversed.resize (n);

        for (size_t i = 0; i < n; ++i)
        {
            size_t r = 0;

            for (int b = 0; b < order; ++b)
                r |= ((i >> b) & 1u) << (order - 1 - b);

            reversed[i] = r;
        }

        // One twiddle per (stage, k). Stage s has a half-span of 2^s.
        twiddles.resize (n / 2);

        for (size_t k = 0; k < n / 2; ++k)
        {
            const auto theta = -2.0 * kPi * (double) k / (double) n;
            twiddles[k] = { std::cos (theta), std::sin (theta) };
        }
    }

    int getSize() const noexcept { return size; }

    /** In-place forward transform of a complex buffer of exactly getSize(). */
    void forward (std::complex<double>* data) const noexcept { run (data, false); }

    /** In-place inverse, scaled by 1/N so that inverse(forward(x)) == x. */
    void inverse (std::complex<double>* data) const noexcept
    {
        run (data, true);

        const auto scale = 1.0 / (double) size;

        for (int i = 0; i < size; ++i)
            data[i] *= scale;
    }

    /** Real input to complex spectrum. The caller's scratch must hold getSize()
        complex values; the full spectrum is written, not just the first half,
        because the envelope code wants to index it without folding. */
    void forwardReal (const float* input, int numSamples,
                      std::complex<double>* scratch) const noexcept
    {
        for (int i = 0; i < size; ++i)
            scratch[i] = { i < numSamples ? (double) input[i] : 0.0, 0.0 };

        forward (scratch);
    }

    /** As above, from a double frame. The shifter windows in double so that the
        only single-precision rounding in the whole path is the final store to
        the host's buffer, which is what puts the transparency floor at the
        -144 dB a 32-bit sample can express rather than a few dB above it. */
    void forwardReal (const double* input, int numSamples,
                      std::complex<double>* scratch) const noexcept
    {
        for (int i = 0; i < size; ++i)
            scratch[i] = { i < numSamples ? input[i] : 0.0, 0.0 };

        forward (scratch);
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    void run (std::complex<double>* data, bool conjugate) const noexcept
    {
        const auto n = (size_t) size;

        for (size_t i = 0; i < n; ++i)
            if (reversed[i] > i)
                std::swap (data[i], data[reversed[i]]);

        for (int s = 1; s <= order; ++s)
        {
            const size_t span = (size_t) 1 << s;         // butterfly width
            const size_t half = span >> 1;
            const size_t stride = n / span;              // twiddle step

            for (size_t base = 0; base < n; base += span)
            {
                for (size_t k = 0; k < half; ++k)
                {
                    auto w = twiddles[k * stride];

                    if (conjugate)
                        w = std::conj (w);

                    const auto even = data[base + k];
                    const auto odd  = w * data[base + k + half];

                    data[base + k]        = even + odd;
                    data[base + k + half] = even - odd;
                }
            }
        }
    }

    int size = 0;
    int order = 0;
    std::vector<size_t> reversed;
    std::vector<std::complex<double>> twiddles;
};

} // namespace kloudformant
