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
        follow[r * COLS + c] = -1.f; followTarget[r * COLS + c] = -1.f; followFrom[r * COLS + c] = 0.f;
    }
    bypassRaw = apvts.getRawParameterValue("bypass"); inputRaw = apvts.getRawParameterValue("input"); inPadRaw = apvts.getRawParameterValue("inpad"); outPadRaw = apvts.getRawParameterValue("outpad");
    bypassParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("bypass"));
    // ROMs the last session used: a new instance finds them by itself
    {
        const juce::var j = readSettings();
        std::vector<juce::File> files;
        if (const juce::Array<juce::var>* a = j["romPaths"].getArray()) for (const juce::var& p : *a) files.push_back(juce::File(p.toString()));
        if (!files.empty()) loadFiles(files);
    }
    startTimerHz(30);
}

MoonVerbProcessor::~MoonVerbProcessor() {
    stopTimer();
    cancelNames = true; if (namesThread.joinable()) namesThread.join();
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

namespace { std::mutex settingsMutex; }
juce::var MoonVerbProcessor::readSettings() const {
    std::lock_guard<std::mutex> lock(settingsMutex);
    const juce::File f = settingsFile();
    if (!f.existsAsFile()) return juce::var(new juce::DynamicObject());
    juce::var j = juce::JSON::parse(f);
    return j.getDynamicObject() ? j : juce::var(new juce::DynamicObject());
}
void MoonVerbProcessor::writeSettings(const juce::String& key, const juce::var& value) {
    juce::var j = readSettings();
    std::lock_guard<std::mutex> lock(settingsMutex);
    j.getDynamicObject()->setProperty(juce::Identifier(key), value);
    const juce::File f = settingsFile(); f.getParentDirectory().createDirectory();
    f.replaceWithText(juce::JSON::toString(j));
}

// ---- factory program names ----------------------------------------------------------------------------------------------------------------------------

juce::String MoonVerbProcessor::romKey(const RomSet& set) {
    return juce::String(sha256hex(set.data[U62].data(), set.data[U62].size()).substr(0, 16) + sha256hex(set.data[U95].data(), set.data[U95].size()).substr(0, 16));
}

void MoonVerbProcessor::startNames(const RomSet& set) {
    const juce::String key = romKey(set);
    if (key == namesKey) return;
    cancelNames = true; if (namesThread.joinable()) namesThread.join(); cancelNames = false;
    namesKey = key;
    { std::lock_guard<std::mutex> lock(snapMutex); names = ProgramNames(); }
    // read before: the names are the firmware's own, so they are the same every time for these images
    const juce::var cache = readSettings()["programNames"][juce::Identifier(key)];
    if (const juce::Array<juce::var>* a = cache.getArray()) if (a->size() == ProgramNames::SLOTS) {
        ProgramNames n; for (int i = 0; i < ProgramNames::SLOTS; i++) n.name[i] = (*a)[i].toString().toStdString(); n.done = true;
        std::lock_guard<std::mutex> lock(snapMutex); names = n; return;
    }
    namesThread = std::thread([this, set, key]() {
        const ProgramNames n = ProgramNames::harvest(set.data[U62], set.data[U95], set.data[U67], set.data[U48], set.data[U49], set.family == "3.01", cancelNames);
        if (!n.done) return;
        { std::lock_guard<std::mutex> lock(snapMutex); names = n; }
        juce::Array<juce::var> arr; for (int i = 0; i < ProgramNames::SLOTS; i++) arr.add(juce::String(n.name[i]));
        juce::var all = readSettings()["programNames"]; if (!all.getDynamicObject()) all = juce::var(new juce::DynamicObject());
        all.getDynamicObject()->setProperty(juce::Identifier(key), arr);
        writeSettings("programNames", all);
    });
}

// ---- ROMs and booting ----------------------------------------------------------------------------------------------------------------------------------

void MoonVerbProcessor::loadFiles(const std::vector<juce::File>& files) {
    std::vector<Candidate> cands;
    for (const juce::File& f : files) { std::vector<uint8_t> b = readAll(f); if (!b.empty()) cands.push_back({ f.getFullPathName().toStdString(), std::move(b) }); }
    RomSet s = assemble(cands);
    RomSet set;
    {
        std::lock_guard<std::mutex> lock(romMutex);
        roms = std::move(s);
        status = statusLine(roms);
        if (roms.ok()) set = roms;
    }
    if (set.ok()) startNames(set);
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
    cancelNames = true; if (namesThread.joinable()) namesThread.join(); cancelNames = false; namesKey.clear();
    { std::lock_guard<std::mutex> lock(snapMutex); names = ProgramNames(); }
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
    const uint64_t gen = ++bootGen;
    bootThread = std::thread([this, set, ram, rate, gen]() {
        std::unique_ptr<Voice> v(new Voice());
        if (ram.size() == 0x2000) v->setBatteryRam(ram.data());
        v->load(set.data[U62].data(), set.data[U62].size(), set.data[U95].data(), set.data[U95].size(), set.data[U67].data(), set.data[U48].data(), set.data[U49].data(), set.family == "3.01");
        v->configure(rate);
        double l, r;
        const long long n = (long long)(9.6 * rate);
        for (long long i = 0; i < n; i++) v->process(0.0, l, r);        // the power-up routine runs against silence
        if (gen != bootGen.load()) return;                                // a newer power-up was asked for meanwhile: this one is stale
        voiceRate = rate;
        handoverGen = gen;
        delete handover.exchange(v.release());
        { std::lock_guard<std::mutex> lock(romMutex); status.clear(); }
        rememberRoms();
        booting = false;
    });
}

void MoonVerbProcessor::rememberRoms() {
    juce::Array<juce::var> paths;
    { std::lock_guard<std::mutex> lock(romMutex); for (int r = 0; r < ROLES; r++) if (roms.have(r)) paths.add(juce::String(roms.path[r])); }
    writeSettings("romPaths", paths);                                            // paths only, never the images
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

void MoonVerbProcessor::stopMachine(bool keep) {
    keepRam = keep; const int n = restartsDone.load(); restartRequested = true;
    // the RAM the next power-up starts from is the machine's as it is now, not the copy of up to a second ago (a setting changed just before would be lost)
    for (int i = 0; i < 150 && prepared && restartsDone.load() == n; i++) juce::Thread::sleep(2);
}
void MoonVerbProcessor::powerCycle() { stopMachine(true); boot(hostRate); }
void MoonVerbProcessor::clearMemory() { stopMachine(false); { std::lock_guard<std::mutex> lock(snapMutex); battery.clear(); presetUser = false; presetSlot = 0; baseValid = false; } boot(hostRate); }
void MoonVerbProcessor::setPower(bool on) {
    power = on;
    if (on) boot(hostRate);
    else { stopMachine(true); std::lock_guard<std::mutex> lock(romMutex); status = "POWER OFF"; }
}

std::vector<uint8_t> MoonVerbProcessor::batteryCopy() { std::lock_guard<std::mutex> lock(snapMutex); return battery; }

// ---- the front panel's hands --------------------------------------------------------------------------------------------------------------------------

void MoonVerbProcessor::ask(std::function<void(Voice&)> f) {
    std::lock_guard<std::mutex> lock(cmdMutex);
    cmds.push_back(std::move(f)); cmdPending = true;
}
void MoonVerbProcessor::pressKey(Keys::Key k, bool down) { ask([k, down](Voice& v) { v.keys.setKey(k, down); }); }
void MoonVerbProcessor::store() { ask([](Voice& v) { v.keys.storeGesture(); }); }
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

// ---- presets ----------------------------------------------------------------------------------------------------------------------------------------------

void MoonVerbProcessor::loadPreset(bool user, int slot) {
    if (slot < 0 || slot >= (user ? USER_SLOTS : FACTORY_SLOTS)) return;
    {
        std::lock_guard<std::mutex> lock(snapMutex);
        if (user && (battery.size() != 0x2000 || !PanelLogic::registerUsed(battery.data(), slot))) return;      // nothing stored there
        if (!user && names.done && names.name[slot].empty()) return;                                            // no program in that slot
        presetUser = user; presetSlot = slot; baseValid = false;
    }
    loading = true;
    // the firmware loads it itself: its own program-change routine, called as its LOAD key and MIDI do (it ends in PGM / REG mode on that slot, as after LOAD)
    ask([this, user, slot](Voice& v) {
        loadsAt = v.control.loadsDone(); loadByCall = true;
        if (user) v.control.loadRegister(slot); else v.control.loadProgram(slot / 10, slot % 10);
        loadWait = true; loadQuiet = loadAge = 0.0; loadSeen = false; loadTarget = std::to_string(slot / 10) + "." + std::to_string(slot % 10);
    });
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
}

std::vector<int> MoonVerbProcessor::presetOrder(const View& v) const {
    std::vector<int> o;
    for (int i = 0; i < FACTORY_SLOTS; i++) if (!v.namesReady || !v.factory[i].empty()) o.push_back(i);
    for (int i = 0; i < USER_SLOTS; i++) if (!v.reg[i].empty()) o.push_back(100 + i);
    return o;
}

void MoonVerbProcessor::stepPreset(int dir) {
    const View v = view(); const std::vector<int> o = presetOrder(v);
    if (o.empty()) return;
    const int cur = (v.user ? 100 : 0) + v.slot;
    int at = -1; for (size_t i = 0; i < o.size(); i++) if (o[i] == cur) at = (int)i;
    if (at < 0) {                                                                // the running one is not in the list (emptied): the nearest in that direction
        at = dir > 0 ? -1 : (int)o.size(); for (size_t i = 0; i < o.size(); i++) if (o[i] < cur) at = (int)i;
        if (dir > 0 && at < (int)o.size() - 1) { at++; dir = 0; }
    }
    const int n = (int)o.size(); const int next = o[(size_t)(((at + dir) % n + n) % n)];
    loadPreset(next >= 100, next % 100);
}

void MoonVerbProcessor::storePreset(int slot) {
    if (slot < 0 || slot >= USER_SLOTS) return;
    const std::string running = view().presetName();
    { std::lock_guard<std::mutex> lock(snapMutex); storingName = running; presetUser = true; presetSlot = slot; baseValid = false; }
    storing = true; loading = true;
    ask([this, slot](Voice& v) { v.keys.storeRegister(slot / 10, slot % 10); loadByCall = false; loadWait = true; loadQuiet = loadAge = 0.0; loadSeen = false; loadTarget = std::to_string(slot / 10) + "." + std::to_string(slot % 10); });
    updateHostDisplay(ChangeDetails().withProgramChanged(true));
}

void MoonVerbProcessor::stepCell(int r, int c, int delta) {
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS || !delta) return;
    ask([r, c, delta](Voice& v) { if (v.control.tableReady() && v.control.cell(r, c).editable()) v.control.editNow(r, c, delta); });      // the firmware's own step, clamped by it
}

// ---- RATE (the BPM programs) ----------------------------------------------------------------------------------------------------------------------------

namespace { struct Div { const char* name; double quarters; };
const Div DIVS[] = { { "1/1", 4.0 }, { "1/2.", 3.0 }, { "1/1T", 8.0 / 3 }, { "1/2", 2.0 }, { "1/4.", 1.5 }, { "1/2T", 4.0 / 3 }, { "1/4", 1.0 }, { "1/8.", 0.75 }, { "1/4T", 2.0 / 3 },
                     { "1/8", 0.5 }, { "1/16.", 0.375 }, { "1/8T", 1.0 / 3 }, { "1/16", 0.25 }, { "1/16T", 1.0 / 6 }, { "1/32", 0.125 } }; }
int MoonVerbProcessor::rateDivs() { return (int)(sizeof DIVS / sizeof DIVS[0]); }
const char* MoonVerbProcessor::rateDivName(int i) { return DIVS[juce::jlimit(0, rateDivs() - 1, i)].name; }
double MoonVerbProcessor::rateBpm(double host, int div) { return host / DIVS[juce::jlimit(0, rateDivs() - 1, div)].quarters; }
bool MoonVerbProcessor::rateReachable(double host, int div) { const double b = rateBpm(host, div); return b >= 63.5 && b <= 191.5; }
int MoonVerbProcessor::rateWord(double host, int div) { return juce::jlimit(448, 575, (int)std::lround(rateBpm(host, div)) + 384); }
bool MoonVerbProcessor::isRateText(const char* t) { const std::string s(t, 13); return s.compare(0, 4, "RATE") == 0 && s.find("BPM") != std::string::npos; }
void MoonVerbProcessor::setRateMode(int m) { rateMode = m == RATE_SYNC ? RATE_SYNC : RATE_HZ; }
void MoonVerbProcessor::stepRateDiv(int dir) {
    // the next note value whose beat the firmware can play (64..191 BPM) at the host's tempo
    const double b = hostBpm.load(); int d = rateDiv.load();
    for (int k = 0; k < rateDivs(); k++) { d += dir > 0 ? 1 : -1; if (d < 0 || d >= rateDivs()) return; if (rateReachable(b, d)) { rateDiv = d; return; } }
}

void MoonVerbProcessor::resetCell(int r, int c) {
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return;
    const View v = view();
    if (!v.baseValid || !v.snap.valid[r][c]) return;
    const int span = v.snap.hi[r][c] - v.snap.lo[r][c]; if (span <= 0) return;
    const float n = juce::jlimit(0.f, 1.f, (float)(v.base[r][c] - v.snap.lo[r][c]) / (float)span);
    if (auto* p = cellParam[r * COLS + c]) { p->beginChangeGesture(); p->setValueNotifyingHost(n); p->endChangeGesture(); }
}

int MoonVerbProcessor::getCurrentProgram() { std::lock_guard<std::mutex> lock(snapMutex); return (presetUser ? FACTORY_SLOTS : 0) + presetSlot; }
void MoonVerbProcessor::setCurrentProgram(int i) {
    if (i < 0 || i >= FACTORY_SLOTS + USER_SLOTS || i == getCurrentProgram()) return;
    loadPreset(i >= FACTORY_SLOTS, i >= FACTORY_SLOTS ? i - FACTORY_SLOTS : i);
}
const juce::String MoonVerbProcessor::getProgramName(int i) {
    if (i < 0 || i >= FACTORY_SLOTS + USER_SLOTS) return {};
    std::lock_guard<std::mutex> lock(snapMutex);
    const bool user = i >= FACTORY_SLOTS; const int slot = user ? i - FACTORY_SLOTS : i;
    const std::string nm = user ? (battery.size() == 0x2000 ? PanelLogic::registerName(battery.data(), slot) : std::string()) : names.name[slot];
    const juce::String where = juce::String(user ? "USER " : "") + juce::String(slot / 10) + "." + juce::String(slot % 10);
    return where + "  " + (nm.empty() ? juce::String(user || names.done ? "(empty)" : "...") : juce::String(nm));
}

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
    {
        std::lock_guard<std::mutex> lock(snapMutex); v.snap = snap; v.family = family;
        if (battery.size() == 0x2000) {
            v.midiChannel = (battery[0x98B2 - 0x8000] & 15) + 1; v.omni = battery[0x98B3 - 0x8000] != 0; v.pgmChange = battery[0x98B8 - 0x8000] != 0;
            for (int i = 0; i < USER_SLOTS; i++) v.reg[i] = PanelLogic::registerName(battery.data(), i);
        }
        v.namesReady = names.done; for (int i = 0; i < FACTORY_SLOTS; i++) v.factory[i] = names.name[i];
        v.user = presetUser; v.slot = presetSlot; v.baseValid = baseValid;
        for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) { v.base[r][c] = baseWord[r][c]; if (baseValid && snap.valid[r][c] && snap.word[r][c] != baseWord[r][c]) v.modified = true; }
    }
    v.rateMode = rateMode.load(); v.rateDiv = rateDiv.load(); v.hostBpm = hostBpm.load(); v.rateRow = rateRow; v.rateCol = rateCol;
    v.loading = loading.load(); v.storing = storing.load(); if (v.storing) { std::lock_guard<std::mutex> lock(snapMutex); v.storingName = storingName; }
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
    if (restartRequested.exchange(false)) {
        if (voice && keepRam.exchange(true)) { std::lock_guard<std::mutex> lock(snapMutex); battery.assign(voice->batteryRam(), voice->batteryRam() + 0x2000); }
        voice.reset(); restartsDone++;
    }
    if (voice && std::fabs(hostRate - voiceRate) > 1.0) {
        { std::lock_guard<std::mutex> lock(snapMutex); battery.assign(voice->batteryRam(), voice->batteryRam() + 0x2000); }
        voice.reset();
        boot(hostRate);
    }
    if (!voice || handoverGen.load() > voiceGen) {
        if (Voice* h = handover.exchange(nullptr)) {
            voiceGen = handoverGen.load();
            if (std::fabs(h->hostRate() - hostRate) > 1.0) delete h;           // (prepareToPlay restarts the power-up on a rate change, so this is only a guard)
            else { voice.reset(h); for (float& t : followTarget) t = -1.f; loadByCall = false; loadWait = true; loadQuiet = loadAge = 0.0; loadSeen = false; loadTarget.clear(); ctlCount = 0; bypassInit = false; bypassFollowing = false; tapWait = 0.0; lastFwBypass = -1; clockFromMidi = false; wasPlaying = false; }
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
        else if (m.isProgramChange()) {
            v.midi.programChange(m.getProgramChangeNumber());
            const int n = m.getProgramChangeNumber();
            if (v.machine.mram[batram::PGM_CHNG - 0x8000] && n < USER_SLOTS) {                      // the firmware loads register n: that is the preset now
                std::unique_lock<std::mutex> lock(snapMutex, std::try_to_lock);
                if (lock) { presetUser = true; presetSlot = n; baseValid = false; loading = true; loadByCall = false; loadWait = true; loadQuiet = loadAge = 0.0; loadSeen = false; loadTarget = std::to_string(n / 10) + "." + std::to_string(n % 10); }
            }
        }
    }
    midi.clear();
    // ---- the host's tempo: the RATE of the BPM programs follows it in SYNC mode (the unit's own clock input is only fed from real MIDI clock) ----
    std::vector<int> clockAt;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto bpm = pos->getBpm()) if (*bpm > 1.0) hostBpm = *bpm;
    size_t ce = 0;
    float* ch0 = buffer.getWritePointer(0); float* ch1 = nch > 1 ? buffer.getWritePointer(1) : nullptr;
    const int ctlPeriod = std::max(1, (int)(hostRate / 1000.0));
    for (int i = 0; i < n; i++) {
        while (ce < clockAt.size() && clockAt[ce] <= i) { v.midi.clockEdge(24); ce++; }
        if (--ctlCount <= 0) {
            ctlCount = ctlPeriod;
            PanelLogic::In in; in.dt = ctlPeriod / hostRate;
            for (int i = 0; i < ROWS * COLS; i++) {
                // A value the firmware set is on its way to the host (the message thread applies it up to a timer tick later). Until the host's value moves
                // off the one it had, that value is stale, not a hand on the knob: read as a touch it would be written back and undo the firmware's move.
                const float k = cellRaw[i]->load();
                if (followTarget[i] >= 0.f && k == followFrom[i]) in.knob[i / COLS][i % COLS] = followTarget[i];
                else { followTarget[i] = -1.f; in.knob[i / COLS][i % COLS] = k; }
            }
            in.input = inputRaw->load(); in.inPad20 = inPadRaw->load() > 0.5f; in.outPad20 = outPadRaw->load() > 0.5f; in.trimVolts = 1.f;     // one volt = full scale: samples are volts
            PanelLogic::Out out;
            logic.control(v, in, out);
            // RATE in SYNC mode: the program's beat is a note value of the host's tempo, written as the firmware's own RATE word (BPM = word - 384)
            rateRow = rateCol = -1;
            if (v.control.tableReady())
                for (int r = 0; r < ROWS && rateRow < 0; r++) for (int c = 0; c < COLS; c++) { const Cell& x = v.control.cell(r, c); if (x.editable() && isRateText(x.text)) { rateRow = r; rateCol = c; break; } }
            if (rateRow >= 0 && rateMode.load() == RATE_SYNC && !loadWait) {
                const int w = rateWord(hostBpm.load(), rateDiv.load());
                if (w != v.control.cell(rateRow, rateCol).word) v.control.setTarget(rateRow, rateCol, w);
            }
            // a load (or store) is done when the keys are still, the cell table is built and nothing has moved for a moment: the words now are the preset's
            if (loadWait) {
                loadAge += in.dt;
                // the firmware has it when its own display names the slot (not "LOADING PROGRAM") and nothing is moving
                std::string d = PanelLogic::clean(v.control.displayText()); { const size_t a = d.find_first_of("0123456789"); d = a == std::string::npos || a > 3 ? d : d.substr(a); }   // past V3's indicator glyphs
                const bool busyText = d.find("LOADING") != std::string::npos || d.find("STORED") != std::string::npos;
                // a called load is under way until the loader has returned (the slave then writes the control store for up to ~0.2 s, and the program-type
                // detector runs on the next pass); a STORE or a MIDI program change goes through the keys / the UART, so there the firmware's own display says it
                if (loadByCall) { if (v.control.loadsDone() > loadsAt && !loadSeen) { loadSeen = true; loadQuiet = -0.3; } }
                else if (busyText) loadSeen = true;
                const bool started = loadByCall ? loadSeen : (loadSeen || loadTarget.empty() || loadAge > 1.5);
                const bool shown = loadByCall || loadTarget.empty() || (!busyText && d.compare(0, loadTarget.size(), loadTarget) == 0);     // a called load says itself when it is done
                if (!started || v.keys.busy() || v.control.busy() || !v.control.tableReady() || !shown) { if (loadQuiet > 0.0) loadQuiet = 0.0; }
                loadQuiet += in.dt;
                if ((started && loadQuiet >= 0.25) || loadAge > 8.0) {
                    std::unique_lock<std::mutex> lock(snapMutex, std::try_to_lock);
                    if (lock) {
                        for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) baseWord[r][c] = v.control.cell(r, c).word;
                        baseValid = true; loadWait = false; loading = false; storing = false;
                        logic.snapshot(v, snap); battery.assign(v.batteryRam(), v.batteryRam() + 0x2000); ramCount = 50;
                    }
                }
            }
            for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) if (out.setKnob[r][c]) {
                const int i = r * COLS + c;
                if (followTarget[i] < 0.f) followFrom[i] = cellRaw[i]->load();
                followTarget[i] = out.knob[r][c]; follow[i] = out.knob[r][c]; followPending = true;
            }
            // BYPASS: the host's switch and the unit's own key are one thing
            // The firmware's flag is the truth; the host's switch asks for a change. A value this side sent to the host arrives a timer tick later: until the
            // host's value moves off the one it had, it is stale, not a request (read as one, it would tap BYPASS again and the two would chase each other).
            const float bpRaw = bypassRaw->load(); const bool fw = v.control.bypassed();
            bool bp = bpRaw > 0.5f;
            if (bypassFollowing) { if (bpRaw == bypassFrom) bp = bypassTarget; else bypassFollowing = false; }
            auto show = [&](bool x) { if (!bypassFollowing) bypassFrom = bpRaw; bypassFollowing = true; bypassTarget = x; bypassFollow = x ? 1 : 0; bp = x; };
            if (tapWait > 0.0) tapWait -= in.dt;
            if (!bypassInit) { bypassInit = true; lastFwBypass = fw ? 1 : 0; if (bp != fw) show(fw); lastBypassReq = bp; }
            else if ((fw ? 1 : 0) != lastFwBypass) { lastFwBypass = fw ? 1 : 0; tapWait = 0.0; if (bp != fw) show(fw); lastBypassReq = bp; }   // the key, a footswitch, MIDI
            else {
                if (bp != lastBypassReq) { lastBypassReq = bp; if (bp != fw && tapWait <= 0.0) { v.keys.bypassTap(); tapWait = 1.0; } }   // the host asked
                else if (bp != fw && tapWait <= 0.0 && !bypassFollowing) { v.keys.bypassTap(); tapWait = 1.0; }                             // a tap that did not take
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
        state.setProperty("presetUser", presetUser, nullptr); state.setProperty("presetSlot", presetSlot, nullptr);
        state.setProperty("rateMode", rateMode.load(), nullptr); state.setProperty("rateDiv", rateDiv.load(), nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void MoonVerbProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;
    const juce::ValueTree state = juce::ValueTree::fromXml(*xml);
    apvts.replaceState(state);
    power = state.getProperty("power", true);
    rateMode = (int)state.getProperty("rateMode", (int)RATE_HZ) == RATE_SYNC ? RATE_SYNC : RATE_HZ; rateDiv = juce::jlimit(0, rateDivs() - 1, (int)state.getProperty("rateDiv", 6));
    { std::lock_guard<std::mutex> lock(snapMutex); presetUser = state.getProperty("presetUser", false); presetSlot = juce::jlimit(0, presetUser ? USER_SLOTS - 1 : FACTORY_SLOTS - 1, (int)state.getProperty("presetSlot", 0)); baseValid = false; }
    const juce::String b64 = state.getProperty("battery").toString();
    if (b64.isNotEmpty()) {
        juce::MemoryOutputStream mo; juce::Base64::convertFromBase64(mo, b64);
        if (mo.getDataSize() == 0x2000) { std::lock_guard<std::mutex> lock(snapMutex); battery.assign((const uint8_t*)mo.getData(), (const uint8_t*)mo.getData() + 0x2000); }
        keepRam = false; restartRequested = true;                                 // boot on that RAM image
    }
    juce::StringArray paths; paths.addLines(state.getProperty("romPaths").toString());
    if (paths.size() > 0) { std::vector<juce::File> files; for (const auto& p : paths) files.push_back(juce::File(p)); loadFiles(files); }
    else if (b64.isNotEmpty()) boot(hostRate);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MoonVerbProcessor(); }
