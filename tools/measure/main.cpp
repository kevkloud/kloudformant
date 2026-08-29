/** Offline measurement harness.

    Drives the real DSP core with no host and no framework involved, and prints
    the numbers the README quotes. Every claim about this plugin should be
    reproducible by someone who has only a compiler.

        measure null        transparency at zero shift
        measure formants    where the formants actually land
        measure artefacts   what the process adds that was not there
        measure profile     all of the above
*/

#include "dsp/DspCore.h"
#include "dsp/Fft.h"
#include "dsp/SpectralEnvelope.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

using namespace kloudformant;

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kSampleRate = 48000.0;

    double toDb (double magnitude)
    {
        return 20.0 * std::log10 (std::max (magnitude, 1.0e-30));
    }

    struct Vowel
    {
        double f0 = 120.0;
        std::vector<double> formantHz { 700.0, 1220.0, 2600.0 };
        std::vector<double> bandwidthHz { 80.0, 100.0, 140.0 };
    };

    std::vector<float> renderVowel (const Vowel& v, int numSamples)
    {
        std::vector<float> x ((size_t) numSamples, 0.0f);
        const auto numHarmonics = (int) (0.45 * kSampleRate / v.f0);

        for (int h = 1; h <= numHarmonics; ++h)
        {
            const auto amplitude = 1.0 / (double) h;
            const auto w = 2.0 * kPi * v.f0 * (double) h / kSampleRate;

            for (int n = 0; n < numSamples; ++n)
                x[(size_t) n] += (float) (amplitude * std::sin (w * (double) n));
        }

        for (size_t f = 0; f < v.formantHz.size(); ++f)
        {
            const auto r = std::exp (-kPi * v.bandwidthHz[f] / kSampleRate);
            const auto theta = 2.0 * kPi * v.formantHz[f] / kSampleRate;
            const auto a1 = -2.0 * r * std::cos (theta);
            const auto a2 = r * r;
            const auto gain = 1.0 + a1 + a2;

            double z1 = 0.0, z2 = 0.0;

            for (int n = 0; n < numSamples; ++n)
            {
                const auto out = gain * (double) x[(size_t) n] - a1 * z1 - a2 * z2;
                z2 = z1;
                z1 = out;
                x[(size_t) n] = (float) out;
            }
        }

        auto peak = 0.0f;

        for (auto s : x)
            peak = std::max (peak, std::abs (s));

        if (peak > 0.0f)
            for (auto& s : x)
                s *= 0.5f / peak;

        return x;
    }

    std::vector<float> run (const std::vector<float>& input, const DspCore::Params& params,
                            DspCore& core)
    {
        constexpr int blockSize = 128;

        core.prepare (kSampleRate, blockSize, 1);
        core.setParams (params);

        auto output = input;

        for (int n = 0; n < (int) output.size(); n += blockSize)
        {
            const auto count = std::min (blockSize, (int) output.size() - n);
            float* channels[] { output.data() + n };
            core.process (channels, 1, count);
        }

        return output;
    }

    std::complex<double> dftAt (const std::vector<float>& x, double hz, int from)
    {
        const auto to = (int) x.size();
        const auto length = (double) (to - from);
        const auto w = -2.0 * kPi * hz / kSampleRate;
        const std::complex<double> step { std::cos (w), std::sin (w) };
        std::complex<double> rot { 1.0, 0.0 }, acc { 0.0, 0.0 };

        for (int n = from; n < to; ++n)
        {
            const auto window = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) (n - from) / length);
            acc += window * (double) x[(size_t) n] * rot;
            rot *= step;
        }

        return acc * (2.0 / length);
    }

    /** Envelope peak nearest an expected frequency, averaged over many frames. */
    double formantAt (const std::vector<float>& signal, double expectedHz)
    {
        constexpr int fftSize = 8192, window = 4096;

        Fft fft;
        SpectralEnvelope estimator;
        fft.prepare (fftSize);
        estimator.prepare (fftSize, kSampleRate);

        std::vector<std::complex<double>> spectrum ((size_t) fftSize);
        std::vector<float> envelope ((size_t) (fftSize / 2 + 1)), frame ((size_t) window);
        std::vector<double> accumulated ((size_t) (fftSize / 2 + 1), 0.0);

        for (int offset = 32000; offset + window < (int) signal.size(); offset += window / 2)
        {
            for (int i = 0; i < window; ++i)
                frame[(size_t) i] = signal[(size_t) (offset + i)]
                                  * (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / window));

            fft.forwardReal (frame.data(), window, spectrum.data());
            estimator.analyse (spectrum.data(), envelope.data(), 0.55);

            for (size_t k = 0; k < accumulated.size(); ++k)
                accumulated[k] += envelope[k];
        }

        const auto binHz = kSampleRate / (double) fftSize;
        const auto centre = (int) std::lround (expectedHz / binHz);
        const auto lo = std::max (1, (int) (expectedHz * 0.65 / binHz));
        const auto hi = std::min ((int) accumulated.size() - 2, (int) (expectedHz * 1.55 / binHz));

        const auto isLocalMax = [&] (int k)
        {
            return accumulated[(size_t) k] >= accumulated[(size_t) (k - 1)]
                && accumulated[(size_t) k] >= accumulated[(size_t) (k + 1)];
        };

        int best = centre;

        for (int radius = 0; radius <= hi - lo; ++radius)
        {
            if (centre - radius >= lo && isLocalMax (centre - radius)) { best = centre - radius; break; }
            if (centre + radius <= hi && isLocalMax (centre + radius)) { best = centre + radius; break; }
        }

        auto position = (double) best;

        if (best > lo && best < hi)
        {
            const auto a = accumulated[(size_t) (best - 1)];
            const auto b = accumulated[(size_t) best];
            const auto c = accumulated[(size_t) (best + 1)];
            const auto denom = a - 2.0 * b + c;

            if (std::abs (denom) > 1.0e-12)
                position += 0.5 * (a - c) / denom;
        }

        return position * binHz;
    }
}

//==============================================================================
static void measureNull()
{
    std::cout << "\nTransparency at zero shift\n"
              << "--------------------------\n";

    const auto input = renderVowel ({}, 96000);

    for (auto tracking : { Tracking::fast, Tracking::normal, Tracking::smooth })
    {
        DspCore::Params params;
        params.tracking = tracking;

        DspCore core;
        const auto output = run (input, params, core);

        const auto latency = core.getLatencySamples();
        double worst = 0.0;
        auto identical = true;

        for (int n = core.getWindow() * 2; n + latency < (int) input.size(); ++n)
        {
            const auto expected = input[(size_t) n];
            const auto actual = output[(size_t) (n + latency)];

            identical = identical && actual == expected;
            worst = std::max (worst, std::abs ((double) actual - (double) expected));
        }

        const char* names[] { "Fast", "Normal", "Smooth" };

        std::cout << "  " << std::setw (7) << std::left << names[(int) tracking]
                  << " window " << std::setw (5) << core.getWindow()
                  << " latency " << std::setw (5) << latency
                  << " (" << std::fixed << std::setprecision (1)
                  << 1000.0 * latency / kSampleRate << " ms)   "
                  << (identical ? "bit-identical to the delayed input"
                                : "worst error " + std::to_string (toDb (worst / 0.5)) + " dB")
                  << '\n';
    }
}

static void measureFormants()
{
    std::cout << "\nFormant placement\n"
              << "-----------------\n";

    Vowel v;
    v.f0 = 110.0;
    v.formantHz = { 600.0, 1400.0, 2500.0 };
    v.bandwidthHz = { 80.0, 100.0, 140.0 };

    const auto input = renderVowel (v, 144000);

    std::vector<double> reference;

    for (auto formant : v.formantHz)
        reference.push_back (formantAt (input, formant));

    std::cout << "  source formants measured at ";

    for (auto hz : reference)
        std::cout << (int) hz << " ";

    std::cout << "Hz\n";

    for (float semitones : { -7.0f, -5.0f, -3.0f, -1.0f, 1.0f, 3.0f, 5.0f, 7.0f })
    {
        const auto ratio = std::pow (2.0, (double) semitones / 12.0);

        DspCore::Params params;
        params.shiftSemitones = semitones;
        params.highPivotHz = 20000.0;

        DspCore core;
        const auto output = run (input, params, core);

        std::cout << "  " << std::showpos << std::setw (3) << (int) semitones << std::noshowpos
                  << " st  ";

        for (size_t f = 0; f < reference.size(); ++f)
        {
            const auto measured = formantAt (output, reference[f] * ratio);
            const auto error = 100.0 * (measured / reference[f] - ratio) / ratio;

            std::ostringstream cell;
            cell << "F" << (f + 1) << " " << (int) measured << " Hz ("
                 << std::showpos << std::fixed << std::setprecision (1) << error << " %)";

            std::cout << std::setw (24) << std::left << cell.str();
        }

        std::cout << '\n';
    }
}

static void measureArtefacts()
{
    std::cout << "\nWhat the process adds\n"
              << "---------------------\n";

    Vowel v;
    v.f0 = 150.0;
    const auto input = renderVowel (v, 144000);

    std::cout << "   shift   inter-harmonic   sidebands   level\n";

    for (float semitones : { -12.0f, -7.0f, -3.0f, 0.0f, 3.0f, 7.0f, 12.0f })
    {
        DspCore::Params params;
        params.shiftSemitones = semitones;

        DspCore core;
        const auto output = run (input, params, core);

        const auto from = core.getWindow() * 3;
        const auto frameRate = kSampleRate / (double) (core.getWindow() / 4);

        double onHarmonic = 0.0, between = 0.0;

        for (int h = 2; h <= 8; ++h)
        {
            onHarmonic = std::max (onHarmonic, std::abs (dftAt (output, v.f0 * h, from)));
            between    = std::max (between,    std::abs (dftAt (output, v.f0 * (h + 0.5), from)));
        }

        const auto carrier = std::abs (dftAt (output, v.f0 * 3.0, from));
        double sideband = 0.0;

        for (double offset : { -2.0, -1.0, 1.0, 2.0 })
        {
            const auto hz = v.f0 * 3.0 + offset * frameRate;

            if (hz > 20.0 && hz < kSampleRate * 0.45)
                sideband = std::max (sideband, std::abs (dftAt (output, hz, from)));
        }

        const auto rms = [&] (const std::vector<float>& x)
        {
            double sum = 0.0;

            for (int n = from; n < (int) x.size(); ++n)
                sum += (double) x[(size_t) n] * (double) x[(size_t) n];

            return std::sqrt (sum / (double) ((int) x.size() - from));
        };

        const auto field = [] (double db)
        {
            std::ostringstream out;
            out << std::fixed << std::setprecision (1) << db << " dB";
            return out.str();
        };

        std::cout << "  " << std::showpos << std::setw (3) << (int) semitones << std::noshowpos
                  << " st" << std::setw (14) << std::right
                  << field (toDb (between / std::max (onHarmonic, 1.0e-30)))
                  << std::setw (13) << field (toDb (sideband / std::max (carrier, 1.0e-30)))
                  << std::setw (11) << field (toDb (rms (output) / rms (input))) << '\n';
    }
}

//==============================================================================
int main (int argc, char** argv)
{
    const std::string what = argc > 1 ? argv[1] : "profile";

    if (what == "null" || what == "profile")      measureNull();
    if (what == "formants" || what == "profile")  measureFormants();
    if (what == "artefacts" || what == "profile") measureArtefacts();

    if (what != "null" && what != "formants" && what != "artefacts" && what != "profile")
    {
        std::cerr << "usage: measure [null|formants|artefacts|profile]\n";
        return 1;
    }

    std::cout << '\n';
    return 0;
}
