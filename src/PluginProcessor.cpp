#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

using namespace moonverb;

namespace {
std::vector<uint8_t> readAll(const juce::File& f) {
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) return {};
    return std::vector<uint8_t>((const uint8_t*)mb.getData(), (const uint8_t*)mb.getData() + mb.getSize());
}
juce::String cellId(int r, int c) { return "p" + juce::String(r) + juce::String(c); }
}

juce::AudioProcessorValueTreeState::ParameterLayout MoonVerbProcessor::createLayout(MoonVerbProcessor* self) {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{ "input", 1 }, "Input level", NormalisableRange<float>(0.f, 1.f), 1.f,
        AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return String(v * 100.f, 0) + " %"; })));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{ "inpad", 1 }, "Main input level switch", StringArray{ "+4 dBu", "-20 dBV" }, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{ "outpad", 1 }, "Output level switch", StringArray{ "+4 dBu", "-20 dBV" }, 0));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID{ "bypass", 1 }, "Bypass", false));
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
        // Names and values are the firmware's own, whatever program is running: the host shows "<name> <value>" while a program is loaded.
        auto text = [self, r, c](float v, int) -> String {
            if (self) {
                std::unique_lock<std::mutex> lock(self->snapMutex, std::try_to_lock);
                if (lock && self->snap.valid[r][c]) {
                    const int span = self->snap.hi[r][c] - self->snap.lo[r][c];
                    const int w = self->snap.lo[r][c] + (int)std::lround(v * span);
                    if (w == self->snap.word[r][c] && !self->snap.caption[r][c].empty()) return String(self->snap.name[r][c]) + " " + String(self->snap.caption[r][c]);
                    return String(self->snap.name[r][c]);
                }
            }
            return String(v * 100.f, 0) + " %";
        };
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{ cellId(r, c), 1 }, "Parameter " + String(r) + "." + String(c), NormalisableRange<float>(0.f, 1.f), 0.5f,
            AudioParameterFloatAttributes().withStringFromValueFunction(text)));
    }
    return layout;
}

MoonVerbProcessor::MoonVerbProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "MOONVERB", createLayout(this)) {
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
        cellRaw[r * COLS + c] = apvts.getRawParameterValue(cellId(r, c));
        cellParam[r * COLS + c] = apvts.getParameter(cellId(r, c));
        follow[r * COLS + c] = 0.f;
    }
    bypassRaw = apvts.getRawParameterValue("bypass"); inputRaw = apvts.getRawParameterValue("input"); inPadRaw = apvts.getRawParameterValue("inpad"); outPadRaw = apvts.getRawParameterValue("outpad");
    bypassParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("bypass"));
    // ROMs the last session used: a new instance finds them by itself
    const juce::File sf = settingsFile();
    if (sf.existsAsFile()) {
        const juce::var j = juce::JSON::parse(sf);
        std::vector<juce::File> files;
        if (const juce::Array<juce::var>* a = j["romPaths"].getArray()) for (const juce::var& p : *a) files.push_back(juce::File(p.toString()));
        if (!files.empty()) loadFiles(files);
    }
    startTimerHz(30);
}

MoonVerbProcessor::~MoonVerbProcessor() {
    stopTimer();
    if (bootThread.joinable()) bootThread.join();
    delete handover.exchange(nullptr);
}

bool MoonVerbProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo() || in.isDisabled();
}

juce::File MoonVerbProcessor::settingsFile() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Moon Technologies").getChildFile("MoonVerb").getChildFile("settings.json");
}

// ---- ROMs and booting ----------------------------------------------------------------------------------------------------------------------------------

void MoonVerbProcessor::loadFiles(const std::vector<juce::File>& files) {
    std::vector<Candidate> cands;
    for (const juce::File& f : files) { std::vector<uint8_t> b = readAll(f); if (!b.empty()) cands.push_back({ f.getFullPathName().toStdString(), std::move(b) }); }
    RomSet s = assemble(cands);
    {
        std::lock_guard<std::mutex> lock(romMutex);
        roms = std::move(s);
        status = statusLine(roms);
    }
    if (prepared) boot(hostRate);
}

void MoonVerbProcessor::loadRomFolder(const juce::File& dir) {
    std::vector<juce::File> files;
    for (const juce::DirectoryEntry& e : juce::RangedDirectoryIterator(dir, true, "*", juce::File::findFiles)) files.push_back(e.getFile());
    loadFiles(files);
}

void MoonVerbProcessor::forgetRoms() {
    if (bootThread.joinable()) bootThread.join();
    restartRequested = true;
    { std::lock_guard<std::mutex> lock(romMutex); roms = RomSet(); status = "LOAD ROMS"; }
    rememberRoms();
}

void MoonVerbProcessor::boot(double rate) {
    RomSet set;
    {
        std::lock_guard<std::mutex> lock(romMutex);
        if (!roms.ok() || !roms.have(U67) || !roms.have(U48) || !roms.have(U49)) return;
        set = roms; status = "POWERING ON";
    }
    if (!power.load()) { std::lock_guard<std::mutex> lock(romMutex); status = "POWER OFF"; return; }
    if (bootThread.joinable()) bootThread.join();
    std::vector<uint8_t> ram;
    { std::lock_guard<std::mutex> lock(snapMutex); ram = battery; family = set.family; }
    booting = true;
    bootRate = rate;
    bootThread = std::thread([this, set, ram, rate]() {
        std::unique_ptr<Voice> v(new Voice());
        if (ram.size() == 0x2000) v->setBatteryRam(ram.data());
        v->load(set.data[U62].data(), set.data[U62].size(), set.data[U95].data(), set.data[U95].size(), set.data[U67].data(), set.data[U48].data(), set.data[U49].data(), set.family == "3.01");
        v->configure(rate);
        double l, r;
        const long long n = (long long)(9.6 * rate);
        for (long long i = 0; i < n; i++) v->process(0.0, l, r);        // the power-up routine runs against silence
        voiceRate = rate;
        delete handover.exchange(v.release());
        { std::lock_guard<std::mutex> lock(romMutex); status.clear(); }
        rememberRoms();
        booting = false;
    });
}

void MoonVerbProcessor::rememberRoms() {
    juce::Array<juce::var> paths;
    { std::lock_guard<std::mutex> lock(romMutex); for (int r = 0; r < ROLES; r++) if (roms.have(r)) paths.add(juce::String(roms.path[r])); }
    const juce::File f = settingsFile();
    f.getParentDirectory().createDirectory();
    auto* o = new juce::DynamicObject(); o->setProperty("romPaths", paths);      // paths only, never the images
    f.replaceWithText(juce::JSON::toString(juce::var(o)));
}

void MoonVerbProcessor::prepareToPlay(double sampleRate, int) {
    hostRate = sampleRate; prepared = true;
    if ((booting.load() || handover.load()) && std::fabs(bootRate.load() - sampleRate) > 1.0) {      // powering up for another rate: start again
        if (bootThread.joinable()) bootThread.join();
        delete handover.exchange(nullptr);
    }
    if (voice && std::fabs(sampleRate - voiceRate) > 1.0) {                // the converter stages are rebuilt for the new rate, the RAM is kept
        { std::lock_guard<std::mutex> lock(snapMutex); battery.assign(voice->batteryRam(), voice->batteryRam() + 0x2000); }
        voice.reset();
    }
    if (!voice && !booting.load() && !handover.load() && power.load()) boot(sampleRate);
}

void MoonVerbProcessor::powerCycle() { restartRequested = true; boot(hostRate); }
void MoonVerbProcessor::clearMemory() { { std::lock_guard<std::mutex> lock(snapMutex); battery.clear(); } restartRequested = true; boot(hostRate); }
void MoonVerbProcessor::setPower(bool on) {
    power = on;
    if (on) boot(hostRate);
    else { restartRequested = true; std::lock_guard<std::mutex> lock(romMutex); status = "POWER OFF"; }
}

std::vector<uint8_t> MoonVerbProcessor::batteryCopy() { std::lock_guard<std::mutex> lock(snapMutex); return battery; }

// ---- the front panel's hands --------------------------------------------------------------------------------------------------------------------------

void MoonVerbProcessor::ask(std::function<void(Voice&)> f) {
    std::lock_guard<std::mutex> lock(cmdMutex);
    cmds.push_back(std::move(f)); cmdPending = true;
}
void MoonVerbProcessor::pressKey(Keys::Key k, bool down) { ask([k, down](Voice& v) { v.keys.setKey(k, down); }); }
void MoonVerbProcessor::softKnob(int delta) {
    ask([delta](Voice& v) {
        if (v.keys.mode() != 1 || !v.control.tableReady()) return;                // the knob edits what the display shows, in PARAM mode
        const int row = v.machine.mram[Keys::ROW - 0x8000], col = v.machine.mram[Keys::COL - 0x8000];
        if (row < Control::ROWS && col < Control::COLS) v.control.editNow(row, col, delta);
    });
}
void MoonVerbProcessor::setMidiChannel(int ch) { ask([ch](Voice& v) { v.control.setMidiChannel(ch); v.midi.channel = ch - 1; }); }
void MoonVerbProcessor::setOmni(bool on) { ask([on](Voice& v) { v.control.setOmni(on); }); }
void MoonVerbProcessor::setProgramChange(bool on) { ask([on](Voice& v) { v.control.setProgramChange(on); }); }

void MoonVerbProcessor::importBank(const juce::File& f) {
    const std::vector<uint8_t> bytes = readAll(f);
    std::vector<std::vector<uint8_t>> msgs; std::vector<uint8_t> cur;
    for (uint8_t b : bytes) { if (b == 0 && cur.empty()) continue; cur.push_back(b); if (b == 0xF7) { msgs.push_back(cur); cur.clear(); } }
    ask([msgs](Voice& v) { for (const auto& g : msgs) v.midi.sysex(g); });
}

bool MoonVerbProcessor::exportBank(const juce::File& f) {
    const std::vector<uint8_t> ram = batteryCopy();
    if (ram.size() != 0x2000) return false;
    std::vector<uint8_t> out;
    for (int n = 0; n < 50; n++) {
        uint8_t data[167]; batram::get(ram.data(), n, data);
        if (data[0] == 0) continue;                                           // unused register
        const std::vector<uint8_t> msg = Midi::bulk(data, n, true); out.insert(out.end(), msg.begin(), msg.end());
    }
    return f.replaceWithData(out.data(), out.size());
}

MoonVerbProcessor::View MoonVerbProcessor::view() {
    View v;
    { std::lock_guard<std::mutex> lock(snapMutex); v.snap = snap; v.family = family; if (battery.size() == 0x2000) { v.midiChannel = (battery[0x98B2 - 0x8000] & 15) + 1; v.omni = battery[0x98B3 - 0x8000] != 0; v.pgmChange = battery[0x98B8 - 0x8000] != 0; } }
    { std::lock_guard<std::mutex> lock(romMutex); v.status = status; v.romsOk = roms.ok(); for (int r = 0; r < ROLES; r++) v.romFile[r] = roms.have(r) ? juce::File(roms.path[r]).getFileName().toStdString() : std::string(); }
    v.booting = booting.load(); v.live = v.status.empty() && power.load();
    return v;
}

juce::AudioProcessorEditor* MoonVerbProcessor::createEditor() { return new MoonVerbEditor(*this); }

// ---- the message-thread half of what the audio thread cannot do -----------------------------------------------------------------------------------------

void MoonVerbProcessor::timerCallback() {
    if (followPending.exchange(false))
        for (int i = 0; i < ROWS * COLS; i++) {
            const float v = follow[i].exchange(-1.f);
            if (v >= 0.f && cellParam[i]) cellParam[i]->setValueNotifyingHost(v);
        }
    const int b = bypassFollow.exchange(-1);
    if (b >= 0 && bypassParam) bypassParam->setValueNotifyingHost(b ? 1.f : 0.f);
}

// ---- audio ----------------------------------------------------------------------------------------------------------------------------------------------

void MoonVerbProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    if (restartRequested.exchange(false)) voice.reset();
    if (voice && std::fabs(hostRate - voiceRate) > 1.0) {
        { std::lock_guard<std::mutex> lock(snapMutex); battery.assign(voice->batteryRam(), voice->batteryRam() + 0x2000); }
        voice.reset();
        boot(hostRate);
    }
    if (!voice) {
        if (Voice* h = handover.exchange(nullptr)) {
            if (std::fabs(h->hostRate() - hostRate) > 1.0) delete h;           // (prepareToPlay restarts the power-up on a rate change, so this is only a guard)
            else { voice.reset(h); ctlCount = 0; bypassInit = false; lastFwBypass = -1; clockFromMidi = false; wasPlaying = false; }
        }
    }
    if (!voice || !power.load()) { buffer.clear(); midi.clear(); return; }
    Voice& v = *voice;
    if (cmdPending.load(std::memory_order_relaxed)) {
        std::unique_lock<std::mutex> lock(cmdMutex, std::try_to_lock);
        if (lock) { std::vector<std::function<void(Voice&)>> run; run.swap(cmds); cmdPending = false; lock.unlock(); for (auto& f : run) f(v); }
    }
    // ---- MIDI in: everything the firmware's own MIDI port understands ----
    const bool omni = v.machine.mram[batram::OMNI - 0x8000] != 0; const int fwCh = v.machine.mram[batram::MIDI_CHAN - 0x8000] & 15; v.midi.channel = fwCh;
    for (const juce::MidiMessageMetadata meta : midi) {
        const juce::MidiMessage m = meta.getMessage();
        if (m.isSysEx()) { v.midi.sysex(std::vector<uint8_t>(m.getRawData(), m.getRawData() + m.getRawDataSize())); continue; }
        if (m.isMidiClock()) { clockFromMidi = true; v.midi.clockEdge(24); continue; }
        if (m.isMidiStart() || m.isMidiContinue()) { clockFromMidi = true; v.midi.clockStart(); continue; }
        if (m.getChannel() == 0) continue;
        if (!omni && m.getChannel() - 1 != fwCh) continue;
        if (m.isNoteOn()) v.midi.note(m.getNoteNumber(), m.getVelocity());
        else if (m.isNoteOff()) v.midi.noteOff(m.getNoteNumber());
        else if (m.isController()) v.midi.cc(m.getControllerNumber(), m.getControllerValue());
        else if (m.isPitchWheel()) v.midi.pitchBend(m.getPitchWheelValue());
        else if (m.isChannelPressure()) v.midi.aftertouch(m.getChannelPressureValue());
        else if (m.isAftertouch()) v.midi.aftertouch(m.getAfterTouchValue());
        else if (m.isProgramChange()) v.midi.programChange(m.getProgramChangeNumber());
    }
    midi.clear();
    // ---- the host's transport as the MIDI clock the V3 firmware measures (24 pulses per quarter note) ----
    std::vector<int> clockAt;
    if (!clockFromMidi) {
        if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) {
            const bool playing = pos->getIsPlaying();
            if (playing && !wasPlaying) { v.midi.clockStart(); nextClockPpq = std::ceil(pos->getPpqPosition().orFallback(0.0) * 24.0) / 24.0; }
            wasPlaying = playing;
            if (playing) if (auto bpm = pos->getBpm()) {
                const double ppq0 = pos->getPpqPosition().orFallback(nextClockPpq), perSample = *bpm / 60.0 / hostRate;
                if (ppq0 < nextClockPpq - 1.0) nextClockPpq = std::ceil(ppq0 * 24.0) / 24.0;       // the transport jumped back
                while (nextClockPpq < ppq0 + n * perSample) { const int s = (int)std::ceil((nextClockPpq - ppq0) / perSample); if (s >= 0 && s < n) clockAt.push_back(s); nextClockPpq += 1.0 / 24.0; }
            }
        }
    }
    size_t ce = 0;
    float* ch0 = buffer.getWritePointer(0); float* ch1 = nch > 1 ? buffer.getWritePointer(1) : nullptr;
    const int ctlPeriod = std::max(1, (int)(hostRate / 1000.0));
    for (int i = 0; i < n; i++) {
        while (ce < clockAt.size() && clockAt[ce] <= i) { v.midi.clockEdge(24); ce++; }
        if (--ctlCount <= 0) {
            ctlCount = ctlPeriod;
            PanelLogic::In in; in.dt = ctlPeriod / hostRate;
            for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) in.knob[r][c] = cellRaw[r * COLS + c]->load();
            in.input = inputRaw->load(); in.inPad20 = inPadRaw->load() > 0.5f; in.outPad20 = outPadRaw->load() > 0.5f; in.trimVolts = 1.f;     // one volt = full scale: samples are volts
            PanelLogic::Out out;
            logic.control(v, in, out);
            for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) if (out.setKnob[r][c]) { follow[r * COLS + c] = out.knob[r][c]; followPending = true; }
            // BYPASS: the host's switch and the unit's own key are one thing
            const float bp = bypassRaw->load(); const int fw = v.control.bypassed() ? 1 : 0;
            if (!bypassInit) { bypassInit = true; lastBypassParam = bp; lastFwBypass = fw; if ((bp > 0.5f) != (fw != 0)) bypassFollow = fw; if ((bp > 0.5f) != (fw != 0)) lastBypassParam = (float)fw; }
            else {
                if (bp != lastBypassParam) { lastBypassParam = bp; if ((bp > 0.5f) != (fw != 0)) v.keys.bypassTap(); }
                if (fw != lastFwBypass) { lastFwBypass = fw; if ((bp > 0.5f) != (fw != 0)) { lastBypassParam = (float)fw; bypassFollow = fw; } }
            }
        }
        const float l0 = ch0[i], r0 = ch1 ? ch1[i] : l0;
        double l, r; v.process(ch1 ? 0.5 * ((double)l0 + (double)r0) : (double)l0, l, r);       // the unit has one input: a stereo signal is summed
        if (ch1) { ch0[i] = (float)l; ch1[i] = (float)r; } else ch0[i] = (float)(0.5 * (l + r));
    }
    for (int c = 2; c < nch; c++) buffer.clear(c, 0, n);
    // what the panel shows, and the RAM image for the next session
    snapCount -= n;
    if (snapCount <= 0) {
        snapCount = std::max(1, (int)(hostRate / 50.0));
        std::unique_lock<std::mutex> lock(snapMutex, std::try_to_lock);
        if (lock) { logic.snapshot(v, snap); if (--ramCount <= 0) { ramCount = 50; battery.assign(v.batteryRam(), v.batteryRam() + 0x2000); } }
    }
    const int lat = v.latencySamples(); if (lat != getLatencySamples()) setLatencySamples(lat);
}

// ---- state --------------------------------------------------------------------------------------------------------------------------------------------

void MoonVerbProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::ValueTree state = apvts.copyState();
    juce::StringArray paths;
    { std::lock_guard<std::mutex> lock(romMutex); for (int r = 0; r < ROLES; r++) if (roms.have(r)) paths.add(juce::String(roms.path[r])); }
    state.setProperty("romPaths", paths.joinIntoString("\n"), nullptr);          // paths only, never the images
    state.setProperty("power", power.load(), nullptr);
    {
        std::lock_guard<std::mutex> lock(snapMutex);
        if (battery.size() == 0x2000) state.setProperty("battery", juce::Base64::toBase64(battery.data(), battery.size()), nullptr);      // the user's own registers and settings: no firmware data
    }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void MoonVerbProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;
    const juce::ValueTree state = juce::ValueTree::fromXml(*xml);
    apvts.replaceState(state);
    power = state.getProperty("power", true);
    const juce::String b64 = state.getProperty("battery").toString();
    if (b64.isNotEmpty()) {
        juce::MemoryOutputStream mo; juce::Base64::convertFromBase64(mo, b64);
        if (mo.getDataSize() == 0x2000) { std::lock_guard<std::mutex> lock(snapMutex); battery.assign((const uint8_t*)mo.getData(), (const uint8_t*)mo.getData() + 0x2000); }
        restartRequested = true;                                                  // boot on that RAM image
    }
    juce::StringArray paths; paths.addLines(state.getProperty("romPaths").toString());
    if (paths.size() > 0) { std::vector<juce::File> files; for (const auto& p : paths) files.push_back(juce::File(p)); loadFiles(files); }
    else if (b64.isNotEmpty()) boot(hostRate);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MoonVerbProcessor(); }
