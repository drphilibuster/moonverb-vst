#pragma once
#include "PluginProcessor.h"
#include "ui/FrontPanel.h"

class MoonVerbEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit MoonVerbEditor(MoonVerbProcessor&);
    ~MoonVerbEditor() override;
    void resized() override;
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black); }
private:
    void timerCallback() override;
    MoonVerbProcessor& proc;
    ui::DeviceLookAndFeel laf;
    ui::FrontPanel front;
    ui::RearPanel rear;
    juce::ComponentBoundsConstrainer constrainer;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MoonVerbEditor)
};
