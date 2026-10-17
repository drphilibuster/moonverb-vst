// Panel.hpp: what the panel's controls mean to the machine (Rack-free, so it is tested like the rest).
//
// The module hands this the state of every control about once a millisecond and gets back what to turn and what to light. It owns:
//   * the PROGRAM controls: ROW and COL pick a program (PGM) or a register (REG); LOAD, STORE and BYPASS act on a rising edge, exactly as a finger on the key
//     would (the keys are pressed through Keys, so the firmware does everything the hardware does);
//   * the PARAMETER MATRIX: 5 x 9 knobs that ARE the machine's cells (row, column), whatever the running program calls them. Turning one sets the target word
//     (the edit scheduler decides when the firmware gets it); when the firmware moves a word on its own (a program load, a master, a limit) the knob follows,
//     but never while a hand is on it;
//   * the CV LANES: SET arms a lane, the next knob touched is its target (touch it again to release), and then lane = knob + attenuverter x CV / 10 V;
//   * the dedicated inputs, turned into the MIDI the firmware's patches listen to (mod wheel, aftertouch, note and gate, sustain), the SOFT knob CV, program
//     change, the bypass gate, and the clock (24 ppqn pulses from edges at 1/2/4/8/24 per quarter note; V3 firmware only) and run;
//   * levels: the input knob, the +4/-20 switches, the full-scale trim.
#pragma once
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "BatRam.hpp"
#include "Voice.hpp"

namespace moonverb {

class PanelLogic {
public:
    static const int ROWS = 5, COLS = 9, LANES = 8;
    struct In {
        double dt = 0.001;                          // seconds since the previous call
        float row = 0, col = 0; bool regMode = false;
        bool load = false, store = false, bypass = false;
        float knob[ROWS][COLS] = {};                // 0..1
        float att[LANES] = {}; bool asg[LANES] = {}; float cv[LANES] = {}; bool cvOn[LANES] = {};
        float mod = 0, at = 0, note = 0, gate = 0, sust = 0, soft = 0, pgm = 0, byp = 0;
        bool modOn = false, atOn = false, noteOn = false, gateOn = false, sustOn = false, softOn = false, pgmOn = false, bypOn = false;
        int clkDiv = 4;                             // index into {1, 2, 4, 8, 24} edges per quarter note (24 by default: the hardware's own rate)
        float input = 1.f; bool inPad20 = false, outPad20 = false; float trimVolts = 5.f;
    };
    struct Out {
        bool setKnob[ROWS][COLS] = {}; float knob[ROWS][COLS] = {};      // knobs to turn to follow the firmware
        float asgLight[LANES] = {}; bool bypassLed = false;
    };
    struct Snap {
        std::string display;                        // the 16-digit display
        bool valid[ROWS][COLS] = {}, master[ROWS][COLS] = {}; std::string name[ROWS][COLS], caption[ROWS][COLS]; int lo[ROWS][COLS] = {}, hi[ROWS][COLS] = {}, word[ROWS][COLS] = {};
        int touched = -1;                           // last-touched cell (row * COLS + col) and when
        double sinceTouch = 99.0;
        std::string laneName[LANES];
        int leds = 0; double load = 0; bool fault = false; uint64_t epoch = 0; bool tableReady = false;
        int mode = 0; int paramRow = 0, paramCol = 0; bool bypassed = false;   // the firmware's own front-panel mode (1 PARAM, 2 PGM, 4 REG), the parameter it shows, the bypass flag
    };

    int laneTarget[LANES]; bool armed[LANES] = {};  // laneTarget is part of the patch
    PanelLogic() { for (int i = 0; i < LANES; i++) laneTarget[i] = -1; for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) { last[r][c] = -1.f; touch[r][c] = -99.0; } }

    static bool registerUsed(const uint8_t* ram, int n) { return ram[batram::slotAddr(n) - 0x8000] != 0; }
    /** A register's own name ("" when it holds nothing). */
    static std::string registerName(const uint8_t* ram, int n) {
        if (!registerUsed(ram, n)) return std::string();
        return tidy(std::string((const char*)ram + (batram::slotAddr(n) - 0x8000) + 3, 13));      // what the display shows after "r.c ": the rest of the field is not part of it
    }
    /** The firmware's 16 digits with the glyphs the font has no letter for taken out: a '?' is a symbol the display draws and this one cannot, and the dot that
        belongs to it goes too. */
    static std::string clean(const std::string& d) {
        std::string o;
        for (size_t i = 0; i < d.size(); i++) { if (d[i] == '?') { o += ' '; if (i + 1 < d.size() && d[i + 1] == '.') i++; } else o += d[i]; }
        return o;
    }

    void control(Voice& v, const In& in, Out& out) {
        now += in.dt;
        Control& C = v.control;
        // ---- levels ----
        v.fullScaleVolts = in.trimVolts < 1.f ? 1.0 : in.trimVolts;
        v.inputGain = in.input * (in.inPad20 ? 0.17783 : 1.0);                  // -20 mode: 15 dB less at the converter (Step 33)
        v.outputPad = in.outPad20 ? 0.0575 : 1.0;                                // output switch: -24.7 dB
        if (C.programEpoch != epoch) { epoch = C.programEpoch; for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) { last[r][c] = -1.f; touch[r][c] = -99.0; } }
        // ---- program ----
        const bool loadEdge = in.load && !pLoad, storeEdge = in.store && !pStore, byEdge = (in.bypass && !pBypass) || (in.bypOn && in.byp > 1.f && !pByp);
        pLoad = in.load; pStore = in.store; pBypass = in.bypass; pByp = in.bypOn && in.byp > 1.f;
        const int row = (int)std::lround(in.row), col = (int)std::lround(in.col);
        if (loadEdge) { if (in.regMode) v.keys.selectRegister(row > 4 ? 4 : row, col); else v.keys.selectProgram(row, col); }
        if (storeEdge) v.keys.storeRegister(row > 4 ? 4 : row, col);
        if (byEdge) v.keys.bypassTap();
        // ---- lanes: arming ----
        for (int i = 0; i < LANES; i++) { if (in.asg[i] && !pAsg[i]) { armed[i] = !armed[i]; for (int j = 0; j < LANES; j++) if (j != i) armed[j] = false; } pAsg[i] = in.asg[i]; }
        // ---- the matrix ----
        double modSum[ROWS][COLS] = {}; bool bound[ROWS][COLS] = {};
        for (int i = 0; i < LANES; i++) if (laneTarget[i] >= 0 && in.cvOn[i]) { const int r = laneTarget[i] / COLS, c = laneTarget[i] % COLS; modSum[r][c] += in.att[i] * in.cv[i] / 10.0; bound[r][c] = true; }
        for (int i = 0; i < LANES; i++) if (laneTarget[i] >= 0) bound[laneTarget[i] / COLS][laneTarget[i] % COLS] = true;
        if (in.softOn) { modSum[0][2] += in.soft / 10.0; bound[0][2] = true; }
        const bool ready = C.tableReady();
        for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
            const Cell& x = C.cell(r, c); if (!ready || !x.editable() || (r == 0 && c == 9)) { last[r][c] = in.knob[r][c]; continue; }
            const float k = in.knob[r][c];
            if (last[r][c] < 0.f) last[r][c] = k;                                  // first sight after a program change: not a touch
            const bool touched = std::fabs(k - last[r][c]) > 1e-5f;
            if (touched) {
                last[r][c] = k; touch[r][c] = now; lastTouched = r * COLS + c; touchedAt = now; C.watch(r, c, true);
                for (int i = 0; i < LANES; i++) if (armed[i]) { laneTarget[i] = (laneTarget[i] == r * COLS + c) ? -1 : r * COLS + c; armed[i] = false; }
            }
            const bool hand = now - touch[r][c] < 0.6;
            if (!hand && now - touch[r][c] > 3.0) C.watch(r, c, bound[r][c] || x.captionStale);   // captions for what is being worked, what a lane drives, and any word that moved
            double want = k + modSum[r][c];
            if (touched || bound[r][c] || hand) { want = want < 0 ? 0 : (want > 1 ? 1 : want); C.setNormalized(r, c, want); }
            else {                                                                  // follow the firmware: its word, normalised to the cell's own limits
                const double span = x.hi - x.lo; double wn = span > 0 ? (x.word - x.lo) / span : 0.0;
                wn = wn < 0 ? 0 : (wn > 1 ? 1 : wn);                                 // a master can carry a word past the cell's own limits: the knob stops at its end
                if (std::fabs(wn - k) > 0.5 / (span > 0 ? span : 1.0)) { out.setKnob[r][c] = true; out.knob[r][c] = (float)wn; last[r][c] = (float)wn; }
            }
        }
        // ---- dedicated inputs -> MIDI ----
        Midi& M = v.midi;
        if (in.modOn) { const int m = cc7(in.mod); if (m != pMod) { M.cc(1, m); pMod = m; } }
        if (in.atOn) { const int a = cc7(in.at); if (a != pAt) { M.aftertouch(a); pAt = a; } }
        if (in.noteOn) curNote = 60 + (int)std::lround(12.0 * in.note);
        if (in.gateOn) {
            const bool g = in.gate > 1.f;
            if (g && !pGate) { M.note(curNote, cc7(in.gate)); heldNote = curNote; }
            if (!g && pGate) M.noteOff(heldNote);
            pGate = g;
        }
        if (in.sustOn) { const int s = in.sust > 1.f ? 127 : 0; if (s != pSus) { M.cc(64, s); pSus = s; } }
        if (in.pgmOn) {
            if (!pgmWas) { C.setProgramChange(true); pgmWas = true; }
            int n = (int)std::lround(in.pgm * 10.0); n = n < 0 ? 0 : (n > 49 ? 49 : n);
            if (n != pPgm) { pgmCand = n; pgmSince = now; pPgm = n; }
            if (pgmCand >= 0 && now - pgmSince > 0.03 && pgmCand != sentPgm) { M.programChange(pgmCand); sentPgm = pgmCand; pgmCand = -1; }
        } else { pgmWas = false; }
        // ---- lights ----
        for (int i = 0; i < LANES; i++) out.asgLight[i] = armed[i] ? 1.f : (laneTarget[i] >= 0 ? 0.25f : 0.f);
        out.bypassLed = C.bypassed();
        clkDiv = in.clkDiv;
    }
    // one clock edge / run edge, found by the module per host sample
    void clockEdge(Voice& v) { static const int ppqn[5] = { 1, 2, 4, 8, 24 }; v.midi.clockEdge(ppqn[clkDiv < 0 ? 0 : (clkDiv > 4 ? 4 : clkDiv)]); }
    void runEdge(Voice& v) { v.midi.clockStart(); }

    void snapshot(Voice& v, Snap& s) const {
        const Control& C = v.control; s.display = clean(C.displayText()); s.tableReady = C.tableReady(); s.epoch = C.programEpoch; s.load = C.load(); s.fault = C.fault(); s.leds = v.detector.leds();
        s.touched = lastTouched; s.sinceTouch = now - touchedAt;
        s.mode = v.keys.mode(); s.paramRow = v.machine.mram[Keys::ROW - 0x8000]; s.paramCol = v.machine.mram[Keys::COL - 0x8000]; s.bypassed = C.bypassed();
        for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
            const Cell& x = C.cell(r, c); const bool ok = C.tableReady() && x.editable() && !(r == 0 && c == 9);
            s.valid[r][c] = ok; s.lo[r][c] = x.lo; s.hi[r][c] = x.hi; s.word[r][c] = x.word;
            s.master[r][c] = ok && x.master && std::string(x.text, 14).find("MST") != std::string::npos;     // the masters are the firmware's ... MST cells (LVL MST, REFL DLY MST)
            if (ok) {
                label(std::string(x.text, 13), x.caption, s.name[r][c], s.caption[r][c]);
                // the 16 digits can run out before the value does (V3's two symbol digits, a long name): the formatter writes nothing visible, so show the
                // parameter's step, which is the number the firmware prints for a plain 0..n parameter
                if (x.caption.empty() && !x.captionStale) s.caption[r][c] = std::to_string(x.word - x.lo);
            } else { s.name[r][c].clear(); s.caption[r][c].clear(); }
        }
        for (int i = 0; i < LANES; i++) s.laneName[i] = laneTarget[i] >= 0 && s.valid[laneTarget[i] / COLS][laneTarget[i] % COLS] ? s.name[laneTarget[i] / COLS][laneTarget[i] % COLS] : (armed[i] ? "TOUCH" : std::string());
    }
    /** The firmware's name text is a template its formatter fills in: "MIX     % WET" takes the value in the gap, "V  LVL" takes the voice number in the double
        space ("V1 LVL"). From the template and the caption the formatter wrote, a name ("V1 LVL", "MIX") and a value with its unit ("FULL", "100 % WET"). */
    static void label(const std::string& raw, const std::string& caption, std::string& name, std::string& value) {
        std::string t = raw; for (char& ch : t) if ((unsigned char)ch < 32 || (unsigned char)ch > 126) ch = ' ';
        const size_t lead = t.find_first_not_of(' ');
        const size_t gap = lead == std::string::npos ? std::string::npos : t.find("   ", lead);
        std::string head = lead == std::string::npos ? std::string() : t.substr(lead, gap == std::string::npos ? std::string::npos : gap - lead), tail;
        if (gap != std::string::npos) { const size_t a = t.find_first_not_of(' ', gap); if (a != std::string::npos) tail = tidy(t.substr(a)); }
        std::string val = tidy(caption);
        auto firstWord = [](std::string& v) { const size_t sp = v.find(' '); std::string w = v.substr(0, sp); v = sp == std::string::npos ? std::string() : tidy(v.substr(sp + 1)); return w; };
        const size_t dbl = head.find("  ");
        if (lead != std::string::npos && lead >= 2 && !val.empty()) head = firstWord(val) + " " + head;                 // "   RFL" + "L2 OFF": the voice fills the blanks
        else if (dbl != std::string::npos && val.size() >= 2 && std::isdigit((unsigned char)val[0]) && val[1] == ' ') {     // "V  LVL" + "1 FULL": the voice number
            head = head.substr(0, dbl) + val[0] + " " + head.substr(head.find_first_not_of(' ', dbl)); val = tidy(val.substr(1));
        }
        head = tidy(head);
        if (head.size() >= 2 && (head.back() == '-' || head.back() == '+') && head[head.size() - 2] == ' ') { val = std::string(1, head.back()) + val; head = tidy(head.substr(0, head.size() - 1)); }   // "V3 FDBK -": the sign is the value's
        if (head.empty() && val.find(' ') != std::string::npos) head = firstWord(val);
        name = head; value = val;
        if (!tail.empty()) {
            const std::string last = tail.substr(tail.find_last_of(' ') == std::string::npos ? 0 : tail.find_last_of(' ') + 1);
            const bool has = value.size() >= last.size() && value.compare(value.size() - last.size(), last.size(), last) == 0;     // "32 S": the unit is there already
            if (!has) value = value.empty() ? tail : value + " " + tail;
        }
    }
    static std::string tidy(const std::string& t) {            // the firmware's 14-byte name/unit text: printable, single spaces
        std::string o; bool sp = false;
        for (char ch : t) { if (ch < 33 || ch > 126) { sp = !o.empty(); continue; } if (sp) o += ' '; sp = false; o += ch; }
        return o;
    }

private:
    double now = 0, touchedAt = -99; uint64_t epoch = 0; int lastTouched = -1;
    float last[ROWS][COLS]; double touch[ROWS][COLS];
    bool pLoad = false, pStore = false, pBypass = false, pByp = false, pAsg[LANES] = {};
    int pMod = -1, pAt = -1, pSus = -1, curNote = 60, heldNote = 60, pPgm = -1, pgmCand = -1, sentPgm = -1, clkDiv = 4; bool pGate = false, pgmWas = false; double pgmSince = 0;
    static int cc7(float volts) { int v = (int)std::lround(volts / 10.0 * 127.0); return v < 0 ? 0 : (v > 127 ? 127 : v); }
};

}
