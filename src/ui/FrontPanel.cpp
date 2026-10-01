#include "FrontPanel.h"

using namespace moonverb;

namespace ui {

const juce::Colour PANEL(0xff1b1d21), PANEL_HI(0xff272a30), INK(0xffd9dce1), INK_DIM(0xff8a8f99), VFD(0xff7dffd0), VFD_DIM(0xff2f6b57), LAMP_ON(0xff4df08a), LAMP_RED(0xffff4a3d), KEY_TOP(0xff3b3f46);

juce::Font mono(float h, bool bold) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain)); }
juce::Font sans(float h, bool bold) { return juce::Font(juce::FontOptions(juce::Font::getDefaultSansSerifFontName(), h, bold ? juce::Font::bold : juce::Font::plain)); }

// ---- look and feel -----------------------------------------------------------------------------------------------------------------------------------

DeviceLookAndFeel::DeviceLookAndFeel() {
    setColour(juce::TextButton::buttonColourId, KEY_TOP); setColour(juce::TextButton::textColourOffId, INK); setColour(juce::TextButton::textColourOnId, INK);
    setColour(juce::ToggleButton::textColourId, INK); setColour(juce::ToggleButton::tickColourId, LAMP_ON);
    setColour(juce::ComboBox::backgroundColourId, KEY_TOP); setColour(juce::ComboBox::textColourId, INK); setColour(juce::ComboBox::outlineColourId, juce::Colours::black);
    setColour(juce::ComboBox::arrowColourId, INK); setColour(juce::PopupMenu::backgroundColourId, PANEL_HI); setColour(juce::PopupMenu::textColourId, INK);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, VFD_DIM);
}

void DeviceLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) {
    const auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(2.f); const float d = std::min(b.getWidth(), b.getHeight()); const auto c = b.getCentre();
    const auto k = juce::Rectangle<float>(d, d).withCentre(c);
    g.setColour(juce::Colours::black.withAlpha(0.55f)); g.fillEllipse(k.expanded(2.f).translated(0.f, 1.5f));                      // shadow
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff55595f), c.x - d * 0.3f, c.y - d * 0.4f, juce::Colour(0xff17181b), c.x + d * 0.4f, c.y + d * 0.5f, false)); g.fillEllipse(k);
    g.setColour(juce::Colours::black); g.drawEllipse(k, 1.2f);
    g.setColour(juce::Colour(0xff2a2c31)); g.drawEllipse(k.reduced(d * 0.12f), 1.f);
    const float a = start + pos * (end - start);
    juce::Path tick; tick.addRoundedRectangle(-1.6f, -d * 0.46f, 3.2f, d * 0.26f, 1.2f);
    g.setColour(INK); g.fillPath(tick, juce::AffineTransform::rotation(a).translated(c.x, c.y));
}

void DeviceLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1.f);
    g.setColour(juce::Colours::black.withAlpha(0.5f)); g.fillRoundedRectangle(r.translated(0.f, 1.f), 4.f);
    g.setColour(down ? KEY_TOP.darker(0.4f) : (over ? KEY_TOP.brighter(0.15f) : KEY_TOP)); g.fillRoundedRectangle(r, 4.f);
    g.setColour(juce::Colours::black); g.drawRoundedRectangle(r, 4.f, 1.f);
}

void DeviceLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool) {
    const auto r = b.getLocalBounds().toFloat();
    const float d = std::min(14.f, r.getHeight() * 0.6f); const auto led = juce::Rectangle<float>(d, d).withCentre({ r.getX() + d, r.getCentreY() });
    g.setColour(juce::Colours::black); g.fillEllipse(led.expanded(1.5f));
    g.setColour(b.getToggleState() ? LAMP_ON : LAMP_ON.withAlpha(0.14f)); g.fillEllipse(led);
    if (b.getToggleState()) { g.setColour(LAMP_ON.withAlpha(0.25f)); g.fillEllipse(led.expanded(4.f)); }
    g.setColour(over ? INK : INK.withAlpha(0.85f)); g.setFont(sans(r.getHeight() * 0.5f, true));
    g.drawText(b.getButtonText(), r.withTrimmedLeft(d * 2.f + 2.f), juce::Justification::centredLeft);
}

void DeviceLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) {
    const auto r = juce::Rectangle<float>(0.f, 0.f, (float)w, (float)h).reduced(1.f);
    g.setColour(KEY_TOP); g.fillRoundedRectangle(r, 4.f); g.setColour(juce::Colours::black); g.drawRoundedRectangle(r, 4.f, 1.f);
    juce::Path p; const float cx = (float)w - 12.f, cy = (float)h * 0.5f; p.addTriangle(cx - 4.f, cy - 2.f, cx + 4.f, cy - 2.f, cx, cy + 3.f);
    g.setColour(INK); g.fillPath(p);
}

// ---- keys ----------------------------------------------------------------------------------------------------------------------------------------------

void PanelKey::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(1.5f);
    const float dy = down ? 1.5f : 0.f;
    g.setColour(juce::Colours::black.withAlpha(0.55f)); g.fillRoundedRectangle(r.translated(0.f, 2.f), 4.f);
    g.setGradientFill(juce::ColourGradient(down ? KEY_TOP.darker(0.5f) : KEY_TOP.brighter(0.18f), 0.f, r.getY() + dy, down ? KEY_TOP.darker(0.7f) : KEY_TOP.darker(0.3f), 0.f, r.getBottom(), false));
    g.fillRoundedRectangle(r.translated(0.f, dy), 4.f);
    g.setColour(juce::Colours::black); g.drawRoundedRectangle(r.translated(0.f, dy), 4.f, 1.f);
    g.setColour(INK);
    if (isArrow) {
        juce::Path p; const auto c = r.getCentre().translated(0.f, dy); const float a = r.getHeight() * 0.22f;
        if (text == "UP") p.addTriangle(c.x - a, c.y + a * 0.7f, c.x + a, c.y + a * 0.7f, c.x, c.y - a * 0.9f); else p.addTriangle(c.x - a, c.y - a * 0.7f, c.x + a, c.y - a * 0.7f, c.x, c.y + a * 0.9f);
        g.fillPath(p);
    } else { g.setFont(sans(r.getHeight() * 0.5f, true)); g.drawText(text, r.translated(0.f, dy), juce::Justification::centred); }
    if (hasLamp) {
        const float d = std::max(5.f, r.getHeight() * 0.2f); const auto led = juce::Rectangle<float>(d, d).withPosition(r.getRight() - d - 3.f, r.getY() + 3.f);
        const auto col = lampRed ? LAMP_RED : LAMP_ON;
        g.setColour(juce::Colours::black); g.fillEllipse(led.expanded(1.f)); g.setColour(lamp ? col : col.withAlpha(0.16f)); g.fillEllipse(led);
        if (lamp) { g.setColour(col.withAlpha(0.22f)); g.fillEllipse(led.expanded(3.f)); }
    }
}
void PanelKey::mouseDown(const juce::MouseEvent&) { down = true; downAt = juce::Time::getMillisecondCounter(); proc.pressKey(key, true); repaint(); }
void PanelKey::mouseUp(const juce::MouseEvent&) {
    if (!down) return;
    down = false; repaint();
    const juce::uint32 held = juce::Time::getMillisecondCounter() - downAt;
    const juce::uint32 minHold = 90;                                  // the firmware's scan needs the key down for about 0.05 s of its own time
    MoonVerbProcessor& p = proc; const Keys::Key k = key;
    if (held >= minHold) p.pressKey(k, false); else juce::Timer::callAfterDelay((int)(minHold - held), [&p, k]() { p.pressKey(k, false); });
}

// ---- soft knob --------------------------------------------------------------------------------------------------------------------------------------

void SoftKnob::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat().reduced(2.f); const float d = std::min(b.getWidth(), b.getHeight()); const auto c = b.getCentre();
    const auto k = juce::Rectangle<float>(d, d).withCentre(c);
    g.setColour(juce::Colours::black.withAlpha(0.55f)); g.fillEllipse(k.expanded(2.f).translated(0.f, 2.f));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff5d6168), c.x - d * 0.3f, c.y - d * 0.4f, juce::Colour(0xff141518), c.x + d * 0.4f, c.y + d * 0.5f, false)); g.fillEllipse(k);
    g.setColour(juce::Colours::black); g.drawEllipse(k, 1.4f);
    for (int i = 0; i < 36; i++) {                                    // knurled edge
        juce::Path t; t.addRectangle(-0.8f, -d * 0.5f, 1.6f, d * 0.07f);
        g.setColour(juce::Colour(0xff0e0f11)); g.fillPath(t, juce::AffineTransform::rotation(juce::MathConstants<float>::twoPi * (float)i / 36.f + angle).translated(c.x, c.y));
    }
    g.setColour(juce::Colour(0xff2a2c31)); g.drawEllipse(k.reduced(d * 0.17f), 1.f);
    juce::Path tick; tick.addRoundedRectangle(-1.8f, -d * 0.38f, 3.6f, d * 0.22f, 1.4f);
    g.setColour(INK); g.fillPath(tick, juce::AffineTransform::rotation(angle).translated(c.x, c.y));
}
void SoftKnob::turn(int detents) {
    if (!detents) return;
    angle += (float)detents * 0.12f;
    // one detent is one step of the parameter; a fast turn travels further (the firmware's value range runs from a few steps to a thousand)
    const int mag = std::abs(detents); const int step = mag <= 2 ? mag : (int)(mag * (1.f + (float)(mag - 2) * 0.6f));
    proc.softKnob(detents > 0 ? step : -step); repaint();
}
void SoftKnob::mouseDrag(const juce::MouseEvent& e) {
    acc += lastY - e.position.y; lastY = e.position.y;
    const int d = (int)(acc / 3.f); if (d) { acc -= (float)d * 3.f; turn(d); }
}
void SoftKnob::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) { const float v = w.deltaY * 20.f; turn((int)std::lround(v > 0 ? std::max(1.f, v) : std::min(-1.f, v))); }

// ---- display, level lamps, power ------------------------------------------------------------------------------------------------------------------------

void Readout::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff04100c)); g.fillRoundedRectangle(r, 3.f);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.07f), 0.f, 0.f, juce::Colours::transparentWhite, 0.f, r.getHeight() * 0.5f, false)); g.fillRoundedRectangle(r, 3.f);
    std::string txt = view.live ? view.snap.display : view.status;
    std::vector<std::pair<char, bool>> digits;                       // a '.' belongs to the digit before it
    for (char ch : txt) { if (ch == '.' && !digits.empty() && !digits.back().second && view.live) digits.back().second = true; else digits.push_back({ ch, false }); }
    while (digits.size() < 16) digits.push_back({ ' ', false });
    const float cw = r.getWidth() / 16.f; const auto fg = view.live ? VFD : VFD_DIM.brighter(0.25f);
    for (int i = 0; i < 16; i++) {
        const auto cell = juce::Rectangle<float>(r.getX() + (float)i * cw, r.getY(), cw, r.getHeight());
        g.setColour(VFD.withAlpha(0.045f)); g.fillRoundedRectangle(cell.reduced(1.f, 3.f), 2.f);
        if (digits[(size_t)i].first == ' ') continue;
        const juce::String ch(juce::CharPointer_ASCII(std::string(1, digits[(size_t)i].first).c_str()));
        g.setFont(mono(r.getHeight() * 0.7f));
        g.setColour(fg.withAlpha(0.22f)); g.drawText(ch, cell.translated(0.f, 0.f).expanded(1.5f), juce::Justification::centred);       // glow
        g.setColour(fg); g.drawText(ch, cell, juce::Justification::centred);
        if (digits[(size_t)i].second) { g.fillEllipse(cell.getRight() - cw * 0.2f - 2.f, cell.getBottom() - r.getHeight() * 0.2f, 3.4f, 3.4f); }
    }
}

void LevelLeds::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat(); const float h = r.getHeight() / 5.f;
    for (int k = 0; k < 5; k++) {                                     // k = 0 is the top lamp, 0 dB; the bar fills from the bottom (-24 dB)
        const bool lit = on && leds >= 5 - k; const auto col = k == 0 ? LAMP_RED : LAMP_ON;
        const auto seg = juce::Rectangle<float>(r.getX() + 1.f, r.getY() + (float)k * h + 2.f, r.getWidth() - 2.f, h - 4.f);
        g.setColour(juce::Colours::black); g.fillRoundedRectangle(seg.expanded(1.f), 2.f);
        g.setColour(lit ? col : col.withAlpha(0.14f)); g.fillRoundedRectangle(seg, 2.f);
        if (lit) { g.setColour(col.withAlpha(0.2f)); g.fillRoundedRectangle(seg.expanded(2.5f), 3.f); }
    }
}

void PowerSwitch::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat().reduced(3.f); const bool on = proc.powered();
    g.setColour(juce::Colours::black); g.fillRoundedRectangle(r, 5.f);
    const auto cap = on ? r.withTrimmedBottom(r.getHeight() * 0.45f).reduced(2.f) : r.withTrimmedTop(r.getHeight() * 0.45f).reduced(2.f);
    g.setGradientFill(juce::ColourGradient(KEY_TOP.brighter(0.2f), 0.f, cap.getY(), KEY_TOP.darker(0.4f), 0.f, cap.getBottom(), false)); g.fillRoundedRectangle(cap, 4.f);
    g.setColour(on ? LAMP_ON : INK_DIM); g.fillEllipse(juce::Rectangle<float>(5.f, 5.f).withCentre({ cap.getCentreX(), cap.getCentreY() }));
}

// ---- the front panel -----------------------------------------------------------------------------------------------------------------------------------

FrontPanel::FrontPanel(MoonVerbProcessor& p) : proc(p), input(juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox), inputAttach(*p.apvts.getParameter("input"), input), soft(p), power(p) {
    input.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true); input.setMouseDragSensitivity(140);
    addAndMakeVisible(input); addAndMakeVisible(soft); addAndMakeVisible(readout); addAndMakeVisible(leds); addAndMakeVisible(power);
    keys[UP].reset(new PanelKey(p, Keys::F1, "UP", true)); keys[DOWN].reset(new PanelKey(p, Keys::F2, "DOWN", true));
    keys[PGM].reset(new PanelKey(p, Keys::F4, "PGM")); keys[REG].reset(new PanelKey(p, Keys::F3, "REG"));
    keys[LOAD].reset(new PanelKey(p, Keys::LOAD, "LOAD")); keys[BYP].reset(new PanelKey(p, Keys::BYP, "BYPASS"));
    keys[PGM]->hasLamp = keys[REG]->hasLamp = keys[BYP]->hasLamp = true;
    for (int d = 0; d < 10; d++) keys[DIGIT0 + d].reset(new PanelKey(p, Keys::digit(d), juce::String(d)));
    for (auto& k : keys) if (k) addAndMakeVisible(*k);
}

juce::Rectangle<float> FrontPanel::r(float x, float y, float w, float h) const { return { x * s(), y * s(), w * s(), h * s() }; }

void FrontPanel::resized() {
    input.setBounds(r(86, 22, 56, 56).toNearestInt()); soft.setBounds(r(556, 16, 70, 70).toNearestInt()); readout.setBounds(r(168, 29, 356, 52).toNearestInt());
    leds.setBounds(r(44, 22, 18, 66).toNearestInt()); power.setBounds(r(1040, 24, 44, 60).toNearestInt());
    keys[UP]->setBounds(r(650, 22, 38, 30).toNearestInt()); keys[DOWN]->setBounds(r(650, 56, 38, 30).toNearestInt());
    const int order[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 };
    for (int i = 0; i < 10; i++) keys[DIGIT0 + order[i]]->setBounds(r(704 + (float)(i % 5) * 36, 22 + (float)(i / 5) * 34, 34, 30).toNearestInt());
    keys[PGM]->setBounds(r(900, 22, 58, 30).toNearestInt()); keys[REG]->setBounds(r(962, 22, 58, 30).toNearestInt());
    keys[LOAD]->setBounds(r(900, 56, 58, 30).toNearestInt()); keys[BYP]->setBounds(r(962, 56, 58, 30).toNearestInt());
}

void FrontPanel::update(const MoonVerbProcessor::View& v) {
    readout.set(v);
    leds.set(v.snap.leds, v.live);
    keys[PGM]->setLamp(v.live && v.snap.mode == 2); keys[REG]->setLamp(v.live && v.snap.mode == 4); keys[BYP]->setLamp(v.live && v.snap.bypassed, true);
    power.repaint();
}

void FrontPanel::paint(juce::Graphics& g) {
    g.addTransform(juce::AffineTransform::scale(s()));
    const auto face = juce::Rectangle<float>(0.f, 0.f, W, H);
    g.setGradientFill(juce::ColourGradient(PANEL_HI, 0.f, 0.f, PANEL, 0.f, H, false)); g.fillRect(face);
    for (int y = 3; y < (int)H; y += 3) { g.setColour(juce::Colours::white.withAlpha(0.012f)); g.drawHorizontalLine(y, 0.f, W); }          // brushed
    for (int ear = 0; ear < 2; ear++) {                                                                                                   // rack ears
        const float x = ear == 0 ? 0.f : W - 30.f;
        g.setColour(juce::Colour(0xff2f3238)); g.fillRect(x, 0.f, 30.f, H); g.setColour(juce::Colours::black.withAlpha(0.5f)); g.drawVerticalLine((int)(ear == 0 ? 30.f : W - 30.f), 0.f, H);
        for (float cy : { 22.f, H - 22.f }) { const auto sc = juce::Rectangle<float>(13.f, 13.f).withCentre({ x + 15.f, cy });
            g.setColour(juce::Colours::black); g.fillEllipse(sc.expanded(2.f)); g.setColour(juce::Colour(0xff6b6f77)); g.fillEllipse(sc); g.setColour(juce::Colours::black); g.drawLine(sc.getX() + 2.f, sc.getCentreY() + 1.f, sc.getRight() - 2.f, sc.getCentreY() - 1.f, 1.4f); }
    }
    g.setColour(juce::Colours::black.withAlpha(0.7f)); g.drawRect(face, 1.f);
    // the display's bezel
    const auto bez = juce::Rectangle<float>(160.f, 22.f, 372.f, 66.f).reduced(1.f);
    g.setColour(juce::Colours::black); g.fillRoundedRectangle(bez, 5.f); g.setColour(juce::Colour(0xff3a3d44)); g.drawRoundedRectangle(bez, 5.f, 1.2f);
    // legends
    g.setColour(INK_DIM); g.setFont(sans(8.5f, true));
    g.drawText("INPUT LEVEL", juce::Rectangle<float>(70.f, 88.f, 88.f, 14.f), juce::Justification::centred);
    g.drawText("SOFT KNOB", juce::Rectangle<float>(551.f, 88.f, 80.f, 14.f), juce::Justification::centred);
    g.drawText("ROW", juce::Rectangle<float>(644.f, 88.f, 50.f, 14.f), juce::Justification::centred);
    g.drawText("PROGRAM / REGISTER / PARAMETER", juce::Rectangle<float>(704.f, 88.f, 190.f, 14.f), juce::Justification::centred);
    g.drawText("POWER", juce::Rectangle<float>(1030.f, 88.f, 64.f, 14.f), juce::Justification::centred);
    g.drawText("-24  -18  -12  -6  0", juce::Rectangle<float>(30.f, 90.f, 50.f, 0.f), juce::Justification::centred);
    // the wordmark
    g.setColour(INK); g.setFont(sans(17.f, true)); g.drawText("moonverb", juce::Rectangle<float>(1090.f, 36.f, 82.f, 26.f), juce::Justification::centredLeft);
    g.setColour(INK_DIM); g.setFont(sans(7.5f, true)); g.drawText("DIGITAL EFFECTS", juce::Rectangle<float>(1090.f, 60.f, 80.f, 10.f), juce::Justification::centredLeft);
}

// ---- the rear panel ---------------------------------------------------------------------------------------------------------------------------------------

RearPanel::RearPanel(MoonVerbProcessor& p) : proc(p), inAttach(p.apvts, "inpad", inPad), outAttach(p.apvts, "outpad", outPad) {
    for (auto* b : { &loadRoms, &forget, &importBank, &exportBank, &powerCycle, &clearMem }) addAndMakeVisible(*b);
    for (int ch = 1; ch <= 16; ch++) midiChannel.addItem(juce::String(ch), ch);
    midiChannel.setSelectedId(1, juce::dontSendNotification);
    for (auto* b : { &omni, &pgmChange, &inPad, &outPad }) addAndMakeVisible(*b);
    addAndMakeVisible(midiChannel);
    loadRoms.setTooltip("Choose the folder that holds your own firmware images");
    loadRoms.onClick = [this]() {
        chooser = std::make_unique<juce::FileChooser>("Choose the folder with the firmware images (U62, U95, U67, U48, U49)", juce::File(), "*", true);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) { if (fc.getResult().isDirectory()) proc.loadRomFolder(fc.getResult()); });
    };
    forget.onClick = [this]() { proc.forgetRoms(); };
    importBank.onClick = [this]() {
        chooser = std::make_unique<juce::FileChooser>("Import a SysEx register bank", juce::File(), "*.syx;*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.importBank(fc.getResult()); });
    };
    exportBank.onClick = [this]() {
        chooser = std::make_unique<juce::FileChooser>("Export the registers as a SysEx bank", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MoonVerb-registers.syx"), "*.syx");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) proc.exportBank(fc.getResult()); });
    };
    powerCycle.onClick = [this]() { proc.powerCycle(); };
    clearMem.onClick = [this]() {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon).withTitle("Clear memory").withMessage("Erase all 50 registers and the settings, as the unit's CLEAR MEMORY does?").withButton("Clear").withButton("Cancel"),
            [this](int r) { if (r == 1) proc.clearMemory(); });
    };
    midiChannel.onChange = [this]() { if (!updating) proc.setMidiChannel(midiChannel.getSelectedId()); };
    omni.onClick = [this]() { if (!updating) proc.setOmni(omni.getToggleState()); };
    pgmChange.onClick = [this]() { if (!updating) proc.setProgramChange(pgmChange.getToggleState()); };
}

void RearPanel::update(const MoonVerbProcessor::View& v) {
    view = v; updating = true;
    if (v.live) { midiChannel.setSelectedId(v.midiChannel, juce::dontSendNotification); omni.setToggleState(v.omni, juce::dontSendNotification); pgmChange.setToggleState(v.pgmChange, juce::dontSendNotification); }
    updating = false; repaint();
}

void RearPanel::resized() {
    const float k = s(); auto R = [k](float x, float y, float w, float h) { return juce::Rectangle<float>(x * k, y * k, w * k, h * k).toNearestInt(); };
    loadRoms.setBounds(R(52, 40, 150, 28)); forget.setBounds(R(208, 40, 76, 28));
    midiChannel.setBounds(R(346, 40, 64, 28)); omni.setBounds(R(424, 40, 80, 28)); pgmChange.setBounds(R(346, 82, 140, 28));
    inPad.setBounds(R(564, 40, 132, 28)); outPad.setBounds(R(564, 82, 132, 28));
    importBank.setBounds(R(750, 40, 126, 28)); exportBank.setBounds(R(750, 82, 126, 28));
    powerCycle.setBounds(R(928, 40, 140, 28)); clearMem.setBounds(R(928, 82, 140, 28));
}

void RearPanel::paint(juce::Graphics& g) {
    g.addTransform(juce::AffineTransform::scale(s()));
    g.setGradientFill(juce::ColourGradient(PANEL.darker(0.2f), 0.f, 0.f, PANEL.darker(0.5f), 0.f, H, false)); g.fillRect(0.f, 0.f, W, H);
    g.setColour(juce::Colours::black); g.drawHorizontalLine(0, 0.f, W);
    auto group = [&g](const juce::String& title, float x, float w) {
        g.setColour(INK_DIM.withAlpha(0.5f)); g.drawRoundedRectangle(x, 24.f, w, 112.f, 5.f, 1.f);
        g.setColour(PANEL.darker(0.3f)); g.fillRect(x + 8.f, 18.f, (float)title.length() * 7.4f + 10.f, 12.f);
        g.setColour(INK_DIM); g.setFont(sans(9.5f, true)); g.drawText(title, juce::Rectangle<float>(x + 12.f, 18.f, 200.f, 12.f), juce::Justification::centredLeft);
    };
    group("FIRMWARE IMAGES (YOUR OWN)", 36.f, 264.f); group("MIDI", 330.f, 190.f); group("LEVELS (REAR SWITCHES)", 548.f, 160.f); group("REGISTERS", 734.f, 158.f); group("SYSTEM", 912.f, 170.f);
    g.setColour(INK_DIM); g.setFont(sans(8.5f, false));
    g.drawText("-20 = 15 dB less at the converter", juce::Rectangle<float>(556.f, 112.f, 150.f, 12.f), juce::Justification::centredLeft);
    // the firmware images found
    const char* names[moonverb::ROLES] = { "U62 MASTER", "U95 SLAVE ", "U67 OPCODE", "U48 PROM  ", "U49 PROM  " };
    g.setFont(mono(8.5f, false));
    for (int i = 0; i < moonverb::ROLES; i++) {
        const bool have = !view.romFile[i].empty();
        g.setColour(have ? VFD : INK_DIM.withAlpha(0.6f));
        g.drawText(juce::String(names[i]) + "  " + (have ? juce::String(view.romFile[i]) : juce::String("-")), juce::Rectangle<float>(52.f, 72.f + (float)i * 10.5f, 244.f, 11.f), juce::Justification::centredLeft);
    }
    g.setColour(INK_DIM); g.setFont(mono(9.f)); g.drawText(view.romsOk ? "VERSION " + juce::String(view.family) : (view.status.empty() ? juce::String("NO FIRMWARE") : juce::String(view.status)), juce::Rectangle<float>(52.f, 124.f, 244.f, 11.f), juce::Justification::centredLeft);
}

}
