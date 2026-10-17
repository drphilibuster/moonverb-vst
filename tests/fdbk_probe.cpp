// Research probe (not part of `make test`): James's CORSENECO BPM patch in Live, FDBK MST stepped -4 -> 0 -> -4 -> +2 -> -4, 10 s each, with noise bursts in.
// Per half second: wet level, wet DC, how much of the delay memory sits at the rails, and the six feedback words. Needs $MOONVERB_ROMS (a V2.0 set).
#define private public
#include "fixtures.hpp"
#include "../src/core/Voice.hpp"
#undef private
#include <cmath>
#include <memory>
#include <random>

using namespace moonverb; using namespace fx;

int main(int argc, char** argv) {
	const char* rd = std::getenv("MOONVERB_ROMS"); if (!rd) { std::printf("SKIP\n"); return 0; }
	(void)rd; std::vector<Fw> fws = loadFirmware(); const Fw* fw = nullptr; for (const Fw& f : fws) if (f.family == "2.0" && !f.u67.empty()) fw = &f;
	if (!fw) { std::printf("no V2 set\n"); return 1; }
	const double host = 48000.0;
	std::unique_ptr<Voice> V(new Voice()); V->fullScaleVolts = 1.0;
	V->load(fw->u62.data(), fw->u62.size(), fw->u95.data(), fw->u95.size(), fw->u67.data(), fw->u48.data(), fw->u49.data()); V->configure(host);
	double l, r; struct { double on, off, corr; } last = { 0, 0, 0 };
	auto run = [&](double s, double amp) {
		static std::mt19937 g(1); std::normal_distribution<double> n(0.0, 1.0);
		static long long t = 0; const long long N = (long long)(s * host);
		double acc = 0, dc = 0, accIn = 0, xy = 0, on2 = 0, off2 = 0; long long k = 0, kon = 0, koff = 0;
		for (long long i = 0; i < N; i++, t++) {
			const bool on = (t % (long long)host) < (long long)(0.15 * host);
			const double x = on ? amp * 0.3 * n(g) : 0.0;
			V->process(x, l, r); acc += V->wetL * V->wetL; dc += V->wetL; accIn += x * x; xy += x * V->wetL; k++; if (on) { on2 += V->wetL * V->wetL; kon++; } else { off2 += V->wetL * V->wetL; koff++; }
		}
		last = { std::sqrt(on2 / std::max(1LL, kon)), std::sqrt(off2 / std::max(1LL, koff)), xy / std::sqrt(std::max(1e-30, acc * accIn)) };
		return std::make_pair(std::sqrt(acc / std::max(1LL, k)), dc / std::max(1LL, k));
	};
	run(9.6, 0.0);
	auto settle = [&]() { for (int i = 0; i < 100 && (V->control.busy() || V->keys.busy() || !V->control.tableReady()); i++) run(0.1, 0.0); run(0.3, 0.0); };
	V->control.loadProgram(0, 9); run(0.5, 0.0); settle();
	std::printf("loaded: \"%s\"\n", V->control.displayText().c_str());
	// the patch's host values (Live), row-major 5 x 9
	const double vals[45] = { 1.0, 0.926257312297821, 0.91015625, 0.6060606241226196, 0.3333333432674408, 0.8333333134651184, 0.4449218809604645, 0.5, 0.5,
		0.5, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.5, 0.5,
		0.28484398126602173, 0.2581420838832855, 0.657116711139679, 0.7601969838142395, 0.6576172113418579, 0.6666666865348816, 0.505078136920929, 0.5, 0.5,
		0.48969072103500366, 0.6701030731201172, 0.6340206265449524, 0.7525773048400879, 0.6907216310501099, 0.7628865838050842, 0.5979381203651428, 0.5, 0.5,
		0.5, 0.009999999776482582, 0.2, 0.4, 0.6, 0.8, 1.0, 0.5, 0.5 };
	// set every cell but the masters first, then the masters (as the patch was built: children edited, then FDBK MST pulled to -4)
	for (int pass = 0; pass < 2; pass++) {
		for (int rr = 0; rr < 5; rr++) for (int cc = 0; cc < 9; cc++) {
			const Cell& c = V->control.cell(rr, cc); if (!c.editable() || (rr == 0 && cc == 9)) continue;
			if ((pass == 1) == c.master) continue;      // the masters first: the host values are the words as they stand, children included
			V->control.setNormalized(rr, cc, vals[rr * 9 + cc]);
		}
		for (int i = 0; i < 30; i++) run(0.1, 0.0);                  // let the firmware apply this pass before the next
	}
	for (int i = 0; i < 40; i++) run(0.1, 0.0);
	auto words = [&]() { std::string s; for (int cc = 0; cc < 7; cc++) { char b[24]; std::snprintf(b, sizeof b, "%d%s", V->control.cell(3, cc).word, cc ? " " : " | "); s += b; } return s; };
	const Cell& m = V->control.cell(3, 0);
	std::printf("FDBK MST word %d (lo %d hi %d), cells 3.x: %s  caption \"%s\"\n", m.word, m.lo, m.hi, words().c_str(), m.caption.c_str());
	for (int cc = 1; cc < 7; cc++) { const Cell& c = V->control.cell(3, cc); std::printf("  3.%d %.13s lo %d hi %d word %d \"%s\"\n", cc, c.text, c.lo, c.hi, c.word, c.caption.c_str()); }
	const int base = m.word;
	const int steps[] = { +6, 0 };           // -4 -> 0 -> -4 -> +2 -> -4 (one word = one unit of the master)
	for (int si = 0; si < 2; si++) {
		V->control.setTarget(3, 0, base + steps[si]); run(0.3, 0.0);
		std::printf("FDBK MST -> word %d (%+d): children %s\n", V->control.cell(3, 0).word, steps[si] - 4, words().c_str());
		for (int w = 0; w < (si == 0 ? 40 : 20); w++) {
			const auto res = run(1.0, 1.0);
			int rail = 0, big = 0; for (double x : V->hsp.mem) { if (std::fabs(x) >= 0.999) rail++; if (std::fabs(x) >= 0.5) big++; }
			std::printf("  t=%4.1f  wet rms %.4f (in bursts %.4f, between %.4f) corr %+.3f  rails %6d  mix codes wet %d dry %d  bypass %d\n", 1.0 * (w + 1), res.first, last.on, last.off, last.corr, rail, V->machine.mixWet, V->machine.mixDry, (int)V->control.bypassed());
		}
	}
	return 0;
}
