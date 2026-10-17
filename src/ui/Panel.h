// MoonVerb's panel: a plug-in's controls for a machine that runs its own firmware. The sound, every program load, every value and every caption are the
// firmware's; what is NOT the hardware's is the way you reach them: presets are browsed by name and load when chosen, every parameter of the running program is
// a knob of its own with the firmware's own name and value, and the housekeeping (firmware images, MIDI, memory) sits in a drawer.
//
//   header    wordmark | <  preset name (bank, slot, modified)  >  | STORE  BYPASS  POWER  settings
//   screen    the unit's 16-digit display, and a status line (what was last touched, loading, faults)
//   grid      the program's parameters, row by row as the firmware groups them; masters ringed
//   footer    headroom lamps, INPUT level, the -20 switches
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include "../PluginProcessor.h"

namespace ui {

extern const juce::Colour BG, PANEL, PANEL_HI, LINE, INK, INK_DIM, ACCENT, VFD, VFD_DIM, LAMP_ON, LAMP_RED, MASTER;
juce::Font mono(float h, bool bold = true);
juce::Font sans(float h, bool bold = false);

class Look : public juce::LookAndFeel_V4 {
public:
    Look();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return sans(juce::jmin(13.f, (float)h * 0.48f), true); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return sans(13.f, true); }
    juce::Font getPopupMenuFont() override { return sans(14.f); }
};

/** The unit's display, drawn as a VFD. */
class Screen : public juce::Component {
public:
    void set(const std::string& digits, bool lit) { if (digits != text || lit != on) { text = digits; on = lit; repaint(); } }
    void paint(juce::Graphics&) override;
private:
    std::string text; bool on = false;
};

/** Headroom lamps: five, -24 dB to 0 dB, left to right. */
class Lamps : public juce::Component {
public:
    void set(int n, bool live) { if (leds != n || on != live) { leds = n; on = live; repaint(); } }
    void paint(juce::Graphics&) override;
private:
    int leds = 0; bool on = false;
};

/** One parameter of the running program: name, knob, the firmware's caption. Drag or wheel; shift = fine; double-click = back to the preset's value. */
class ParamKnob : public juce::Component {
public:
    ParamKnob(MoonVerbProcessor& p, int row, int col);
    void set(const MoonVerbProcessor::View& v, bool badges, bool selected);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    std::function<void(int, int)> onSelect;                 // clicked: the arrow keys now act on this one
    bool isRate() const { return rate; }
    bool rateSync() const { return rate && sync; }
    int row, col;
private:
    class Knob : public juce::Slider {
    public:
        explicit Knob(ParamKnob& o) : owner(o) { setWantsKeyboardFocus(false); }
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
        void mouseDoubleClick(const juce::MouseEvent&) override { if (!owner.rateSync()) owner.proc.resetCell(owner.row, owner.col); }
        ParamKnob& owner; float lastY = 0.f, acc = 0.f;
    };
    MoonVerbProcessor& proc; Knob knob; juce::SliderParameterAttachment attach;
    std::string name, caption; bool master = false, changed = false, badge = false, selected = false, rate = false, sync = false;
    juce::Rectangle<int> chip;                              // RATE: the HZ | SYNC switch
};

/** The preset browser: previous, the name (click for the list, wheel to step), next. */
class PresetBar : public juce::Component {
public:
    explicit PresetBar(MoonVerbProcessor& p);
    void set(const MoonVerbProcessor::View& v);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    void showList();
    MoonVerbProcessor& proc; juce::TextButton prev{ "<" }, next{ ">" };
    MoonVerbProcessor::View view; juce::Rectangle<int> nameArea; float wheelAcc = 0.f;
};

/** Firmware images, MIDI, registers, system: what a session sets once. */
class Drawer : public juce::Component {
public:
    explicit Drawer(MoonVerbProcessor& p);
    void set(const MoonVerbProcessor::View& v);
    void paint(juce::Graphics&) override;
    void resized() override;
    juce::ToggleButton badges{ "SHOW MATRIX NUMBERS" };
private:
    MoonVerbProcessor& proc;
    juce::TextButton loadRoms{ "LOAD ROM FOLDER" }, forget{ "FORGET" }, importBank{ "IMPORT BANK" }, exportBank{ "EXPORT BANK" }, powerCycle{ "POWER CYCLE" }, clearMem{ "CLEAR MEMORY" };
    juce::ComboBox midiChannel; juce::ToggleButton omni{ "OMNI" }, pgmChange{ "PROGRAM CHANGE" };
    std::unique_ptr<juce::FileChooser> chooser;
    MoonVerbProcessor::View view; bool updating = false;
};

class Panel : public juce::Component {
public:
    explicit Panel(MoonVerbProcessor&);
    void update(const MoonVerbProcessor::View&);
    void paint(juce::Graphics&) override;
    void resized() override;
    static constexpr float W = 1120.f, H = 660.f;
private:
    void storeMenu();
    bool keyPressed(const juce::KeyPress&) override;
    void select(int r, int c);
    MoonVerbProcessor& proc; MoonVerbProcessor::View view;
    PresetBar presets; Screen screen; Lamps lamps; Drawer drawer;
    juce::TextButton store{ "STORE" }, bypass{ "BYPASS" }, power{ "POWER" }, gear{ "SETTINGS" };
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttach;
    juce::Slider input; juce::SliderParameterAttachment inputAttach;
    juce::ToggleButton inPad{ "INPUT -20 dBV" }, outPad{ "OUTPUT -20 dBV" };
    juce::AudioProcessorValueTreeState::ButtonAttachment inAttach, outAttach;
    std::unique_ptr<ParamKnob> knobs[MoonVerbProcessor::ROWS * MoonVerbProcessor::COLS];
    juce::String statusLine; juce::Rectangle<int> grid, statusArea;
    bool paintedLoading = false, paintedLive = false, paintedRoms = false;
    int selR = -1, selC = -1;
    int lastTouched = -1; double touchedAt = 0; uint64_t lastEpoch = ~0ull; std::string layoutKey;
    float s() const { return (float)getWidth() / W; }
};

}
