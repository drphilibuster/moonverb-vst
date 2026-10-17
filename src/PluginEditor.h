#pragma once
#include "PluginProcessor.h"
#include "ui/Panel.h"

class MoonVerbEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit MoonVerbEditor(MoonVerbProcessor&);
    ~MoonVerbEditor() override;
    void resized() override;
    void paint(juce::Graphics& g) override { g.fillAll(ui::BG); }
private:
    void timerCallback() override;
    MoonVerbProcessor& proc;
    ui::Look look;
    ui::Panel panel;
    juce::ComponentBoundsConstrainer constrainer;
    juce::TooltipWindow tooltips { this, 700 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MoonVerbEditor)
};
