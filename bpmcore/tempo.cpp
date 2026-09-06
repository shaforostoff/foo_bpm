#include "internal.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>

namespace bpmcore
{

namespace
{
	// Twelve seconds is around six bars of tango: long enough to resolve the
	// bar, short enough that the tempo inside one window is effectively steady.
	const double acf_window_seconds = 12.0;
	const double acf_hop_seconds    =  3.0;

	const double novelty_local_mean_seconds = 0.6;

	//! Windows below this are not worth a thread hand-off.
	const int min_windows_per_thread = 4;

	// Candidate beat rates. The beat is not what a dancer taps for every rhythm,
	// but it is the level the audio states most clearly, so the search is
	// anchored on it. The slow end is further limited by the autocorrelation
	// only reaching five seconds: eight multiples of the beat have to fit, which
	// puts the real floor near 96 BPM. Every rhythm here sits well above that -
	// a tango beat is 110 to 140, a vals beat around 205.
	const double beat_bpm_min =  70.0;
	const double beat_bpm_max = 280.0;

	//! Log-normal tempo priors fitted to hand tapping, one per rhythm. They only
	//! ever choose between metrical levels, which are a factor of at least 1.33
	//! apart; the value itself always comes from the autocorrelation peak, so a
	//! prior cannot pull a tempo towards its mean.
	//!
	//! `support` is how much the autocorrelation at a level counts against the
	//! prior. Tango, vals and milonga have priors tight enough to settle the
	//! level on their own; "other" spans bossa to disco and has no useful tempo
	//! prior, so there the audio is trusted and the prior only breaks ties.
	struct tempo_prior { double mu; double sigma; double support; };
	const tempo_prior priors[rhythm_class_count] =
	{
		{ 125.5, 0.075, 1.6 },   // tango:   tapped on the beat
		{  68.5, 0.090, 1.6 },   // vals:    tapped once per 3/4 bar
		{  52.5, 0.110, 1.6 },   // milonga: tapped once per 2/4 bar
		{ 110.0, 0.450, 6.0 },   // other:   whatever pulse is most salient
	};

	// Levels the tapped rate may sit on, relative to the beat period. Offering a
	// duple rhythm a division by three is what used to send slow milongas to
	// beat/3 instead of beat/4, so the sets are kept apart.
	//
	// Two beats is not a metrical level of a 3/4 bar, and leaving it in the
	// triple set was enough to take a slow vals - a Peruvian one at 56 to the
	// bar, below anything the Argentine prior expects - and report the
	// two-beat rate instead. It is not offered.
	const double levels_duple[]  = { 1.0/8, 1.0/4, 1.0/2, 1.0, 2.0, 4.0 };
	const double levels_triple[] = { 1.0/6, 1.0/3, 1.0/2, 2.0/3, 1.0, 1.5, 3.0 };

	//! Taps run marginally ahead of the measured pulse across the whole
	//! collection. A calibration to that habit, not a correction to the measurement.
	const double tap_bias = 0.5;

	//! Relative weights of the first eight multiples of the beat period. The
	//! even multiples land on bar lines and score higher in all these rhythms,
	//! so they are weighted above the odd ones.
	const double harmonic_weight[8] = { 1.0, 0.9, 0.6, 0.8, 0.4, 0.5, 0.3, 0.4 };

	//! Centred moving average with zero padding, matching numpy's
	//! convolve(..., mode='same') for an odd kernel.
	void moving_average(const std::vector<float> & in, std::vector<float> & out, int width)
	{
		const int n = static_cast<int>(in.size());
		if (width < 1 || n == 0) { out = in; return; }

		// Prefix sums keep this independent of the window width.
		std::vector<double> prefix(n + 1, 0.0);
		for (int i = 0; i < n; i++) prefix[i + 1] = prefix[i] + in[i];

		out.assign(n, 0.0f);
		const int half = (width - 1) / 2;
		const double inv = 1.0 / width;
		for (int i = 0; i < n; i++)
		{
			const int lo = std::max(0, i - half);
			const int hi = std::min(n, i - half + width);
			out[i] = static_cast<float>((prefix[hi] - prefix[lo]) * inv);
		}
	}

	double population_stddev(const std::vector<float> & x)
	{
		if (x.empty()) return 0.0;
		double sum = 0;
		for (float v : x) sum += v;
		const double mean = sum / x.size();
		double var = 0;
		for (float v : x) { const double d = v - mean; var += d * d; }
		return std::sqrt(var / x.size());
	}

	//! Parabolic refinement of the autocorrelation peak nearest `lag`.
	double refine_peak(const std::vector<double> & r, double lag, double tol, double * peak_out)
	{
		const int n = static_cast<int>(r.size());
		const int lo = std::max(1, static_cast<int>(std::floor(lag * (1.0 - tol))));
		const int hi = std::min(n - 2, static_cast<int>(std::ceil(lag * (1.0 + tol))));
		if (hi <= lo)
		{
			if (peak_out) *peak_out = acf_at(r, lag);
			return lag;
		}
		int j = lo;
		for (int i = lo; i <= hi; i++) if (r[i] > r[j]) j = i;

		if (j >= 1 && j < n - 1)
		{
			const double a = r[j - 1], b = r[j], c = r[j + 1];
			const double d = a - 2 * b + c;
			if (std::fabs(d) > 1e-12)
			{
				const double off = 0.5 * (a - c) / d;
				if (off > -1.0 && off < 1.0)
				{
					if (peak_out) *peak_out = b - 0.25 * (a - c) * off;
					return j + off;
				}
			}
		}
		if (peak_out) *peak_out = r[j];
		return j;
	}
}

const char * rhythm_name(int cls)
{
	switch (cls)
	{
		case rhythm_tango:   return "Tango";
		case rhythm_vals:    return "Vals";
		case rhythm_milonga: return "Milonga";
		default:             return "Other";
	}
}

double lag_to_bpm(double lag, double frame_rate) { return lag > 0 ? 60.0 * frame_rate / lag : 0.0; }
double bpm_to_lag(double bpm, double frame_rate) { return bpm > 0 ? 60.0 * frame_rate / bpm : 0.0; }

double acf_at(const std::vector<double> & acf, double lag)
{
	if (lag < 0) return 0.0;
	const int i = static_cast<int>(lag);
	if (i + 1 >= static_cast<int>(acf.size())) return 0.0;
	const double f = lag - i;
	return acf[i] * (1.0 - f) + acf[i + 1] * f;
}

void mix_bands(const odf & o, std::vector<float> & out)
{
	const int n = o.frames;
	out.assign(std::max(n, 0), 0.0f);
	if (n <= 0) return;

	for (int b = 0; b < odf::band_count; b++)
	{
		const float * src = o.band(b);
		double sum = 0;
		for (int i = 0; i < n; i++) sum += src[i];
		const double mean = sum / n;
		double var = 0;
		for (int i = 0; i < n; i++) { const double d = src[i] - mean; var += d * d; }
		double sd = std::sqrt(var / n);
		if (sd < 1e-9) sd = 1e-9;
		const float inv = static_cast<float>(1.0 / sd);
		for (int i = 0; i < n; i++) out[i] += src[i] * inv;
	}
}

void make_novelty(std::vector<float> & x, double frame_rate)
{
	if (x.empty()) return;

	std::vector<float> tmp;
	moving_average(x, tmp, 3);
	x.swap(tmp);

	int width = static_cast<int>(std::lround(novelty_local_mean_seconds * frame_rate));
	if (width < 3) width = 3;
	if ((width & 1) == 0) width |= 1;

	std::vector<float> local;
	moving_average(x, local, width);

	// Subtracting a local mean turns a loud passage into onsets rather than a
	// plateau; rectifying keeps only what rises above its surroundings.
	for (std::size_t i = 0; i < x.size(); i++)
	{
		const float v = x[i] - local[i];
		x[i] = v > 0.0f ? v : 0.0f;
	}

	const double sd = population_stddev(x);
	if (sd > 1e-9)
	{
		const float inv = static_cast<float>(1.0 / sd);
		for (float & v : x) v *= inv;
	}
}

void autocorrelate(const std::vector<float> & y, std::vector<double> & acf,
                   int max_lag, double frame_rate, int threads)
{
	acf.clear();
	const int n = static_cast<int>(y.size());
	if (n < 16 || max_lag < 8) return;

	int W = static_cast<int>(std::lround(acf_window_seconds * frame_rate));
	const int H = std::max(1, static_cast<int>(std::lround(acf_hop_seconds * frame_rate)));
	int L = max_lag;
	if (n < W) W = n;
	if (W <= L + 8) L = std::max(8, W - 8);
	if (L < 8) return;

	std::vector<int> starts;
	for (int s = 0; s + W <= n; s += H) starts.push_back(s);
	if (starts.empty()) starts.push_back(0);

	// One autocorrelation per window, kept so they can be reduced lag by lag.
	// The windows do not interact, so they are computed across cores; a slot is
	// reserved per window rather than appended to, which keeps the result
	// independent of the thread count.
	std::vector<std::vector<double> > slots(starts.size());
	const int n_threads = resolve_threads(threads, static_cast<int>(starts.size()),
	                                      min_windows_per_thread);
	parallel_blocks(static_cast<int>(starts.size()), n_threads, [&](int w)
	{
		const int s = starts[w];
		const int len = std::min(W, n - s);
		if (len <= L + 8) return;

		std::vector<double> seg(len);
		double sum = 0;
		for (int i = 0; i < len; i++) sum += y[s + i];
		const double mean = sum / len;
		double var = 0;
		for (int i = 0; i < len; i++) { seg[i] = y[s + i] - mean; var += seg[i] * seg[i]; }
		if (var / len < 1e-18) return;

		std::vector<double> r(L, 0.0);
		// Direct evaluation. At 86 frames a second this is a few hundred
		// thousand multiply-adds per window - two unit-stride reads that any
		// compiler vectorises - and far cheaper than the transform that fed it.
		for (int lag = 0; lag < L; lag++)
		{
			const double * a = seg.data();
			const double * b = seg.data() + lag;
			const int m = len - lag;
			double acc = 0;
			for (int t = 0; t < m; t++) acc += a[t] * b[t];
			// Unbiased: every lag averages over a different number of products.
			r[lag] = acc / std::max(m, 1);
		}
		if (r[0] > 1e-12)
		{
			const double inv = 1.0 / r[0];
			for (double & v : r) v *= inv;
		}
		slots[w] = std::move(r);
	});

	// A window with no periodicity in it - a beatless introduction, a run of
	// digital silence - leaves its slot empty and drops out here.
	std::vector<std::vector<double> > per_window;
	per_window.reserve(slots.size());
	for (std::size_t w = 0; w < slots.size(); w++)
		if (!slots[w].empty()) per_window.push_back(std::move(slots[w]));

	if (per_window.empty()) return;

	acf.assign(L, 0.0);
	std::vector<double> column(per_window.size());
	for (int lag = 0; lag < L; lag++)
	{
		for (std::size_t w = 0; w < per_window.size(); w++) column[w] = per_window[w][lag];
		std::sort(column.begin(), column.end());
		const std::size_t m = column.size();
		acf[lag] = (m & 1) ? column[m / 2] : 0.5 * (column[m / 2 - 1] + column[m / 2]);
	}
}

grid find_grid(const std::vector<double> & r, double frame_rate)
{
	grid best;
	const int n = static_cast<int>(r.size());
	if (n < 24) return best;

	const double lag_lo = bpm_to_lag(beat_bpm_max, frame_rate);
	// Eight multiples of the beat have to stay inside the autocorrelation, so
	// refine_period below has a full harmonic comb to fit.
	const double lag_hi = std::min(bpm_to_lag(beat_bpm_min, frame_rate), (n - 2) / 8.0);
	if (lag_hi <= lag_lo) return best;

	static const int meters[] = { 2, 3, 4, 6 };
	double best_score = -1e18;
	double best_lag = 0;
	int best_meter = 4;

	for (double lag = lag_lo; lag < lag_hi; lag += 0.25)
	{
		for (int mi = 0; mi < 4; mi++)
		{
			const int m = meters[mi];
			const double bar = lag * m;
			if (bar * 2 + 1 >= n) continue;

			// Support on the grid: the beat, the bar, two bars, and the halfway
			// or third-way subdivision the meter implies.
			double num = acf_at(r, lag) + acf_at(r, bar) + 0.6 * acf_at(r, 2 * bar);
			double den = 2.6;
			if (m % 2 == 0) { num += 0.7 * acf_at(r, bar / 2); den += 0.7; }
			if (m % 3 == 0) { num += 0.7 * acf_at(r, bar / 3); den += 0.7; }
			double score = num / den;

			// A grid that leaves a strong periodicity sitting off it is probably
			// the wrong grid.
			const double off = (m == 3)
				? acf_at(r, lag * 4.0 / 3.0)
				: std::max(acf_at(r, lag * 1.5), acf_at(r, lag * 2.5));
			score -= 0.25 * std::max(0.0, off - score);

			if (score > best_score)
			{
				best_score = score;
				best_lag = lag;
				best_meter = m;
			}
		}
	}

	if (best_lag <= 0) return best;

	double peak = 0;
	best.beat_lag = refine_peak(r, best_lag, 0.03, &peak);
	best.meter = best_meter;
	best.score = best_score;
	best.beat_acf = peak;
	return best;
}

double refine_period(const std::vector<double> & r, double lag0)
{
	if (lag0 <= 0) return lag0;
	const int n = static_cast<int>(r.size());

	// A single autocorrelation peak is a couple of frames wide; its multiples
	// are not, so fitting the whole comb pins the period far more tightly.
	const double span = 0.06;
	const int steps = 241;
	double best = -1e18, best_lag = lag0;
	for (int s = 0; s < steps; s++)
	{
		const double lag = lag0 * (1.0 - span) + (2.0 * span * lag0) * s / (steps - 1);
		double sum = 0, weight = 0;
		for (int k = 1; k <= 8; k++)
		{
			const double x = k * lag;
			if (x + 1 >= n) break;
			sum += harmonic_weight[k - 1] * acf_at(r, x);
			weight += harmonic_weight[k - 1];
		}
		if (weight > 0)
		{
			const double score = sum / weight;
			if (score > best) { best = score; best_lag = lag; }
		}
	}
	return best_lag;
}

double tapped_bpm(const std::vector<double> & r, double beat_lag, int rhythm,
                  int meter, double frame_rate)
{
	if (beat_lag <= 0 || r.empty()) return 0.0;

	const double lag = refine_period(r, beat_lag);

	// The three tango rhythms state their own metre, whatever the grid search
	// made of it. "Other" is everything from chacarera to disco and states
	// nothing, so there the detected metre decides - which keeps a duple piece
	// off the two-thirds level. Reading a son at two thirds of its beat was
	// what put Chan Chan at 112 and Guantanamera at 83.
	const bool triple = rhythm == rhythm_vals ||
	                    (rhythm != rhythm_tango && rhythm != rhythm_milonga &&
	                     (meter == 3 || meter == 6));

	const double * levels = triple ? levels_triple : levels_duple;
	const std::size_t level_count = triple
		? sizeof(levels_triple) / sizeof(levels_triple[0])
		: sizeof(levels_duple) / sizeof(levels_duple[0]);

	const tempo_prior & prior =
		priors[(rhythm >= 0 && rhythm < rhythm_class_count) ? rhythm : rhythm_other];
	const double log_mu = std::log(prior.mu);

	double best = -1e18, result = 0;
	for (std::size_t i = 0; i < level_count; i++)
	{
		const double L = lag * levels[i];
		const double bpm = lag_to_bpm(L, frame_rate);
		if (bpm < 25.0 || bpm > 320.0) continue;

		const double support = acf_at(r, L);
		const double z = (std::log(bpm) - log_mu) / prior.sigma;
		const double score = prior.support * support - 0.5 * z * z;
		if (score > best) { best = score; result = bpm; }
	}

	return result > 0 ? result + tap_bias : 0.0;
}

}   // namespace bpmcore
