#include "SpectrumDisplay.h"
#include "PluginProcessor.h"

namespace kloudformant::gui
{

namespace
{
    constexpr int kRefreshHz = 30;

    // The envelope and the spectrum are on unrelated absolute scales -- one is a
    // frame magnitude, the other a log-magnitude envelope in dB -- so the
    // envelope curves are drawn relative to their own running level. What
    // matters visually is the *distance between* the two envelopes, which is the
    // filter, and that difference is preserved by any common offset.
    constexpr float kEnvelopeTrimDb = -6.0f;
}

SpectrumDisplay::SpectrumDisplay (KloudFormantAudioProcessor& p)
    : processorRef (p)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (kRefreshHz);
}

void SpectrumDisplay::resized()
{
    plot = getLocalBounds().toFloat().reduced (1.0f);
}

float SpectrumDisplay::frequencyToX (double hz) const noexcept
{
    const auto t = std::log (juce::jlimit (kMinHz, kMaxHz, hz) / kMinHz) / std::log (kMaxHz / kMinHz);
    return plot.getX() + (float) t * plot.getWidth();
}

float SpectrumDisplay::decibelsToY (double db) const noexcept
{
    const auto t = (kTopDb - juce::jlimit (kBottomDb, kTopDb, db)) / (kTopDb - kBottomDb);
    return plot.getY() + (float) t * plot.getHeight();
}

void SpectrumDisplay::timerCallback()
{
    processorRef.copyDisplayFrame (frame);

    if (frame.numBins <= 0)
        return;

    if (smoothedInput.size() != frame.inputDb.size())
        smoothedInput = frame.inputDb;
    else
        for (size_t k = 0; k < smoothedInput.size(); ++k)
        {
            // Rises quickly, falls slowly: the same asymmetry a spectrum
            // analyser uses, so peaks register but the display does not flicker.
            const auto target = frame.inputDb[k];
            const auto coeff = target > smoothedInput[k] ? 0.5f : 0.12f;
            smoothedInput[k] += coeff * (target - smoothedInput[k]);
        }

    buildPath (spectrumPath, smoothedInput,     frame.binWidthHz, 0.0f);
    buildPath (envelopePath, frame.envelopeDb,  frame.binWidthHz, kEnvelopeTrimDb);
    buildPath (shiftedPath,  frame.shiftedDb,   frame.binWidthHz, kEnvelopeTrimDb);
    buildBandPath (bandPath, frame.binWidthHz);

    repaint();
}

void SpectrumDisplay::buildPath (juce::Path& path, const std::vector<float>& curve,
                                 double binWidthHz, float offsetDb) const
{
    path.clear();

    if (curve.empty() || binWidthHz <= 0.0 || plot.isEmpty())
        return;

    auto started = false;
    auto lastColumn = -1;

    for (size_t k = 1; k < curve.size(); ++k)
    {
        const auto hz = (double) k * binWidthHz;

        if (hz < kMinHz)
            continue;

        if (hz > kMaxHz)
            break;

        const auto x = frequencyToX (hz);
        const auto column = (int) x;

        if (column == lastColumn)
            continue;

        lastColumn = column;

        const auto y = decibelsToY ((double) curve[k] + (double) offsetDb);

        if (! started)
        {
            path.startNewSubPath (x, y);
            started = true;
        }
        else
        {
            path.lineTo (x, y);
        }
    }
}

void SpectrumDisplay::buildBandPath (juce::Path& path, double binWidthHz) const
{
    path.clear();

    const auto& lower = frame.envelopeDb;
    const auto& upper = frame.shiftedDb;

    if (lower.empty() || upper.size() != lower.size() || binWidthHz <= 0.0 || plot.isEmpty())
        return;

    // Forward along the shifted envelope, back along the measured one, closed.
    std::vector<juce::Point<float>> forward, backward;
    auto lastColumn = -1;

    for (size_t k = 1; k < upper.size(); ++k)
    {
        const auto hz = (double) k * binWidthHz;

        if (hz < kMinHz) continue;
        if (hz > kMaxHz) break;

        const auto x = frequencyToX (hz);
        const auto column = (int) x;

        if (column == lastColumn)
            continue;

        lastColumn = column;

        forward.push_back  ({ x, decibelsToY ((double) upper[k] + kEnvelopeTrimDb) });
        backward.push_back ({ x, decibelsToY ((double) lower[k] + kEnvelopeTrimDb) });
    }

    if (forward.size() < 2)
        return;

    path.startNewSubPath (forward.front());

    for (size_t i = 1; i < forward.size(); ++i)
        path.lineTo (forward[i]);

    for (auto i = backward.size(); i-- > 0;)
        path.lineTo (backward[i]);

    path.closeSubPath();
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    g.setColour (theme::panelDeep);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), theme::corner);

    //== Grid ==================================================================
    g.setFont (theme::labelFont (9.5f));

    for (double hz : { 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const auto x = frequencyToX (hz);

        g.setColour (theme::grid);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());

        g.setColour (theme::textDim.withAlpha (0.6f));
        g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k"
                                 : juce::String (hz, 0),
                    (int) x + 3, (int) plot.getBottom() - 13, 34, 12,
                    juce::Justification::left, false);
    }

    for (double db = kTopDb - 12.0; db > kBottomDb; db -= 12.0)
    {
        g.setColour (theme::grid);
        g.drawHorizontalLine ((int) decibelsToY (db), plot.getX(), plot.getRight());
    }

    //== Curves ================================================================
    g.setColour (theme::spectrum);
    g.strokePath (spectrumPath, juce::PathStrokeType (1.0f));

    // The filled band between the two envelopes *is* the correction filter.
    // Where they touch, nothing is being done.
    g.setColour (theme::accentSoft);
    g.fillPath (bandPath);

    g.setColour (theme::envelope);
    g.strokePath (envelopePath, juce::PathStrokeType (1.6f));

    g.setColour (theme::accent);
    g.strokePath (shiftedPath, juce::PathStrokeType (2.0f));

    //== Readout ===============================================================
    // Pitch and voicing, because both explain what the plugin is doing at any
    // instant: an unvoiced frame is one where the shift has deliberately backed
    // off, and without this that reads as the plugin failing to work.
    const auto voiced = frame.voicedness > 0.5;

    g.setFont (theme::labelFont (10.0f));
    g.setColour (voiced ? theme::voiced : theme::textDim);

    const auto pitch = voiced && frame.f0Hz > 0.0
                     ? juce::String (frame.f0Hz, 1) + " Hz"
                     : juce::String ("unvoiced");

    g.drawText (pitch, plot.reduced (6.0f, 4.0f), juce::Justification::topRight, false);

    g.setColour (theme::outline);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), theme::corner, 1.0f);
}

} // namespace kloudformant::gui
