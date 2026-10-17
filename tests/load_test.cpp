// MoonVerb: the fast program/register load (Control::loadProgram / loadRegister, NOTES.md Step 60) against the front-panel key path (Keys::selectProgram /
// selectRegister), plus MIDI program change measured the same way. Needs the user's ROMs (MOONVERB_ROMS) and, for registers and program change, the library
// banks (MOONVERB_SYX). Each missing input prints SKIP.
//
// Every case starts its machines from one booted state (the bank in the battery RAM, booted, settled, cloned). One loads through the keys, one through the
// injected call, timed so the call lands where the key's load did (a third copy makes the same call as soon as asked, for the latency). Pass means:
//   1. both enter the same loader (program V2 0x400A / V3 0x40AC, register 0x45FE / 0x46A0) with the same row and column, or neither does (an empty factory
//      slot, or the slot already loaded and unedited: the firmware's program-change routine skips those, as on the hardware);
//   2. the bytes the loader sends the slave, entry to return, are the same sequence (the slave, and so the HSP, is given the same program); the input-level
//      bytes the scan ISR streams to the slave meanwhile are left out (their number depends on when the ISR fires);
//   3. compared at one absolute machine time, 6 s after the loads, WCS, slave RAM and master RAM are equal at every address except
//        - the front-panel addresses in allowedMaster() (the key path walks the panel's state machine, the call does not),
//        - the slave's time base in allowedSlave() (tick counter, loop divider, noise generator, last byte from the master),
//        - the two stacks (from the deepest SP either run reached up to the top),
//        - bytes that change by themselves over the last 2 s of either run (sampled every 10 ms): LFO-modulated WCS words and the state behind them.
//          V2's chorus/flange LFOs are computed by the master on its own loop timing, so any extra master work (a key, a MIDI byte, even a no-op call)
//          moves them; their value at an instant says when, not what, was loaded,
//        - addresses that also differ between the key path and other hardware loads of the same slot at nearby moments (the LOAD key pressed 7 ms
//          and 50 ms later, MIDI program changes landing at several phases around it): what depends on when a load happens, not on how. The slave seeds some modulation words from its free-running noise generator at load (Echorus, Concert Hall ...), and LFO
//          phases count from the load, so two presses of the LOAD key a few milliseconds apart already differ there.
//      Every address that differed is printed by category at the end.
#include "fixtures.hpp"
#include "../src/core/Machine.hpp"
#include "../src/core/Control.hpp"
#include "../src/core/Keys.hpp"
#include "../src/core/Midi.hpp"
#include "../src/core/BatRam.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>

using namespace moonverb; using namespace fx;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const uint64_t HZ = 3250000;
static const double SNAP_SECONDS = 6.0;          // after the later entry: every load and the controller's harvest are long done
static const double SETTLED = 1.0;               // a WCS word unchanged over this last stretch is settled (for the HSP latency)
static const double VARY_WINDOW = 2.0, VARY_STEP = 0.01;   // a byte that changes in this last stretch (sampled every 10 ms) is varying by itself
static const double CONTROL_DELAYS[] = { 0.0073, 0.05 };   // control LOAD key presses: one master idle pass later, and 50 ms later
static const double PC_OFFSETS[] = { -0.003, -0.0025, -0.002, -0.0015, -0.001, -0.0005, 0.0, 0.0005, 0.001, 0.0015, 0.002, 0.0025, 0.003 };   // control MIDI program changes landing around the call (any phase)
static const double PC_LEAD = 0.045;              // a program change byte pair reaches the loader about this long after it is sent

// notes the loader entry between master instructions; Control drives the firmware as in the plugin
struct Tap : Hook {
	Control* c = nullptr; unsigned pgm = 0, reg = 0, minSp = 0xFFFF; bool armed = false;
	uint64_t hit = 0; unsigned hitPc = 0, hitRow = 0, hitCol = 0, hitSp = 0; size_t hitM2s = 0, retM2s = 0; bool returned = false; std::vector<uint8_t>* trace = nullptr;
	unsigned isrOut = 0, nextPc = 0; std::set<size_t> isrBytes;   // the scan ISR's OUT (F0),A that streams the input level to the slave (V2 0x1915, V3 0x191A)
	void note(Machine& m) {
		if (armed && !hit && (m.m.pc == pgm || m.m.pc == reg)) { hit = m.m.cyc; hitPc = m.m.pc; hitRow = m.m.h << 8 | m.m.l; hitCol = m.m.d << 8 | m.m.e; hitSp = m.m.sp; hitM2s = trace ? trace->size() : 0; }
		if (hit && !returned && m.m.sp > hitSp) { returned = true; retM2s = trace ? trace->size() : 0; }           // the loader's RET: what it sent is the program download
	}
	void step(Machine& m) override {
		if (isrOut && nextPc == isrOut && trace && !trace->empty()) isrBytes.insert(trace->size() - 1);
		note(m); if (m.m.sp < minSp && m.m.sp >= 0x8000) minSp = m.m.sp; c->step(m); note(m); nextPc = m.m.pc;
	}   // after the controller too: an injected call jumps to its entry
};

struct Rig {
	std::unique_ptr<Machine> M; std::unique_ptr<Control> C; std::unique_ptr<Keys> K; std::unique_ptr<Midi> Mi; Tap T; bool v3 = false;
	uint64_t target = 0; unsigned minSsp = 0xFFFF; std::vector<uint8_t> m2s;
	std::vector<uint64_t> lastChange; Bytes prevWcs;            // per WCS word: the last cycle its content changed (for the HSP latency)
	explicit Rig(const Fw& fw) : M(new Machine()), C(new Control(*M)), K(new Keys(*M)), Mi(new Midi(*M)), v3(fw.family == "3.01") {
		M->load(fw.u62.data(), fw.u62.size(), fw.u95.data(), fw.u95.size()); T.c = C.get(); M->hook = &T; C->setV3(v3);
		T.pgm = C->loadProgramAt(); T.reg = C->loadRegisterAt(); T.trace = &m2s;
		static const int lvl[] = { 0x7b, 0xd3, 0xf0, 0xd9, 0x08, 0xfb, 0xed, 0x4d };    // LD A,E / OUT (F0),A / EXX / EX AF,AF' / EI / RETI
		const long q = moonverbFind(M->mrom, sizeof M->mrom, lvl); T.isrOut = q < 0 ? 0 : (unsigned)q + 1; lastChange.assign(0x400, 0); prevWcs.assign(0x400, 0);
	}
	// the whole machine state of another rig; the controller starts fresh and harvests again (identically on every clone)
	void cloneFrom(const Rig& o) {
		Machine& a = *M; const Machine& b = *o.M;
		a.m = b.m; a.s = b.s; a.m.userdata = a.s.userdata = &a;
		memcpy(a.mram, b.mram, sizeof a.mram); memcpy(a.sram, b.sram, sizeof a.sram); memcpy(a.wcs, b.wcs, sizeof a.wcs);
		a.mdav = b.mdav; a.m2s = b.m2s; a.sdav = b.sdav; a.s2m = b.s2m; a.midi = b.midi; a.da = b.da; a.rxbyte = b.rxbyte; a.nextMidi = b.nextMidi;
		a.copyPage = b.copyPage; a.lastNzPage = b.lastNzPage; a.wcsWrites = b.wcsWrites; a.copyEvents = b.copyEvents; a.slaveLoads = b.slaveLoads; a.prevM2s = b.prevM2s;
		a.headrm = b.headrm; memcpy(a.keyCol, b.keyCol, sizeof a.keyCol); a.direct = b.direct; a.mixWet = b.mixWet; a.mixDry = b.mixDry; a.irqNext = b.irqNext;
		target = o.target; prevWcs.assign(a.wcs, a.wcs + 0x400); lastChange.assign(0x400, a.m.cyc);
	}
	void tick() {
		target += 96; M->runTo(target); K->tick(); Mi->tick(); if (M->s.sp < minSsp && M->s.sp >= 0x4000) minSsp = M->s.sp;
		if (memcmp(prevWcs.data(), M->wcs, 0x400)) for (int i = 0; i < 0x400; i++) if (prevWcs[i] != M->wcs[i]) { prevWcs[i] = M->wcs[i]; lastChange[i] = M->m.cyc; }
	}
	void run(double s) { if (!target) target = M->m.cyc; const long n = (long)(s * HZ / 96.0); for (long i = 0; i < n; i++) tick(); }
	void settle(double maxSeconds = 20.0) { double t = 0; run(0.5); while (t < maxSeconds && (C->busy() || K->busy() || Mi->pending())) { run(0.25); t += 0.25; } }
};

// ---- the front-panel addresses the key path may leave differently (master RAM). Each is traced in the ROMs (NOTES.md Step 60). ---------------------------
//   V2 0x9BAD-0x9BF0 / V3 0x9BAF-0x9BF2   the key/knob event ring (the scan ISR's type/data pairs) and its write/read pointers (V2 0x9BED/0x9BEF, V3 0x9BEF/0x9BF1)
//   V2 0x9C16 / V3 0x9C18                 the key handler's last key (0xFF = none)
//   V2 0x9C25-0x9C30 / V3 0x9C27-0x9C32   the software timers the key handler arms for tap/hold (4 bytes each: a deadline in ISR ticks)
// That is all, for the program-change routine Control calls: it enters PGM/REG mode, sets the location and redraws the display itself. (The bare loader call
// would also leave mode 0x994D/0x994B, location 0x9949/0x994A, the LOAD handler's column copy V2 0x9C3C / V3 0x9C3E and the display RAM 0x9BF3-0x9C13 behind.)
struct Allow { unsigned lo, hi; const char* why; };
static std::vector<Allow> allowedMaster(bool v3) {
	const unsigned d = v3 ? 2 : 0;
	return {
		{ 0x9BAD + d, 0x9BF0 + d, "event ring and pointers" },
		{ 0x9C16 + d, 0x9C16 + d, "last key" },
		{ 0x9C25 + d, 0x9C30 + d, "key timers" },
	};
}
// ---- slave addresses that hold the slave's time base, not the program (U95 V2 and V3 alike; NOTES.md Step 60) ----------------------------------------------
//   0x4342 the ISR's tick counter (decremented every 8th interrupt), 0x4343/0x4344 the flags the ISR sets for the main loop at that tick
//   0x41CE the main loop's divide-by-8 counter
//   0x4335-0x4337 the noise generator (a pointer walking the slave ROM, 0x0B70, and the byte it read), 0x40D2 the copy of that byte the WCS writer keeps
//   0x4349 the last byte read from the master (the paths are not at the same point of their loops at the snapshot)
//   the input-level detector, fed by the level bytes the master's scan ISR streams after FF 82 (master 0x1900: HEADRM -> 0x9B38 -> OUT F0 while 0x9B3A is
//   set; how many land between two commands is timing): 0x42FE current level, 0x42FD/0x42FF its flags, 0x4302 hold count, 0x4303-0x430D the 11-step
//   level history (shifted by LDIR at 0x1799), 0x41CA/0x41CC trigger and peak level, 0x42E3-0x42E5 the 13-step bit counter of the routine at 0x03C5
static std::vector<Allow> allowedSlave() {
	return {
		{ 0x4342, 0x4344, "ISR tick counter and flags" },
		{ 0x41CE, 0x41CE, "loop divider" },
		{ 0x4335, 0x4337, "noise generator" },
		{ 0x40D2, 0x40D2, "noise byte copy" },
		{ 0x4349, 0x4349, "last byte from the master" },
		{ 0x42FD, 0x430D, "level detector" },
		{ 0x41CA, 0x41CC, "level detector trigger/peak" },
		{ 0x42E3, 0x42E5, "level stream counter" },
	};
}
static const char* allowedWhy(const std::vector<Allow>& al, unsigned a) { for (const Allow& x : al) if (a >= x.lo && a <= x.hi) return x.why; return nullptr; }

// ---- one path ------------------------------------------------------------------------------------------------------------------------------------
struct Snap {
	Bytes wcs, sram, mram; std::set<unsigned> varying;           // final state; addresses that changed by themselves over the last VARY_WINDOW
	std::string disp; unsigned mFloor = 0xFFFF, sFloor = 0xFFFF;
	bool entered = false; unsigned hitPc = 0, row = 0, col = 0; std::vector<uint8_t> m2s;        // what the loader sent the slave between its entry and its return
	                                                                                          // (without the level bytes the scan ISR streams in between)
	double entryLat = 0, hspLat = 0, callDur = -1;
};
struct Path { std::unique_ptr<Rig> R; uint64_t req = 0, loads0 = 0, done = 0; };

template <class F> static Path startPath(const Fw& fw, const Rig& base, F go) {
	Path p; p.R.reset(new Rig(fw)); Rig& R = *p.R; R.cloneFrom(base); R.settle();      // the clone's controller harvests the running program first, as the plugin's would have
	R.M->m2sTrace = &R.m2s; p.req = R.M->m.cyc; p.loads0 = R.C->loadsDone();
	R.T.armed = true; go(R);
	while (!R.T.hit && R.M->m.cyc - p.req < (uint64_t)(4 * HZ)) R.tick();                // a key sequence enters within ~2 s; an empty or unchanged slot never does
	return p;
}
static uint64_t mark(const Path& p) { return p.R->T.hit ? p.R->T.hit : p.req; }
static Snap finishPath(Path& p, uint64_t end) {
	Rig& R = *p.R; Snap s;
	const uint64_t w0 = end - (uint64_t)(SETTLED * HZ);
	const uint64_t v0 = end - (uint64_t)(VARY_WINDOW * HZ); uint64_t nextV = v0; Bytes pw, ps, pm;
	auto sample = [&]() {
		const Machine& M = *R.M;
		if (!pw.empty()) {
			for (int i = 0; i < 0x400; i++) if (pw[i] != M.wcs[i]) s.varying.insert(0x10000u | i);
			for (int i = 0; i < 0x2000; i++) if (ps[i] != M.sram[i]) s.varying.insert(0x4000u + i);
			for (int i = 0; i < 0x2000; i++) if (pm[i] != M.mram[i]) s.varying.insert(0x8000u + i);
		}
		pw.assign(M.wcs, M.wcs + 0x400); ps.assign(M.sram, M.sram + 0x2000); pm.assign(M.mram, M.mram + 0x2000);
	};
	while (R.target + 96 <= end) {
		R.tick(); if (!p.done && R.C->loadsDone() != p.loads0) p.done = R.M->m.cyc;
		if (R.M->m.cyc >= nextV) { sample(); nextV += (uint64_t)(VARY_STEP * HZ); }
	}
	R.M->runTo(end); sample();
	s.wcs.assign(R.M->wcs, R.M->wcs + 0x400); s.sram.assign(R.M->sram, R.M->sram + 0x2000); s.mram.assign(R.M->mram, R.M->mram + 0x2000);
	s.disp = R.M->displayText(); s.mFloor = R.T.minSp; s.sFloor = R.minSsp;
	s.entered = R.T.hit != 0; s.hitPc = R.T.hitPc; s.row = R.T.hitRow; s.col = R.T.hitCol;
	if (s.entered) for (size_t i = R.T.hitM2s; i < (R.T.returned ? R.T.retM2s : R.m2s.size()); i++) if (!R.T.isrBytes.count(i)) s.m2s.push_back(R.m2s[i]);
	if (s.entered) { s.entryLat = (R.T.hit - p.req) / (double)HZ; if (p.done) s.callDur = (p.done - p.req) / (double)HZ; }
	// HSP latency: the last change of a WCS word that is settled at the end
	uint64_t last = p.req;
	for (int i = 0; i < 0x400; i++) if (R.lastChange[i] < w0 && R.lastChange[i] > last) last = R.lastChange[i];
	s.hspLat = (last - p.req) / (double)HZ;
	return s;
}
static std::vector<Snap> finishAll(std::vector<Path>& ps) {
	uint64_t m = 0; for (const Path& p : ps) m = std::max(m, mark(p));
	std::vector<Snap> out; for (Path& p : ps) out.push_back(finishPath(p, m + (uint64_t)(SNAP_SECONDS * HZ))); return out;
}

// ---- comparison ------------------------------------------------------------------------------------------------------------------------------------
// addresses are encoded with the area: WCS word w = 0x10000 | w, slave RAM and master RAM by their own address
static std::set<unsigned> differing(const Snap& a, const Snap& b) {
	std::set<unsigned> d;
	for (int i = 0; i < 0x400; i++) if (a.wcs[i] != b.wcs[i]) d.insert(0x10000u | i);
	for (int i = 0; i < 0x2000; i++) if (a.sram[i] != b.sram[i]) d.insert(0x4000u + i);
	for (int i = 0; i < 0x2000; i++) if (a.mram[i] != b.mram[i]) d.insert(0x8000u + i);
	return d;
}
struct Seen { std::map<unsigned, std::string> panel, slaveTime; std::set<unsigned> stackM, stackS, timing, varying; };
static std::vector<unsigned> unexplained(const Snap& a, const Snap& b, const std::set<unsigned>& timing, bool v3, Seen& seen, int& nTiming) {
	std::vector<unsigned> bad; const std::vector<Allow> am = allowedMaster(v3), as = allowedSlave();
	const unsigned mFloor = std::min(a.mFloor, b.mFloor), sFloor = std::min(a.sFloor, b.sFloor);
	for (unsigned ad : differing(a, b)) {
		if (ad >= 0x8000 && ad < 0x10000 && allowedWhy(am, ad)) { seen.panel[ad] = allowedWhy(am, ad); continue; }
		if (ad < 0x8000 && allowedWhy(as, ad)) { seen.slaveTime[ad] = allowedWhy(as, ad); continue; }
		if (ad >= 0x8000 && ad < 0x10000 && ad >= mFloor) { seen.stackM.insert(ad); continue; }
		if (ad < 0x8000 && ad >= sFloor && ad <= 0x47FF) { seen.stackS.insert(ad); continue; }
		if (timing.count(ad)) { seen.timing.insert(ad); nTiming++; continue; }
		if (a.varying.count(ad) || b.varying.count(ad)) { seen.varying.insert(ad); nTiming++; continue; }
		bad.push_back(ad);
	}
	return bad;
}
static std::string addrs(const std::vector<unsigned>& v, size_t n = 20) {
	std::string s; char b[24];
	for (size_t i = 0; i < v.size() && i < n; i++) { const unsigned a = v[i]; std::snprintf(b, sizeof b, " %s%04X", a & 0x10000 ? "WCS " : "", a & 0xFFFF); s += b; }
	if (v.size() > n) s += " ..."; return s;
}
static std::string addrs(const std::set<unsigned>& v) { return addrs(std::vector<unsigned>(v.begin(), v.end()), 1000); }

struct Stats { std::vector<double> v; void add(double x) { if (x >= 0) v.push_back(x); } std::string str() const {
	if (v.empty()) return "-"; std::vector<double> s = v; std::sort(s.begin(), s.end()); double sum = 0; for (double x : s) sum += x; char b[96];
	std::snprintf(b, sizeof b, "min %.3f  median %.3f  mean %.3f  max %.3f s", s.front(), s[s.size() / 2], sum / s.size(), s.back()); return b; } };

static std::vector<uint8_t> bankImage(const std::vector<Reg>& regs) {
	std::vector<uint8_t> ram(0x2000, 0); for (const Reg& r : regs) batram::put(ram.data(), r.n, r.data.data()); return ram;
}
static std::unique_ptr<Rig> bootBase(const Fw& fw, const std::vector<uint8_t>& ram) {
	std::unique_ptr<Rig> B(new Rig(fw)); if (!ram.empty()) memcpy(B->M->mram, ram.data(), 0x2000);
	B->M->powerCycle(); B->run(12.0); B->settle(); return B;
}

// key path `k` against the other path `f`; `ctl` = the key path pressed later (the timing controls). Returns true if it passes.
static bool judge(const char* what, const Snap& k, const Snap& f, const std::vector<Snap>& ctl, bool v3, Seen& seen, bool verbose) {
	bool ok = true; std::string why;
	if (k.entered != f.entered) { ok = false; why += " entry differs (key " + std::to_string(k.entered) + ", other " + std::to_string(f.entered) + ")"; }
	else if (k.entered && (k.hitPc != f.hitPc || k.row != f.row || k.col != f.col)) { ok = false; why += " different loader or arguments"; }
	const bool sameBytes = k.m2s == f.m2s;
	if (!sameBytes) { ok = false; why += " master->slave bytes differ (" + std::to_string(k.m2s.size()) + " vs " + std::to_string(f.m2s.size()) + ")"; }
	std::set<unsigned> timing; for (const Snap& c : ctl) { const std::set<unsigned> d = differing(k, c); timing.insert(d.begin(), d.end());
		if (c.m2s != k.m2s && std::getenv("LOAD_DUMP")) { size_t i = 0; while (i < c.m2s.size() && i < k.m2s.size() && c.m2s[i] == k.m2s[i]) i++; std::printf("     m2s differ at %zu of %zu/%zu: key", i, k.m2s.size(), c.m2s.size());
			for (size_t j = i > 6 ? i - 6 : 0; j < i + 12 && j < k.m2s.size(); j++) std::printf(" %02X", k.m2s[j]); std::printf(" | ctl"); for (size_t j = i > 6 ? i - 6 : 0; j < i + 12 && j < c.m2s.size(); j++) std::printf(" %02X", c.m2s[j]); std::printf("\n"); } }
	int nTiming = 0; const std::vector<unsigned> bad = unexplained(k, f, timing, v3, seen, nTiming);
	if (!bad.empty()) { ok = false; why += " state differs at" + addrs(bad); }
	if (std::getenv("LOAD_DUMP")) for (unsigned a : bad) { const unsigned ad = a & 0xFFFF; const Bytes& x = a & 0x10000 ? k.wcs : ad < 0x8000 ? k.sram : k.mram; const Bytes& y = a & 0x10000 ? f.wcs : ad < 0x8000 ? f.sram : f.mram;
		const unsigned o = a & 0x10000 ? ad : ad < 0x8000 ? ad - 0x4000 : ad - 0x8000; std::printf("     %05X key %02X other %02X\n", a, x[o], y[o]); }
	if (!ok || verbose)
		std::printf("  %-8s %-20s %s  entry %.3f/%.3f s  HSP %.3f/%.3f s  %zu bytes to the slave; %d timing-dependent bytes differ (%zu between key presses)%s%s\n", what, ("\"" + f.disp + "\"").c_str(),
			k.entered ? "loads  " : "no load", k.entryLat, f.entryLat, k.hspLat, f.hspLat, f.m2s.size(), nTiming, timing.size(), ok ? "" : "\n     FAIL:", why.c_str());
	return ok;
}

static void testFamily(const Fw& fw, const std::vector<Reg>& regs) {
	const bool v3 = fw.family == "3.01"; const bool verbose = std::getenv("LOAD_VERBOSE") != nullptr;
	// PGM CHNG on (0x98B8 = 1) in every run: the timing controls include MIDI program changes; nothing else reads the byte
	std::vector<uint8_t> ram = regs.empty() ? std::vector<uint8_t>(0x2000, 0) : bankImage(regs); ram[batram::PGM_CHNG - 0x8000] = 1;
	std::unique_ptr<Rig> B = bootBase(fw, ram);
	std::printf("V%s: program-change routine %04X, loaders: program %04X register %04X; base state mode %d, \"%s\"\n", fw.family.c_str(), B->C->programChangeAt(), B->C->loadProgramAt(), B->C->loadRegisterAt(),
		B->M->mram[Keys::MODE - 0x8000], B->M->displayText().c_str());
	CHECK(B->C->programChangeAt() && B->C->loadProgramAt() && B->C->loadRegisterAt());
	if (!B->C->programChangeAt()) return;
	Stats keyLat, fastLat, keyHsp, fastHsp, fastDur; Seen seen; int cases = 0, pass = 0, loads = 0;
	auto one = [&](bool reg, int row, int col) {
		auto keys = [&](double delay) { return [&, delay](Rig& R) { if (delay > 0) R.run(delay); if (reg) R.K->selectRegister(row, col); else R.K->selectProgram(row, col); }; };
		auto fast = [&](Rig& R) { if (reg) CHECK(R.C->loadRegister(row * 10 + col)); else CHECK(R.C->loadProgram(row, col)); };
		std::vector<Path> ps;
		ps.push_back(startPath(fw, *B, keys(0)));                       // [0] the LOAD key
		const uint64_t at = ps[0].R->T.hit ? ps[0].R->T.hit : ps[0].req + (uint64_t)(HZ / 2);
		ps.push_back(startPath(fw, *B, [&](Rig& R) { while (R.M->m.cyc + (uint64_t)(0.0074 * HZ) < at) R.tick(); fast(R); }));   // [1] the call, timed to land where the key's load did
		for (double d : CONTROL_DELAYS) ps.push_back(startPath(fw, *B, keys(d)));    // [2..] the LOAD key pressed a little later
		const int pcn = (reg ? 0 : 50) + row * 10 + col;                  //       and MIDI program changes for the same slot landing around the call
		const uint64_t fat = ps[1].R->T.hit ? ps[1].R->T.hit : at;
		for (double o : PC_OFFSETS) ps.push_back(startPath(fw, *B, [&, o](Rig& R) { while ((double)R.M->m.cyc + (PC_LEAD - o) * HZ < (double)fat) R.tick(); R.Mi->programChange(pcn); }));
		ps.push_back(startPath(fw, *B, [&](Rig& R) { (void)R; }));       //       and a run with no request at all
		ps.push_back(startPath(fw, *B, fast));                          // [last] the call as soon as asked: latency, loader arguments and slave bytes
		std::vector<Snap> sn = finishAll(ps); const Snap& k = sn[0]; const Snap& f = sn[1]; const Snap& now = sn.back(); const std::vector<Snap> ctl(sn.begin() + 2, sn.end() - 2);
		CHECK(now.entered == f.entered && now.hitPc == f.hitPc && now.row == f.row && now.col == f.col);
		char what[16]; std::snprintf(what, sizeof what, "%s %d.%d", reg ? "REG" : "PGM", row, col);
		cases++; bool ok;
		if (f.entered) ok = judge(what, k, f, ctl, v3, seen, verbose);
		else {
			// no load: an empty factory slot (the LOAD key shows a message, the program-change routine does nothing), or the program already running and
			// unedited (the LOAD key reloads it, the program-change routine does not). The reference is then the machine left alone: it must be exact.
			const Snap& idle = sn[sn.size() - 2];
			const bool current = B->M->mram[0x9941 - 0x8000] == (reg ? 1 : 0) && B->M->mram[0x993F - 0x8000] == row && B->M->mram[0x993E - 0x8000] == col && !B->M->mram[0x9940 - 0x8000];
			std::printf("  %-8s %s: no load on the program-change path (the LOAD key %s); compared with no request at all\n", what, current ? "is the program running" : "is empty", current ? "reloads it" : "shows a message");
			ok = judge(what, idle, f, std::vector<Snap>(), v3, seen, verbose);
		}
		pass += ok; CHECK(ok);
		if (now.entered) { loads++; keyLat.add(k.entryLat); fastLat.add(now.entryLat); keyHsp.add(k.hspLat); fastHsp.add(now.hspLat); fastDur.add(now.callDur); }
	};
	const char* only = std::getenv("LOAD_ONLY");                      // e.g. "P1.3" or "R2.0" to run one case
	if (only) one(only[0] == 'R', only[1] - '0', only[3] - '0');
	else {
		for (int row = 0; row < 7; row++) for (int col = 0; col < 10; col++) one(false, row, col);
		if (!regs.empty()) {                                           // 12 registers spread over the bank
			std::vector<int> used; for (const Reg& r : regs) if (r.data[0]) used.push_back(r.n);
			const size_t n = std::min<size_t>(12, used.size()); for (size_t i = 0; i < n; i++) { const int r = used[i * used.size() / n]; one(true, r / 10, r % 10); }
			CHECK(n >= 10);
		}
		else std::printf("SKIP V%s registers (MOONVERB_SYX not set)\n", fw.family.c_str());
	}
	std::printf("V%s: %d cases (%d loads, %d empty or unchanged slots), %d pass\n", fw.family.c_str(), cases, loads, cases - loads, pass);
	std::printf("  request -> loader entry      key   %s\n                               fast  %s\n", keyLat.str().c_str(), fastLat.str().c_str());
	std::printf("  request -> program on HSP    key   %s\n                               fast  %s\n", keyHsp.str().c_str(), fastHsp.str().c_str());
	std::printf("  request -> injected call returns   %s\n", fastDur.str().c_str());
	std::string p; char b[64]; for (auto& kv : seen.panel) { std::snprintf(b, sizeof b, " %04X", kv.first); p += b; }
	std::printf("  differed, allowed front-panel addresses:%s\n", p.c_str());
	p.clear(); for (auto& kv : seen.slaveTime) { std::snprintf(b, sizeof b, " %04X", kv.first); p += b; }
	std::printf("  differed, allowed slave time-base addresses:%s\n", p.c_str());
	std::printf("  differed, master stack:%s\n  differed, slave stack:%s\n", addrs(seen.stackM).c_str(), addrs(seen.stackS).c_str());
	std::printf("  differed, varying by themselves in the last 2 s:%s\n", addrs(seen.varying).c_str());
	std::printf("  differed, timing-dependent (also differ between hardware loads at nearby moments):%s\n", addrs(seen.timing).c_str());
}

// MIDI program change n (PGM CHNG on): the firmware's own path into the same routine; measured against the key path for the same register
static void testProgramChange(const Fw& fw, const std::vector<Reg>& regs) {
	const bool v3 = fw.family == "3.01";
	std::vector<uint8_t> ram = bankImage(regs); ram[batram::PGM_CHNG - 0x8000] = 1;
	std::unique_ptr<Rig> B = bootBase(fw, ram);
	Stats lat, hsp; Seen seen; int n = 0, pass = 0;
	for (const Reg& r : regs) {
		if (!r.data[0] || r.n % 6 != 1) continue;
		std::vector<Path> ps;
		ps.push_back(startPath(fw, *B, [&](Rig& R) { R.K->selectRegister(r.n / 10, r.n % 10); }));
		const uint64_t at = ps[0].R->T.hit;
		ps.push_back(startPath(fw, *B, [&](Rig& R) { while (R.M->m.cyc + (uint64_t)(PC_LEAD * HZ) < at) R.tick(); R.Mi->programChange(r.n); }));
		for (double o : PC_OFFSETS) if (o > 0) ps.push_back(startPath(fw, *B, [&, o](Rig& R) { while (R.M->m.cyc + (uint64_t)((PC_LEAD - o) * HZ) < at) R.tick(); R.Mi->programChange(r.n); }));
		for (double d : CONTROL_DELAYS) ps.push_back(startPath(fw, *B, [&, d](Rig& R) { R.run(d); R.K->selectRegister(r.n / 10, r.n % 10); }));
		ps.push_back(startPath(fw, *B, [&](Rig& R) { R.Mi->programChange(r.n); }));
		std::vector<Snap> sn = finishAll(ps); const Snap& k = sn[0]; const Snap& p = sn[1]; const Snap& now = sn.back(); const std::vector<Snap> ctl(sn.begin() + 2, sn.end() - 1);
		CHECK(now.entered && now.m2s == k.m2s && now.hitPc == k.hitPc && now.row == k.row && now.col == k.col);
		char what[16]; std::snprintf(what, sizeof what, "PC %d", r.n);
		n++; const bool ok = judge(what, k, p, ctl, v3, seen, true); pass += ok; CHECK(ok);
		lat.add(now.entryLat); hsp.add(now.hspLat);
	}
	std::printf("V%s MIDI program change: %d of %d equal to the key path; request -> loader entry %s; -> program on HSP %s\n", fw.family.c_str(), pass, n, lat.str().c_str(), hsp.str().c_str());
}

int main() {
	const std::vector<Fw> fws = loadFirmware(); const char* sd = std::getenv("MOONVERB_SYX");
	if (fws.empty()) { std::printf("SKIP load (MOONVERB_ROMS not set or no ROM set found)\n"); return 0; }
	std::vector<std::string> files; if (sd) walk(sd, files);
	for (const Fw& fw : fws) {
		if (fw.family != "2.0" && fw.family != "3.01") continue;
		if (std::getenv("LOAD_FAMILY") && fw.family != std::getenv("LOAD_FAMILY")) continue;
		std::vector<Reg> regs;
		for (const std::string& f : files) if (f.find(fw.family == "2.0" ? "Ver-2.syx" : "Ver-3") != std::string::npos && f.find(".syx") != std::string::npos) regs = parseSyx(readFile(f));
		testFamily(fw, regs);
		if (!regs.empty() && !std::getenv("LOAD_ONLY")) testProgramChange(fw, regs);
	}
	if (failures) { std::printf("%d FAILED\n", failures); return 1; }
	std::printf("ok\n");
	return 0;
}
