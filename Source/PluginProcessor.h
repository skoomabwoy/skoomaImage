/*
    skoomaImage - VST3 Stereo Image Meter
    License: GPL-3.0
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

class SkoomaImageProcessor : public juce::AudioProcessor
{
public:
    SkoomaImageProcessor();
    ~SkoomaImageProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    std::atomic<bool> darkMode{true};

    static constexpr int kRingSize = 32768;
    static constexpr uint32_t kRingMask = kRingSize - 1;
    float ringL[kRingSize]{};
    float ringR[kRingSize]{};
    std::atomic<uint32_t> ringWrite{0};

    std::atomic<float> correlation{0.0f};
    std::atomic<float> minCorrelation{2.0f};   // 2.0 == "no data since last transport start"

private:
    double corrLL{0.0}, corrRR{0.0}, corrLR{0.0};
    float  corrAlpha{0.0f};
    bool   wasPlaying{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkoomaImageProcessor)
};
