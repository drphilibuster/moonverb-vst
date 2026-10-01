#include "PluginEditor.h"

MoonVerbEditor::MoonVerbEditor(MoonVerbProcessor& p) : AudioProcessorEditor(&p), proc(p), front(p), rear(p) {
    setLookAndFeel(&laf);
    addAndMakeVisible(front); addAndMakeVisible(rear);
    const float aspect = ui::FrontPanel::W / (ui::FrontPanel::H + ui::RearPanel::H);
    constrainer.setFixedAspectRatio(aspect);
    constrainer.setMinimumSize(720, (int)(720 / aspect));
    constrainer.setMaximumSize(2400, (int)(2400 / aspect));
    setConstrainer(&constrainer);
    setResizable(true, true);
    setSize(1200, (int)(1200 / aspect));
    timerCallback();
    startTimerHz(30);
}

MoonVerbEditor::~MoonVerbEditor() { setLookAndFeel(nullptr); }

void MoonVerbEditor::resized() {
    const float s = (float)getWidth() / ui::FrontPanel::W;
    const int fh = (int)std::lround(ui::FrontPanel::H * s);
    front.setBounds(0, 0, getWidth(), fh);
    rear.setBounds(0, fh, getWidth(), getHeight() - fh);
}

void MoonVerbEditor::timerCallback() {
    const auto v = proc.view();
    front.update(v); rear.update(v);
}
