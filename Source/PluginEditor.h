/*
    skoomaImage - VST3 Stereo Image Meter
    License: GPL-3.0
*/

#pragma once

#include "PluginProcessor.h"

class SkoomaImageEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit SkoomaImageEditor(SkoomaImageProcessor&);
    ~SkoomaImageEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    SkoomaImageProcessor& processor;

    juce::Font monoFont;
    std::unique_ptr<juce::Drawable> iconTheme;
    juce::ComponentBoundsConstrainer constrainer;

    float corrSmoothed{0.0f};
    float minCorrSmoothed{2.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkoomaImageEditor)
};
