/*
    skoomaImage - VST3 Stereo Image Meter
    License: GPL-3.0
*/

#include "PluginEditor.h"
#include "BinaryData.h"

namespace {

struct Theme {
    juce::Colour background;
    juce::Colour frameDim, frameBright;
    juce::Colour dot;
    juce::Colour labelText;
    juce::Colour toggleIcon;
};

const Theme darkTheme = {
    juce::Colour(0xff1a1a2e),
    juce::Colour(0xff444444),
    juce::Colour(0xff888888),
    juce::Colour(0xffcccccc),
    juce::Colour(0xffaaaaaa),
    juce::Colour(0xff999999),
};

const Theme lightTheme = {
    juce::Colour(0xfff2f2f7),
    juce::Colour(0xffcccccc),
    juce::Colour(0xff555555),
    juce::Colour(0xff333333),
    juce::Colour(0xff666666),
    juce::Colour(0xff777777),
};

void drawIcon(juce::Graphics& g, const juce::Drawable* src,
              juce::Rectangle<float> rect, juce::Colour colour)
{
    if (src == nullptr) return;
    auto d = src->createCopy();
    d->replaceColour(juce::Colours::black, colour);
    d->drawWithin(g, rect, juce::RectanglePlacement::centred, 1.0f);
}

} // anonymous namespace

SkoomaImageEditor::SkoomaImageEditor(SkoomaImageProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    auto typeface = juce::Typeface::createSystemTypefaceFor(
        BinaryData::JetBrainsMonoBold_ttf,
        BinaryData::JetBrainsMonoBold_ttfSize);
    monoFont = juce::Font(juce::FontOptions(typeface));

    iconTheme = juce::Drawable::createFromImageData(BinaryData::theme_svg, BinaryData::theme_svgSize);

    constrainer.setFixedAspectRatio(1.0);
    constrainer.setMinimumSize(200, 200);
    constrainer.setMaximumSize(800, 800);
    setConstrainer(&constrainer);
    setResizable(true, true);
    setSize(300, 300);

    startTimerHz(30);
}

SkoomaImageEditor::~SkoomaImageEditor()
{
    stopTimer();
}

void SkoomaImageEditor::timerCallback()
{
    const float target = processor.correlation.load(std::memory_order_acquire);
    corrSmoothed += 0.25f * (target - corrSmoothed);

    // Min tracker snaps instantly downward (it's a peak-hold, not a meter).
    const float minTarget = processor.minCorrelation.load(std::memory_order_acquire);
    minCorrSmoothed = minTarget;

    repaint();
}

void SkoomaImageEditor::paint(juce::Graphics& g)
{
    const bool dark = processor.darkMode.load();
    const auto& t = dark ? darkTheme : lightTheme;

    float w = static_cast<float>(getWidth());
    float scale = w / 300.0f;

    g.fillAll(t.background);

    // Upper-half-circle frame. Signal source (dot) sits on the diameter's
    // centre; hard-L is 9 o'clock, hard-R is 3 o'clock, mono is 12 o'clock.
    // cy is aligned with the Y of the endpoint labels in Tuner/Loud
    // (cy_sib + 0.65·r_sib·sin 135° ≈ 0.655·w) so the meter "centres" read
    // the same across the family.
    float cx = w * 0.5f;
    float cy = w * 0.655f;
    float radius = w * 0.42f;

    // Half-circle frame. Flip `#if 0` → `#if 1` to bring it back.
#if 0
    juce::Path arc;
    arc.addCentredArc(cx, cy, radius, radius, 0.0f,
                      -juce::MathConstants<float>::halfPi,
                       juce::MathConstants<float>::halfPi,
                      true);
    g.setColour(t.frameDim);
    g.strokePath(arc, juce::PathStrokeType(1.5f * scale));
    g.drawLine(cx - radius, cy, cx + radius, cy, 1.0f * scale);
#endif

    // Scatter of recent L/R sample pairs as vectorscope dots. Angle is
    // doubled from the standard goniometer so hard-L maps to 9 o'clock,
    // mono to 12 o'clock, hard-R to 3 o'clock (the half-disc's horizon).
    // Phosphor-style persistence: age bands fade exponentially, so the
    // most recent samples stand out while ~340 ms of tail remains visible.
    const uint32_t writeIdx = processor.ringWrite.load(std::memory_order_acquire);
    constexpr int kPersistSamples = 16384;
    constexpr int kBands = 32;
    constexpr int kBandSize = kPersistSamples / kBands;
    const float rNorm = radius * 0.70710678f;  // 1/√2 — max mag=√2 → radius
    const float dotR  = 1.0f * scale;
    const float dotD  = dotR * 2.0f;

    for (int b = kBands - 1; b >= 0; --b)
    {
        const float ageNorm = static_cast<float>(b) / static_cast<float>(kBands - 1);
        const float alpha   = 0.55f * std::exp(-ageNorm * 3.5f);
        g.setColour(t.dot.withAlpha(alpha));

        const int baseI = b * kBandSize;
        for (int i = baseI; i < baseI + kBandSize; ++i)
        {
            const uint32_t idx = (writeIdx - 1u - static_cast<uint32_t>(i)) & SkoomaImageProcessor::kRingMask;
            const float L = processor.ringL[idx];
            const float R = processor.ringR[idx];
            const float mag2 = L * L + R * R;
            if (mag2 < 1.0e-8f) continue;
            const float theta2 = 2.0f * std::atan2(R - L, L + R);
            const float rDisp  = std::sqrt(mag2) * rNorm;
            const float dx = rDisp * std::sin(theta2);
            const float dy = rDisp * std::cos(theta2);
            if (dy < 0.0f) continue;  // clip below the horizon (anti-phase)
            g.fillEllipse(cx + dx - dotR, cy - dy - dotR, dotD, dotD);
        }
    }

    // Correlation readout, color-coded by mono-compatibility risk:
    //   < 0     → red   (phase cancellation, sum-to-mono will hurt)
    //   0…0.3   → amber (very wide, narrow-risk)
    //   ≥ 0.3   → green (safe)
    auto corrColourFor = [](float c) {
        if (c < 0.0f)      return juce::Colour(0xffe74c3c);
        if (c < 0.3f)      return juce::Colour(0xffd6b240);
        return juce::Colour(0xff4caf50);
    };

    // Unified text band across Tuner/Loud/Image: main at 0.76w, secondary at 0.89w.
    const float mainH  = 34.0f * scale;
    const float mainY  = 0.76f * w;
    const float labelH = 14.0f * scale;
    const float labelY = 0.89f * w;

    // Main readout hidden while input is silent — matches Tuner (no freq) /
    // Loud (−∞ LUFS). The "low" peak-hold below persists after playback.
    if (processor.hasSignal.load(std::memory_order_acquire))
    {
        g.setFont(monoFont.withHeight(mainH));
        g.setColour(corrColourFor(corrSmoothed));
        g.drawText(juce::String::formatted("%+.1f", corrSmoothed),
                   juce::Rectangle<float>(0, mainY, w, mainH),
                   juce::Justification::centred, false);
    }

    // Peak-hold "low" secondary readout — hidden until real data arrives.
    if (minCorrSmoothed <= 1.0f)
    {
        g.setFont(monoFont.withHeight(labelH));
        g.setColour(corrColourFor(minCorrSmoothed).withAlpha(0.75f));
        g.drawText(juce::String::formatted("%+.1f low", minCorrSmoothed),
                   juce::Rectangle<float>(0, labelY, w, labelH),
                   juce::Justification::centred, false);
    }

    // Theme toggle (top-right, hover-only background)
    float iconSize = 33.0f * scale;
    float iconPad  = 8.0f * scale;
    juce::Rectangle<float> themeRect(w - iconSize - iconPad, iconPad, iconSize, iconSize);

    if (isMouseOver(false)) {
        auto mp = getMouseXYRelative().toFloat();
        if (themeRect.contains(mp)) {
            g.setColour(t.toggleIcon.withAlpha(0.15f));
            g.fillRoundedRectangle(themeRect, 3.0f * scale);
        }
    }
    drawIcon(g, iconTheme.get(), themeRect.reduced(iconSize * 0.2f), t.toggleIcon);
}

void SkoomaImageEditor::resized()
{
}

void SkoomaImageEditor::mouseDown(const juce::MouseEvent& e)
{
    float w = static_cast<float>(getWidth());
    float scale = w / 300.0f;
    float iconSize = 33.0f * scale;
    float iconPad  = 8.0f * scale;

    juce::Rectangle<float> themeRect(w - iconSize - iconPad, iconPad, iconSize, iconSize);
    if (themeRect.contains(e.position))
    {
        processor.darkMode.store(!processor.darkMode.load());
        repaint();
    }
}
