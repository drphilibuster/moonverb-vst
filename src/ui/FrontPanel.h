// The front panel, drawn the way the unit's own is laid out: a five-segment level display, the input level knob, the 16-digit display, the soft knob, ROW up/down keys,
// the ten digit keys, PGM, REG, LOAD and BYPASS keys with their lamps, the power switch. Beneath it, the rear panel's switches and the things a plugin needs that a
// rack unit does not (where the firmware images are).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"

namespace ui {

extern const juce::Colour PANEL, PANEL_HI, INK, INK_DIM, VFD, VFD_DIM, LAMP_ON, LAMP_RED, KEY_TOP;

juce::Font mono(float h, bool bold = true);
juce::Font sans(float h, bool bold = false);

class DeviceLookAndFeel : public juce::LookAndFeel_V4 {
public:
    DeviceLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return sans((float)h * 0.55f, true); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return sans(13.f, true); }
};

/** A key of the front panel: the matrix bit is down while the mouse is, with a minimum so a quick click is still a press. */
class PanelKey : public juce::Component {
public:
    PanelKey(MoonVerbProcessor& p, moonverb::Keys::Key k, const juce::String& legend, bool arrow = false) : proc(p), key(k), text(legend), isArrow(arrow) {}
    void setLamp(bool on, bool red = false) { if (lamp != on || lampRed != red) { lamp = on; lampRed = red; repaint(); } }
    bool hasLamp = false;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
private:
    MoonVerbProcessor& proc; moonverb::Keys::Key key; juce::String text; bool isArrow, down = false, lamp = false, lampRed = false; juce::uint32 downAt = 0;
};

/** The soft knob: it has no end stops. Each turn is the firmware's own edit of the parameter the display is showing. */
class SoftKnob : public juce::Component {
public:
    explicit SoftKnob(MoonVerbProcessor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent& e) override { lastY = e.position.y; acc = 0; }
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override;
private:
    void turn(int detents);
    MoonVerbProcessor& proc; float lastY = 0, acc = 0, angle = 0.4f;
};

class Readout : public juce::Component {
public:
    void set(const MoonVerbProcessor::View& v) { view = v; repaint(); }
    void paint(juce::Graphics&) override;
private:
    MoonVerbProcessor::View view;
};

class LevelLeds : public juce::Component {
public:
    void set(int n, bool live) { if (leds != n || on != live) { leds = n; on = live; repaint(); } }
    void paint(juce::Graphics&) override;
private:
    int leds = 0; bool on = false;
};

class PowerSwitch : public juce::Component {
public:
    explicit PowerSwitch(MoonVerbProcessor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { proc.setPower(!proc.powered()); repaint(); }
private:
    MoonVerbProcessor& proc;
};

class FrontPanel : public juce::Component {
public:
    explicit FrontPanel(MoonVerbProcessor&);
    void update(const MoonVerbProcessor::View&);
    void paint(juce::Graphics&) override;
    void resized() override;
    static constexpr float W = 1200.f, H = 112.f;       // the layout's own coordinates; everything scales from the editor's width
private:
    MoonVerbProcessor& proc;
    juce::Slider input; juce::SliderParameterAttachment inputAttach;
    SoftKnob soft; Readout readout; LevelLeds leds; PowerSwitch power;
    std::unique_ptr<PanelKey> keys[16];
    enum { UP, DOWN, PGM, REG, LOAD, BYP, DIGIT0 };
    juce::Rectangle<float> r(float x, float y, float w, float h) const;
    float s() const { return (float)getWidth() / W; }
};

class RearPanel : public juce::Component {
public:
    explicit RearPanel(MoonVerbProcessor&);
    void update(const MoonVerbProcessor::View&);
    void paint(juce::Graphics&) override;
    void resized() override;
    static constexpr float W = 1200.f, H = 150.f;
private:
    MoonVerbProcessor& proc;
    juce::TextButton loadRoms{ "LOAD ROM FOLDER" }, forget{ "FORGET" }, importBank{ "IMPORT BANK" }, exportBank{ "EXPORT BANK" }, powerCycle{ "POWER CYCLE" }, clearMem{ "CLEAR MEMORY" };
    juce::ComboBox midiChannel; juce::ToggleButton omni{ "OMNI" }, pgmChange{ "PGM CHANGE" };
    juce::ToggleButton inPad{ "INPUT -20 dBV" }, outPad{ "OUTPUT -20 dBV" };
    juce::AudioProcessorValueTreeState::ButtonAttachment inAttach, outAttach;
    std::unique_ptr<juce::FileChooser> chooser;
    MoonVerbProcessor::View view; bool updating = false;
    float s() const { return (float)getWidth() / W; }
};

}
