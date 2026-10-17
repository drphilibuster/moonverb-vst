#include "Panel.h"

using namespace moonverb;

namespace ui {

const juce::Colour BG(0xff141518), PANEL(0xff1d1f24), PANEL_HI(0xff262930), LINE(0xff32363e), INK(0xffe2e5ea), INK_DIM(0xff8b919c), ACCENT(0xff6fd6b4),
                   VFD(0xff7dffd0), VFD_DIM(0xff2a5c4b), LAMP_ON(0xff4df08a), LAMP_RED(0xffff4a3d), MASTER(0xfff0b860);

juce::Font mono(float h, bool bold) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain)); }
juce::Font sans(float h, bool bold) { return juce::Font(juce::FontOptions(juce::Font::getDefaultSansSerifFontName(), h, bold ? juce::Font::bold : juce::Font::plain)); }

static juce::String slotText(bool user, int slot) { return juce::String(user ? "USER " : "FACTORY ") + juce::String(slot / 10) + "." + juce::String(slot % 10); }

// ---- look ------------------------------------------------------------------------------------------------------------------------------------------------

Look::Look() {
    setColour(juce::TextButton::buttonColourId, PANEL_HI); setColour(juce::TextButton::buttonOnColourId, PANEL_HI);
    setColour(juce::TextButton::textColourOffId, INK); setColour(juce::TextButton::textColourOnId, INK);
    setColour(juce::ToggleButton::textColourId, INK); setColour(juce::ToggleButton::tickColourId, LAMP_ON);
    setColour(juce::ComboBox::backgroundColourId, PANEL_HI); setColour(juce::ComboBox::textColourId, INK); setColour(juce::ComboBox::outlineColourId, LINE); setColour(juce::ComboBox::arrowColourId, INK);
    setColour(juce::PopupMenu::backgroundColourId, PANEL); setColour(juce::PopupMenu::textColourId, INK); setColour(juce::PopupMenu::headerTextColourId, ACCENT);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, VFD_DIM); setColour(juce::PopupMenu::highlightedTextColourId, INK);
    setColour(juce::AlertWindow::backgroundColourId, PANEL); setColour(juce::AlertWindow::textColourId, INK);
    setColour(juce::TooltipWindow::backgroundColourId, PANEL_HI); setColour(juce::TooltipWindow::textColourId, INK);
}

void Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& sl) {
    const auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(3.f); const float d = std::min(b.getWidth(), b.getHeight()); const auto c = b.getCentre();
    const float rad = d * 0.5f, ring = juce::jmax(2.5f, d * 0.08f);
    const bool master = (bool)sl.getProperties()["master"];
    const auto col = master ? MASTER : ACCENT;
    juce::Path track; track.addCentredArc(c.x, c.y, rad - ring, rad - ring, 0.f, start, end, true);
    g.setColour(LINE); g.strokePath(track, juce::PathStrokeType(ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float a = start + pos * (end - start);
    juce::Path val; val.addCentredArc(c.x, c.y, rad - ring, rad - ring, 0.f, start, a, true);
    g.setColour(sl.isEnabled() ? col : INK_DIM); g.strokePath(val, juce::PathStrokeType(ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float kr = rad - ring * 2.4f;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a3e46), c.x, c.y - kr, juce::Colour(0xff1a1c20), c.x, c.y + kr, false)); g.fillEllipse(c.x - kr, c.y - kr, kr * 2, kr * 2);
    g.setColour(juce::Colours::black.withAlpha(0.6f)); g.drawEllipse(c.x - kr, c.y - kr, kr * 2, kr * 2, 1.f);
    juce::Path tick; tick.addRoundedRectangle(-1.3f, -kr + 2.f, 2.6f, kr * 0.5f, 1.f);
    g.setColour(INK); g.fillPath(tick, juce::AffineTransform::rotation(a).translated(c.x, c.y));
}

void Look::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1.f);
    const bool on = b.getToggleState(); const juce::Colour lit = b.getProperties().contains("lit") ? juce::Colour((juce::uint32)(juce::int64)b.getProperties()["lit"]) : ACCENT;
    g.setColour(down ? PANEL_HI.darker(0.3f) : (over ? PANEL_HI.brighter(0.08f) : PANEL_HI)); g.fillRoundedRectangle(r, 5.f);
    g.setColour(on ? lit : LINE); g.drawRoundedRectangle(r, 5.f, on ? 1.6f : 1.f);
    if (on) { g.setColour(lit.withAlpha(0.12f)); g.fillRoundedRectangle(r, 5.f); }
}
void Look::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    const bool on = b.getToggleState(); const juce::Colour lit = b.getProperties().contains("lit") ? juce::Colour((juce::uint32)(juce::int64)b.getProperties()["lit"]) : ACCENT;
    g.setColour(!b.isEnabled() ? INK_DIM.withAlpha(0.5f) : (on ? lit : INK)); g.setFont(getTextButtonFont(b, b.getHeight()));
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

void Look::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool) {
    const auto r = b.getLocalBounds().toFloat();
    const float d = std::min(12.f, r.getHeight() * 0.55f); const auto led = juce::Rectangle<float>(d, d).withCentre({ r.getX() + d, r.getCentreY() });
    g.setColour(juce::Colours::black); g.fillEllipse(led.expanded(1.5f));
    g.setColour(b.getToggleState() ? LAMP_ON : LAMP_ON.withAlpha(0.14f)); g.fillEllipse(led);
    if (b.getToggleState()) { g.setColour(LAMP_ON.withAlpha(0.25f)); g.fillEllipse(led.expanded(4.f)); }
    g.setColour(over ? INK : INK.withAlpha(0.85f)); g.setFont(sans(juce::jmin(13.f, r.getHeight() * 0.5f), true));
    g.drawText(b.getButtonText(), r.withTrimmedLeft(d * 2.f + 4.f), juce::Justification::centredLeft);
}

void Look::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) {
    const auto r = juce::Rectangle<float>(0.f, 0.f, (float)w, (float)h).reduced(1.f);
    g.setColour(PANEL_HI); g.fillRoundedRectangle(r, 5.f); g.setColour(LINE); g.drawRoundedRectangle(r, 5.f, 1.f);
    juce::Path p; const float cx = (float)w - 12.f, cy = (float)h * 0.5f; p.addTriangle(cx - 4.f, cy - 2.f, cx + 4.f, cy - 2.f, cx, cy + 3.f);
    g.setColour(INK); g.fillPath(p);
}

// ---- screen, lamps ------------------------------------------------------------------------------------------------------------------------------------------

void Screen::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff04100c)); g.fillRoundedRectangle(r, 6.f);
    g.setColour(LINE); g.drawRoundedRectangle(r.reduced(0.5f), 6.f, 1.f);
    std::vector<std::pair<char, bool>> digits;                       // a '.' belongs to the digit before it
    for (char ch : text) { if (ch == '.' && !digits.empty() && !digits.back().second) digits.back().second = true; else digits.push_back({ ch, false }); }
    while (digits.size() < 16) digits.push_back({ ' ', false });
    const auto inner = r.reduced(r.getHeight() * 0.18f, r.getHeight() * 0.14f);
    const float cw = inner.getWidth() / 16.f; const auto fg = on ? VFD : VFD_DIM.brighter(0.3f);
    for (int i = 0; i < 16; i++) {
        const auto cell = juce::Rectangle<float>(inner.getX() + (float)i * cw, inner.getY(), cw, inner.getHeight());
        g.setColour(VFD.withAlpha(0.04f)); g.fillRoundedRectangle(cell.reduced(1.f, 0.f), 2.f);
        if (digits[(size_t)i].first == ' ') continue;
        const juce::String ch = juce::String::charToString((juce::juce_wchar)(unsigned char)digits[(size_t)i].first);
        g.setFont(mono(inner.getHeight() * 0.82f));
        g.setColour(fg.withAlpha(0.2f)); g.drawText(ch, cell.expanded(1.5f), juce::Justification::centred);
        g.setColour(fg); g.drawText(ch, cell, juce::Justification::centred);
        if (digits[(size_t)i].second) g.fillEllipse(cell.getRight() - cw * 0.22f, cell.getBottom() - inner.getHeight() * 0.16f, 3.f, 3.f);
    }
}

void Lamps::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat(); const float w = r.getWidth() / 5.f;
    static const char* lab[5] = { "-24", "-18", "-12", "-6", "0" };
    for (int k = 0; k < 5; k++) {                                    // k = 4 is 0 dB (red); the bar fills from the left (-24 dB)
        const bool lit = on && leds >= k + 1; const auto col = k == 4 ? LAMP_RED : LAMP_ON;
        const auto seg = juce::Rectangle<float>(r.getX() + (float)k * w + 2.f, r.getY(), w - 4.f, r.getHeight() * 0.42f);
        g.setColour(juce::Colours::black); g.fillRoundedRectangle(seg.expanded(1.f), 2.f);
        g.setColour(lit ? col : col.withAlpha(0.13f)); g.fillRoundedRectangle(seg, 2.f);
        if (lit) { g.setColour(col.withAlpha(0.2f)); g.fillRoundedRectangle(seg.expanded(2.5f), 3.f); }
        g.setColour(INK_DIM); g.setFont(sans(r.getHeight() * 0.3f, true));
        g.drawText(lab[k], juce::Rectangle<float>(seg.getX(), r.getY() + r.getHeight() * 0.55f, seg.getWidth(), r.getHeight() * 0.4f), juce::Justification::centred);
    }
}

// ---- a parameter -------------------------------------------------------------------------------------------------------------------------------------------

ParamKnob::ParamKnob(MoonVerbProcessor& p, int r, int c)
    : row(r), col(c), proc(p), knob(*this), attach(*p.apvts.getParameter("p" + juce::String(r) + juce::String(c)), knob) {
    knob.setSliderStyle(juce::Slider::RotaryVerticalDrag); knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    knob.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    knob.setScrollWheelEnabled(true); knob.setDoubleClickReturnValue(false, 0.0);
    addAndMakeVisible(knob);
}

void ParamKnob::Knob::mouseDown(const juce::MouseEvent& e) {
    if (owner.onSelect) owner.onSelect(owner.row, owner.col);
    if (owner.rateSync()) { lastY = e.position.y; acc = 0.f; return; }              // SYNC: the knob walks the note values, not the word
    setMouseDragSensitivity(e.mods.isShiftDown() ? 1600 : 320); juce::Slider::mouseDown(e);
}
void ParamKnob::Knob::mouseDrag(const juce::MouseEvent& e) {
    if (owner.rateSync()) { acc += lastY - e.position.y; lastY = e.position.y; while (acc >= 14.f) { owner.proc.stepRateDiv(-1); acc -= 14.f; } while (acc <= -14.f) { owner.proc.stepRateDiv(+1); acc += 14.f; } return; }
    setMouseDragSensitivity(e.mods.isShiftDown() ? 1600 : 320); juce::Slider::mouseDrag(e);
}
void ParamKnob::Knob::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    if (owner.rateSync()) { if (w.deltaY != 0.f) owner.proc.stepRateDiv(w.deltaY > 0 ? -1 : +1); return; }    // up = a longer note = a slower beat
    juce::Slider::mouseWheelMove(e, w);
}

void ParamKnob::mouseDown(const juce::MouseEvent& e) {
    if (onSelect) onSelect(row, col);
    if (rate && chip.contains(e.getPosition())) proc.setRateMode(sync ? MoonVerbProcessor::RATE_HZ : MoonVerbProcessor::RATE_SYNC);
}

void ParamKnob::set(const MoonVerbProcessor::View& v, bool badges, bool sel) {
    std::string n = v.snap.name[row][col], c = v.snap.caption[row][col];
    const bool m = v.snap.master[row][col], ch = v.baseValid && v.snap.word[row][col] != v.base[row][col];
    const bool rt = v.rateRow == row && v.rateCol == col, sy = rt && v.rateMode == MoonVerbProcessor::RATE_SYNC;
    if (rt) {
        const int bpm = v.snap.word[row][col] - 384; char b[64];
        if (sy) {
            if (MoonVerbProcessor::rateReachable(v.hostBpm, v.rateDiv)) std::snprintf(b, sizeof b, "%s  %d BPM", MoonVerbProcessor::rateDivName(v.rateDiv), bpm);
            else std::snprintf(b, sizeof b, "%s OUT OF RANGE", MoonVerbProcessor::rateDivName(v.rateDiv));
        } else std::snprintf(b, sizeof b, "%.2f HZ", bpm / 60.0);
        c = b;
    }
    if (n != name || c != caption || m != master || ch != changed || badges != badge || sel != selected || rt != rate || sy != sync) {
        name = n; caption = c; master = m; changed = ch; badge = badges; selected = sel; rate = rt; sync = sy;
        knob.getProperties().set("master", master); knob.repaint(); repaint();
    }
    knob.setTooltip(rate ? juce::String(sync ? "SYNC: the beat is a note value of the host's tempo (drag or wheel to choose it). Click SYNC for Hz." : "The beat as a rate. Click HZ to follow the host's tempo.") : juce::String());
}

void ParamKnob::resized() {
    const auto b = getLocalBounds(); const int k = juce::jmin(b.getWidth(), (int)(b.getHeight() * 0.56f));
    knob.setBounds(juce::Rectangle<int>(k, k).withCentre({ b.getCentreX(), b.getY() + (int)(b.getHeight() * 0.22f) + k / 2 }));
    const int cw = juce::jmax(26, b.getWidth() / 4), chh = juce::jmax(12, (int)(b.getHeight() * 0.17f));
    chip = juce::Rectangle<int>(b.getRight() - cw - 2, knob.getBounds().getCentreY() - chh / 2, cw, chh);
}

void ParamKnob::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat(); const float h = b.getHeight();
    if (selected) { g.setColour(ACCENT.withAlpha(0.10f)); g.fillRoundedRectangle(b.reduced(1.f), 6.f); g.setColour(ACCENT.withAlpha(0.7f)); g.drawRoundedRectangle(b.reduced(1.f), 6.f, 1.2f); }
    g.setColour(master ? MASTER : INK); g.setFont(sans(h * 0.15f, true));
    g.drawFittedText(juce::String(name), b.withHeight(h * 0.2f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    g.setColour(changed ? ACCENT : INK_DIM); g.setFont(mono(h * 0.145f, false));
    g.drawFittedText(juce::String(caption), b.withTrimmedTop(h * 0.8f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
    if (badge) { g.setColour(INK_DIM.withAlpha(0.7f)); g.setFont(mono(h * 0.12f, false)); g.drawText(juce::String(row) + "." + juce::String(col), b.withTrimmedTop(h * 0.2f).withHeight(h * 0.16f).withTrimmedLeft(2.f), juce::Justification::left); }
    if (rate) {
        const auto r = chip.toFloat(); g.setColour(sync ? ACCENT.withAlpha(0.18f) : PANEL_HI); g.fillRoundedRectangle(r, 3.f);
        g.setColour(sync ? ACCENT : LINE); g.drawRoundedRectangle(r, 3.f, 1.f);
        g.setColour(sync ? ACCENT : INK_DIM); g.setFont(sans(r.getHeight() * 0.62f, true)); g.drawText(sync ? "SYNC" : "HZ", r, juce::Justification::centred);
    }
}

// ---- presets -----------------------------------------------------------------------------------------------------------------------------------------------

PresetBar::PresetBar(MoonVerbProcessor& p) : proc(p) {
    prev.setTooltip("Previous preset"); next.setTooltip("Next preset");
    prev.onClick = [this]() { proc.stepPreset(-1); }; next.onClick = [this]() { proc.stepPreset(+1); };
    addAndMakeVisible(prev); addAndMakeVisible(next);
}
void PresetBar::set(const MoonVerbProcessor::View& v) { view = v; repaint(); }
void PresetBar::resized() {
    auto b = getLocalBounds(); const int w = b.getHeight();
    prev.setBounds(b.removeFromLeft(w).reduced(3)); next.setBounds(b.removeFromRight(w).reduced(3)); nameArea = b.reduced(4, 0);
}
void PresetBar::paint(juce::Graphics& g) {
    const auto r = nameArea.toFloat(); const float h = r.getHeight();
    g.setColour(BG); g.fillRoundedRectangle(r, 6.f); g.setColour(LINE); g.drawRoundedRectangle(r.reduced(0.5f), 6.f, 1.f);
    const bool live = view.live;
    const juce::String where = slotText(view.user, view.slot);
    const std::string nm = view.storing && view.presetName().empty() ? view.storingName : view.presetName();
    juce::String title = !live ? juce::String(view.status.empty() ? "NO FIRMWARE" : view.status) : (nm.empty() ? juce::String(view.namesReady || view.user ? "(empty)" : "reading names...") : juce::String(nm));
    const auto text = r.reduced(h * 0.3f, 0.f);
    g.setColour(INK_DIM); g.setFont(sans(h * 0.24f, true));
    g.drawText(live ? where + (view.storing ? "   STORING" : (view.loading ? "   LOADING" : (view.modified ? "   EDITED" : ""))) : juce::String("MOONVERB"), text.withHeight(h * 0.42f).withTrimmedTop(h * 0.08f), juce::Justification::centredLeft);
    g.setColour(view.loading ? INK.withAlpha(0.55f) : INK); g.setFont(sans(h * 0.4f, true));
    g.drawText(title, text.withTrimmedTop(h * 0.38f).withTrimmedRight(h * 0.5f), juce::Justification::centredLeft);
    juce::Path tri; const float cx = r.getRight() - h * 0.4f, cy = r.getCentreY(); tri.addTriangle(cx - 5.f, cy - 2.5f, cx + 5.f, cy - 2.5f, cx, cy + 3.5f);
    g.setColour(INK_DIM); g.fillPath(tri);
}
void PresetBar::mouseUp(const juce::MouseEvent& e) { if (nameArea.contains(e.getPosition()) && view.live) showList(); }
void PresetBar::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    wheelAcc += w.deltaY; if (std::fabs(wheelAcc) < 0.12f) return;
    proc.stepPreset(wheelAcc > 0 ? -1 : +1); wheelAcc = 0.f;
}
void PresetBar::showList() {
    juce::PopupMenu fac, usr;
    for (int row = 0; row < 7; row++) {
        bool any = false; for (int c = 0; c < 10; c++) if (!view.factory[row * 10 + c].empty()) any = true;
        if (!any && view.namesReady) continue;
        fac.addSectionHeader("ROW " + juce::String(row));
        for (int c = 0; c < 10; c++) {
            const int s = row * 10 + c; const std::string& n = view.factory[s];
            if (n.empty() && view.namesReady) continue;
            fac.addItem(1 + s, juce::String(row) + "." + juce::String(c) + "   " + (n.empty() ? juce::String("...") : juce::String(n)), true, !view.user && view.slot == s);
        }
    }
    for (int row = 0; row < 5; row++) {
        usr.addSectionHeader("ROW " + juce::String(row));
        for (int c = 0; c < 10; c++) {
            const int s = row * 10 + c; const std::string& n = view.reg[s];
            usr.addItem(1001 + s, juce::String(row) + "." + juce::String(c) + "   " + (n.empty() ? juce::String("(empty)") : juce::String(n)), !n.empty(), view.user && view.slot == s);
        }
    }
    juce::PopupMenu m; m.addSubMenu("FACTORY", fac, true, nullptr, !view.user); m.addSubMenu("USER", usr, true, nullptr, view.user);
    juce::Component::SafePointer<PresetBar> self(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(nameArea.getWidth()).withMaximumNumColumns(1).withStandardItemHeight(22), [self](int id) {
        if (!self || id <= 0) return;
        if (id >= 1001) self->proc.loadPreset(true, id - 1001); else self->proc.loadPreset(false, id - 1);
    });
}

// ---- the drawer ----------------------------------------------------------------------------------------------------------------------------------------------

Drawer::Drawer(MoonVerbProcessor& p) : proc(p) {
    for (auto* b : { &loadRoms, &forget, &importBank, &exportBank, &powerCycle, &clearMem }) addAndMakeVisible(*b);
    for (int ch = 1; ch <= 16; ch++) midiChannel.addItem("CHANNEL " + juce::String(ch), ch);
    midiChannel.setSelectedId(1, juce::dontSendNotification);
    for (auto* b : { &omni, &pgmChange, &badges }) addAndMakeVisible(*b);
    addAndMakeVisible(midiChannel);
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
        juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon).withTitle("Clear memory").withMessage("Erase all 50 user presets and the settings, as the unit's CLEAR MEMORY does?").withButton("Clear").withButton("Cancel"),
            [this](int r) { if (r == 1) proc.clearMemory(); });
    };
    midiChannel.onChange = [this]() { if (!updating) proc.setMidiChannel(midiChannel.getSelectedId()); };
    omni.onClick = [this]() { if (!updating) proc.setOmni(omni.getToggleState()); };
    pgmChange.onClick = [this]() { if (!updating) proc.setProgramChange(pgmChange.getToggleState()); };
    pgmChange.setTooltip("MIDI program change n loads user preset n (0-49)");
}
void Drawer::set(const MoonVerbProcessor::View& v) {
    view = v; updating = true;
    if (v.live) { midiChannel.setSelectedId(v.midiChannel, juce::dontSendNotification); omni.setToggleState(v.omni, juce::dontSendNotification); pgmChange.setToggleState(v.pgmChange, juce::dontSendNotification); }
    updating = false; repaint();
}
void Drawer::resized() {
    const float k = (float)getWidth() / 1072.f; auto R = [k](float x, float y, float w, float h) { return juce::Rectangle<float>(x * k, y * k, w * k, h * k).toNearestInt(); };
    loadRoms.setBounds(R(28, 60, 170, 32)); forget.setBounds(R(206, 60, 90, 32));
    midiChannel.setBounds(R(372, 60, 150, 32)); omni.setBounds(R(372, 104, 160, 30)); pgmChange.setBounds(R(372, 140, 200, 30));
    importBank.setBounds(R(612, 60, 150, 32)); exportBank.setBounds(R(612, 104, 150, 32));
    powerCycle.setBounds(R(852, 60, 180, 32)); clearMem.setBounds(R(852, 104, 180, 32));
    badges.setBounds(R(852, 160, 200, 30));
}
void Drawer::paint(juce::Graphics& g) {
    const float k = (float)getWidth() / 1072.f;
    g.setColour(PANEL.withAlpha(0.98f)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.f);
    g.setColour(LINE); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 8.f, 1.f);
    g.addTransform(juce::AffineTransform::scale(k));
    auto head = [&g](const juce::String& t, float x) { g.setColour(ACCENT); g.setFont(sans(12.f, true)); g.drawText(t, juce::Rectangle<float>(x, 26.f, 240.f, 16.f), juce::Justification::centredLeft); };
    head("FIRMWARE IMAGES (YOUR OWN)", 28.f); head("MIDI", 372.f); head("USER PRESETS", 612.f); head("SYSTEM", 852.f);
    const char* names[moonverb::ROLES] = { "U62 MASTER", "U95 SLAVE ", "U67 OPCODE", "U48 PROM  ", "U49 PROM  " };
    g.setFont(mono(11.f, false));
    for (int i = 0; i < moonverb::ROLES; i++) {
        const bool have = !view.romFile[i].empty();
        g.setColour(have ? VFD : INK_DIM.withAlpha(0.6f));
        g.drawText(juce::String(names[i]) + "  " + (have ? juce::String(view.romFile[i]) : juce::String("-")), juce::Rectangle<float>(28.f, 108.f + (float)i * 15.f, 320.f, 14.f), juce::Justification::centredLeft);
    }
    g.setColour(INK_DIM); g.setFont(mono(11.f));
    g.drawText(view.romsOk ? "VERSION " + juce::String(view.family) : (view.status.empty() ? juce::String("NO FIRMWARE") : juce::String(view.status)), juce::Rectangle<float>(28.f, 190.f, 320.f, 14.f), juce::Justification::centredLeft);
    g.setFont(sans(11.f)); g.drawText("Banks are SysEx (.syx), as the unit sends them.", juce::Rectangle<float>(612.f, 148.f, 230.f, 14.f), juce::Justification::centredLeft);
}

// ---- the panel ------------------------------------------------------------------------------------------------------------------------------------------------

Panel::Panel(MoonVerbProcessor& p)
    : proc(p), presets(p), drawer(p), bypassAttach(p.apvts, "bypass", bypass),
      input(juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox), inputAttach(*p.apvts.getParameter("input"), input),
      inAttach(p.apvts, "inpad", inPad), outAttach(p.apvts, "outpad", outPad) {
    addAndMakeVisible(presets); addAndMakeVisible(screen); addAndMakeVisible(lamps);
    for (auto* b : { &store, &bypass, &power, &gear }) addAndMakeVisible(*b);
    bypass.setClickingTogglesState(true); bypass.getProperties().set("lit", (juce::int64)LAMP_RED.getARGB());
    power.getProperties().set("lit", (juce::int64)LAMP_ON.getARGB()); power.onClick = [this]() { proc.setPower(!proc.powered()); };
    gear.setClickingTogglesState(true); gear.onClick = [this]() { drawer.setVisible(gear.getToggleState()); };
    store.setTooltip("Save the running program, as it is now, as a user preset"); store.onClick = [this]() { storeMenu(); };
    input.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true); input.setMouseDragSensitivity(200);
    addAndMakeVisible(input); addAndMakeVisible(inPad); addAndMakeVisible(outPad);
    for (int r = 0; r < MoonVerbProcessor::ROWS; r++) for (int c = 0; c < MoonVerbProcessor::COLS; c++) {
        knobs[r * MoonVerbProcessor::COLS + c].reset(new ParamKnob(p, r, c)); addChildComponent(*knobs[r * MoonVerbProcessor::COLS + c]);
        knobs[r * MoonVerbProcessor::COLS + c]->onSelect = [this](int rr, int cc) { select(rr, cc); };
    }
    addChildComponent(drawer);
    setWantsKeyboardFocus(true);
}

void Panel::select(int r, int c) { selR = r; selC = c; grabKeyboardFocus(); update(view); }

bool Panel::keyPressed(const juce::KeyPress& k) {
    // the selected parameter: up / down = the firmware's own step (shift: ten), left / right = the neighbouring parameter, escape = none
    if (k == juce::KeyPress::escapeKey) { selR = selC = -1; update(view); return true; }
    if (selR < 0) return false;
    const int code = k.getKeyCode(); const bool shift = k.getModifiers().isShiftDown();
    ParamKnob& pk = *knobs[selR * MoonVerbProcessor::COLS + selC];
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) {
        const int dir = code == juce::KeyPress::upKey ? +1 : -1;
        if (pk.rateSync()) proc.stepRateDiv(-dir); else proc.stepCell(selR, selC, dir * (shift ? 10 : 1));
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
        std::vector<int> vis; for (auto& q : knobs) if (q->isVisible()) vis.push_back(q->row * MoonVerbProcessor::COLS + q->col);
        const int cur = selR * MoonVerbProcessor::COLS + selC; size_t at = 0; for (size_t i = 0; i < vis.size(); i++) if (vis[i] == cur) at = i;
        if (!vis.empty()) { at = (at + (code == juce::KeyPress::rightKey ? 1 : vis.size() - 1)) % vis.size(); select(vis[at] / MoonVerbProcessor::COLS, vis[at] % MoonVerbProcessor::COLS); }
        return true;
    }
    return false;
}

void Panel::storeMenu() {
    juce::PopupMenu m; m.addSectionHeader("STORE AS USER PRESET");
    for (int row = 0; row < 5; row++) {
        juce::PopupMenu sub;
        for (int c = 0; c < 10; c++) { const int s = row * 10 + c; sub.addItem(1 + s, juce::String(row) + "." + juce::String(c) + "   " + (view.reg[s].empty() ? juce::String("(empty)") : juce::String(view.reg[s])), true, view.user && view.slot == s); }
        m.addSubMenu("ROW " + juce::String(row), sub);
    }
    juce::Component::SafePointer<Panel> self(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&store), [self](int id) {
        if (!self || id <= 0) return;
        const int slot = id - 1; const std::string old = self->view.reg[slot];
        if (old.empty()) { self->proc.storePreset(slot); return; }
        juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon).withTitle("Replace user preset " + juce::String(slot / 10) + "." + juce::String(slot % 10))
            .withMessage("It holds \"" + juce::String(old) + "\". Replace it with the running program?").withButton("Replace").withButton("Cancel"),
            [self, slot](int r) { if (self && r == 1) self->proc.storePreset(slot); });
    });
}

void Panel::update(const MoonVerbProcessor::View& v) {
    view = v;
    presets.set(v); screen.set(v.live ? v.snap.display : v.status, v.live); lamps.set(v.snap.leds, v.live); drawer.set(v);
    power.setToggleState(proc.powered(), juce::dontSendNotification);
    store.setEnabled(v.live);
    // the grid follows the program: which cells exist decides the layout
    std::string key; for (int r = 0; r < MoonVerbProcessor::ROWS; r++) for (int c = 0; c < MoonVerbProcessor::COLS; c++) key += (v.live && v.snap.valid[r][c]) ? '1' : '0';
    if (key != layoutKey) { layoutKey = key; resized(); repaint(); }
    if (v.loading != paintedLoading || v.live != paintedLive || v.romsOk != paintedRoms) { paintedLoading = v.loading; paintedLive = v.live; paintedRoms = v.romsOk; repaint(); }
    if (selR >= 0 && !(v.live && v.snap.valid[selR][selC])) selR = selC = -1;           // the program changed under it
    for (auto& k : knobs) k->set(v, drawer.badges.getToggleState(), k->row == selR && k->col == selC);
    // the status line: loading, a fault, the parameter last touched, or what is running
    juce::String st;
    if (!v.live) st = v.booting ? "POWERING ON" : juce::String(v.status);
    else if (v.storing) st = "STORING  " + juce::String(v.storingName) + "  AS USER " + juce::String(v.slot / 10) + "." + juce::String(v.slot % 10);
    else if (v.loading) st = "LOADING  " + juce::String(v.presetName());
    else if (v.snap.fault) st = "THE FIRMWARE IS BUSY: AN EDIT DID NOT COME BACK";
    else if (v.snap.touched >= 0 && v.snap.sinceTouch < 4.0) {
        const int r = v.snap.touched / MoonVerbProcessor::COLS, c = v.snap.touched % MoonVerbProcessor::COLS;
        if (r < MoonVerbProcessor::ROWS && v.snap.valid[r][c]) st = juce::String(v.snap.name[r][c]) + "   " + juce::String(v.snap.caption[r][c]);
    } else {
        int n = 0; for (int r = 0; r < MoonVerbProcessor::ROWS; r++) for (int c = 0; c < MoonVerbProcessor::COLS; c++) if (v.snap.valid[r][c]) n++;
        st = slotText(v.user, v.slot) + "    " + juce::String(n) + " PARAMETERS" + (v.modified ? "    EDITED (DOUBLE-CLICK A KNOB TO RESTORE IT)" : "");
    }
    if (st != statusLine) { statusLine = st; repaint(statusArea); }
}

void Panel::resized() {
    const float k = s(); auto R = [k](float x, float y, float w, float h) { return juce::Rectangle<float>(x * k, y * k, w * k, h * k).toNearestInt(); };
    presets.setBounds(R(214, 12, 520, 52));
    store.setBounds(R(752, 20, 76, 36)); bypass.setBounds(R(836, 20, 84, 36)); power.setBounds(R(928, 20, 76, 36)); gear.setBounds(R(1012, 20, 88, 36));
    screen.setBounds(R(24, 84, 500, 58)); statusArea = R(544, 84, 552, 58);
    grid = R(24, 160, 1072, 400);
    drawer.setBounds(R(24, 160, 1072, 236));
    lamps.setBounds(R(24, 590, 150, 40)); input.setBounds(R(196, 580, 56, 56));
    inPad.setBounds(R(330, 594, 160, 28)); outPad.setBounds(R(500, 594, 170, 28));
    // the grid: each firmware row on a line of its own, its cells packed from the left; rows with nothing are skipped
    const int rowsUsed = [this]() { int n = 0; for (int r = 0; r < MoonVerbProcessor::ROWS; r++) { bool any = false; for (int c = 0; c < MoonVerbProcessor::COLS; c++) if (layoutKey.size() == 45 && layoutKey[(size_t)(r * 9 + c)] == '1') any = true; if (any) n++; } return n; }();
    const float rowH = juce::jmin(84.f, 400.f / (float)juce::jmax(1, rowsUsed)), cellW = 1072.f / 9.f;
    int line = 0;
    for (int r = 0; r < MoonVerbProcessor::ROWS; r++) {
        int x = 0;
        for (int c = 0; c < MoonVerbProcessor::COLS; c++) {
            ParamKnob& pk = *knobs[r * MoonVerbProcessor::COLS + c];
            const bool on = layoutKey.size() == 45 && layoutKey[(size_t)(r * 9 + c)] == '1';
            pk.setVisible(on);
            if (on) { pk.setBounds(R(24.f + (float)x * cellW + 4.f, 160.f + (float)line * rowH + 2.f, cellW - 8.f, rowH - 4.f)); x++; }
        }
        if (x > 0) line++;
    }
}

void Panel::paint(juce::Graphics& g) {
    const float k = s();
    g.fillAll(BG);
    g.saveState(); g.addTransform(juce::AffineTransform::scale(k));
    g.setColour(PANEL); g.fillRect(0.f, 0.f, W, 76.f); g.setColour(LINE); g.drawHorizontalLine(76, 0.f, W);
    g.setColour(INK); g.setFont(sans(24.f, true)); g.drawText("moonverb", juce::Rectangle<float>(24.f, 14.f, 180.f, 30.f), juce::Justification::centredLeft);
    g.setColour(INK_DIM); g.setFont(sans(10.f, true)); g.drawText("DIGITAL EFFECTS", juce::Rectangle<float>(24.f, 44.f, 190.f, 14.f), juce::Justification::centredLeft);
    g.setColour(PANEL); g.fillRect(0.f, 572.f, W, H - 572.f); g.setColour(LINE); g.drawHorizontalLine(572, 0.f, W);
    g.setColour(INK_DIM); g.setFont(sans(10.f, true)); g.drawText("INPUT", juce::Rectangle<float>(258.f, 600.f, 60.f, 16.f), juce::Justification::centredLeft);
    g.drawText(view.live ? "V" + juce::String(view.family) + " FIRMWARE" : juce::String(), juce::Rectangle<float>(900.f, 600.f, 196.f, 16.f), juce::Justification::centredRight);
    if (view.live && layoutKey.find('1') == std::string::npos) { g.setColour(INK_DIM); g.setFont(sans(14.f)); g.drawText(view.loading ? "LOADING..." : "READING THE PROGRAM'S PARAMETERS...", juce::Rectangle<float>(24.f, 160.f, 1072.f, 400.f), juce::Justification::centred); }
    if (!view.live) { g.setColour(INK_DIM); g.setFont(sans(15.f)); g.drawText(view.romsOk ? (view.booting ? "POWERING ON..." : juce::String(view.status)) : "Open SETTINGS and LOAD ROM FOLDER: MoonVerb runs your own firmware images.", juce::Rectangle<float>(24.f, 160.f, 1072.f, 400.f), juce::Justification::centred); }
    g.restoreState();
    g.setColour(INK); g.setFont(sans(15.f * k, true));
    g.drawFittedText(statusLine, statusArea.reduced((int)(12 * k), 0), juce::Justification::centredLeft, 2);
}

}
