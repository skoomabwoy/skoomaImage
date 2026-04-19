/*
    skoomaImage - VST3 Stereo Image Meter
    License: GPL-3.0
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

SkoomaImageProcessor::SkoomaImageProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input",  juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool SkoomaImageProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::stereo();
}

void SkoomaImageProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    // 200 ms one-pole smoothing for the correlation accumulators.
    const double timeConstant = 0.2;
    corrAlpha = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * timeConstant)));
    corrLL = corrRR = corrLR = 0.0;
    correlation.store(0.0f, std::memory_order_release);
    minCorrelation.store(2.0f, std::memory_order_release);
    hasSignal.store(false, std::memory_order_release);
    wasPlaying = false;
}

void SkoomaImageProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Reset the peak-hold "min correlation" at transport start (rising edge of isPlaying).
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            const bool playing = pos->getIsPlaying();
            if (playing && !wasPlaying)
                minCorrelation.store(2.0f, std::memory_order_release);
            wasPlaying = playing;
        }
    }

    const int nCh = buffer.getNumChannels();
    const int nS  = buffer.getNumSamples();
    if (nCh <= 0 || nS <= 0) return;

    const float* L = buffer.getReadPointer(0);
    const float* R = nCh > 1 ? buffer.getReadPointer(1) : L;

    uint32_t w = ringWrite.load(std::memory_order_relaxed);
    const double a = corrAlpha;
    double ll = corrLL, rr = corrRR, lr = corrLR;

    for (int i = 0; i < nS; ++i)
    {
        const float l = L[i];
        const float r = R[i];
        ringL[w & kRingMask] = l;
        ringR[w & kRingMask] = r;
        ++w;

        // One-pole smoothed accumulators for Pearson correlation.
        ll += a * (static_cast<double>(l) * l - ll);
        rr += a * (static_cast<double>(r) * r - rr);
        lr += a * (static_cast<double>(l) * r - lr);
    }

    ringWrite.store(w, std::memory_order_release);
    corrLL = ll; corrRR = rr; corrLR = lr;

    const double denom = std::sqrt(ll * rr);
    const float  c = denom > 1.0e-12 ? static_cast<float>(lr / denom) : 0.0f;
    const float  cClamped = juce::jlimit(-1.0f, 1.0f, c);
    correlation.store(cClamped, std::memory_order_release);

    // Peak-hold the lowest correlation seen since transport start. Gate on
    // there being actual signal energy (avoid latching on silence/DC). The
    // same energy gate also drives the editor's "is there live audio?" check.
    const bool live = (ll > 1.0e-6 && rr > 1.0e-6);
    hasSignal.store(live, std::memory_order_release);
    if (live)
    {
        const float prevMin = minCorrelation.load(std::memory_order_relaxed);
        if (cClamped < prevMin)
            minCorrelation.store(cClamped, std::memory_order_release);
    }
}

juce::AudioProcessorEditor* SkoomaImageProcessor::createEditor()
{
    return new SkoomaImageEditor(*this);
}

void SkoomaImageProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    uint8_t dark = darkMode.load() ? 1 : 0;
    destData.append(&dark, sizeof(uint8_t));
}

void SkoomaImageProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (sizeInBytes >= 1)
        darkMode.store(static_cast<const char*>(data)[0] != 0);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SkoomaImageProcessor();
}
