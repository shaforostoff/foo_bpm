#ifndef BPMCORE_INTERNAL_H
#define BPMCORE_INTERNAL_H

// Shared internals of the analysis. Not part of the public interface, but split
// across translation units - resampling, envelope, tempo, rhythm - so each
// stays readable on its own.

#include "bpmcore.h"

#include <cstdint>

namespace bpmcore
{

//! Multi-band onset detection function.
//!
//! The 2009 analysis summed a broadband spectral flux weighted by bin index,
//! which puts most of the weight on the top of the spectrum. On a shellac
//! transfer the top of the spectrum is surface noise, so the beat it tracked
//! was largely hiss. One flux envelope per frequency band is produced instead,
//! and the weighting is left to the tempo stage, which normalises each band by
//! its own variation before mixing.
struct odf
{
	enum { band_count = 6 };

	int frames = 0;
	double frame_rate = 0;    //!< actual frames per second, sample_rate / hop
	double duration = 0;
	std::vector<float> data;  //!< band_count rows of `frames` samples

	const float * band(int b) const { return &data[static_cast<std::size_t>(b) * frames]; }
	bool empty() const { return frames <= 0; }
};

//! Analysis geometry, fixed in seconds rather than samples, so that the window
//! and hop describe the same stretch of time whatever the input rate.
extern const double odf_window_seconds;   //!< 1024 / 22050 = 46.4ms
extern const double odf_hop_seconds;      //!< 256 / 22050 = 11.6ms
extern const double odf_band_edges_hz[odf::band_count + 1];
//! Magnitudes are compressed as log(1 + gamma * m) before differencing, so a
//! quiet passage contributes onsets on the same scale as a loud one.
extern const double odf_gamma;

//! The rate the rhythm model was fitted at, and the rate the analysis runs at.
extern const unsigned odf_model_rate;     //!< 22050

//! True when `rate` reproduces the model's analysis with no resampling.
//!
//! Seconds-based geometry gets the window and hop right in time at any rate,
//! but the window still has to be a power of two, so only a rate that is the
//! model rate times a power of two gives both the model's 46.4ms window and its
//! 21.53Hz per bin. 11.025, 44.1 and 88.2kHz do; 48kHz does not - it is handed
//! a 2048-point window covering 42.7ms, which is a different analysis from the
//! same track at 44.1kHz.
bool rate_matches_model(unsigned rate);

//! Rational polyphase resampler, used to bring any rate to `odf_model_rate`.
//!
//! Streaming, so the component can convert as the decoder produces audio and
//! hold only the analysis-rate buffer; feeding everything in one call and then
//! flushing gives the identical result, which is what keeps `analyse` and
//! `collector` in step.
class resampler
{
public:
	//! `valid()` is false if the rates are unusable, in which case the caller
	//! should analyse at the input rate rather than not at all.
	resampler(unsigned from, unsigned to);

	bool valid() const { return m_phases > 0; }
	unsigned rate_out() const { return m_rate_out; }
	//! Output samples `in` input samples produce once flushed.
	std::size_t expected_output(std::size_t in) const;

	//! Converts `count` samples, appending to `out`.
	void process(const float * in, std::size_t count, std::vector<float> & out);
	//! Emits the tail, so the output covers the input's duration exactly.
	void flush(std::vector<float> & out);

private:
	int m_phases = 0;              //!< interpolation factor, L
	int m_decim = 0;               //!< decimation factor, M
	int m_taps = 0;                //!< coefficients per phase
	std::int64_t m_delay = 0;      //!< group delay, in L * rate_in samples
	unsigned m_rate_out = 0;
	std::vector<float> m_coeff;    //!< m_phases rows of m_taps, time-reversed
	std::vector<float> m_hist;     //!< input samples the next block reaches back over
	std::vector<float> m_work;
	std::int64_t m_consumed = 0;
	std::int64_t m_produced = 0;
};

//! `threads` is 0 for automatic, 1 to stay on the calling thread. The result
//! does not depend on it.
bool compute_odf(const float * mono, std::size_t count, unsigned sample_rate,
                 odf & out, listener * l, int threads = 0);

//! The metrical grid the audio settles on.
struct grid
{
	double beat_lag = 0;   //!< beat period, in ODF frames
	int meter = 4;         //!< beats per bar: 2, 3, 4 or 6
	double score = 0;      //!< how well the grid explains the autocorrelation
	double beat_acf = 0;   //!< autocorrelation at the beat period
};

//! Sum the bands after putting each on the same footing.
void mix_bands(const odf & o, std::vector<float> & out);
//! Smooth, subtract a local mean, half-wave rectify, normalise.
void make_novelty(std::vector<float> & x, double frame_rate);
//! Autocorrelation over overlapping windows, reduced lag by lag with a median.
//!
//! The median across windows is what lets a passage that drops tempo for a few
//! bars - a bandoneon variation, a singer's rubato - pass without dragging the
//! answer down, which is what the hand tapping did too.
void autocorrelate(const std::vector<float> & y, std::vector<double> & acf,
                   int max_lag, double frame_rate);
//! Joint search over beat period and meter.
grid find_grid(const std::vector<double> & acf, double frame_rate);
//! Sharpen a beat period against every harmonic of itself at once.
double refine_period(const std::vector<double> & acf, double lag0);
//! Choose the metrical level the user would have tapped, given the rhythm.
//!
//! `meter` is only consulted for `rhythm_other`, which has no tapping
//! convention of its own; the three tango rhythms carry their own level set.
double tapped_bpm(const std::vector<double> & acf, double beat_lag, int rhythm,
                  int meter, double frame_rate);

double lag_to_bpm(double lag, double frame_rate);
double bpm_to_lag(double bpm, double frame_rate);
//! Linear interpolation into the autocorrelation at a fractional lag.
double acf_at(const std::vector<double> & acf, double lag);

//! Feature vector layout. The Python trainer builds the same vector in the same
//! order, so weights transfer without a mapping table.
enum
{
	rel_lag_count = 24,
	bar_bins = 24,
	beat_bins = 8,
	fixed_fold_count = 3,
	fixed_group_count = 3,
	feature_count = 4 + rel_lag_count + 3 + 2 * odf::band_count + 3
	                + odf::band_count * bar_bins
	                + odf::band_count * beat_bins
	                + fixed_group_count * (12 + 12 + 16)
};

extern const int fixed_fold_mult[fixed_fold_count];
extern const int fixed_fold_bins[fixed_fold_count];

//! Average one period of onset energy per band, rotated so the strongest
//! low-band accent lands in bin 0.
void fold_pattern(const float * const * bands, int band_count, int frames,
                  double period, int bins, std::vector<float> & out);

void build_features(const odf & o, const std::vector<float> & novelty,
                    const std::vector<double> & acf, const grid & g,
                    std::vector<double> & out);

//! Returns a rhythm_class; `confidence` receives the winning probability.
int classify(const std::vector<double> & features, double * confidence);

}   // namespace bpmcore

#endif // BPMCORE_INTERNAL_H
