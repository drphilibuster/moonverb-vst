#include "PluginEditor.h"

MoonVerbEditor::MoonVerbEditor(MoonVerbProcessor& p) : AudioProcessorEditor(&p), proc(p), panel(p) {
    setLookAndFeel(&look);
    addAndMakeVisible(panel);
    const float aspect = ui::Panel::W / ui::Panel::H;
    constrainer.setFixedAspectRatio(aspect);
    constrainer.setMinimumSize(840, (int)(840 / aspect));
    constrainer.setMaximumSize(2400, (int)(2400 / aspect));
    setConstrainer(&constrainer);
    setResizable(true, true);
    setSize((int)ui::Panel::W, (int)ui::Panel::H);
    timerCallback();
    startTimerHz(30);
}

MoonVerbEditor::~MoonVerbEditor() { setLookAndFeel(nullptr); }

void MoonVerbEditor::resized() { panel.setBounds(getLocalBounds()); }

void MoonVerbEditor::timerCallback() { panel.update(proc.view()); }
