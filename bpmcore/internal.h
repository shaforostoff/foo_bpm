#ifndef BPMCORE_INTERNAL_H
#define BPMCORE_INTERNAL_H

// Shared internals of the analysis. Not part of the public interface, but split
// across three translation units - envelope, tempo, rhythm - so each stays
// readable on its own.

#include "bpmcore.h"

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

//! Analysis geometry, fixed in seconds rather than samples so that a track
//! decoded at 44100Hz and the same track at 22050Hz give the same envelope -
//! a host cannot be assumed to have a resampler.
extern const double odf_window_seconds;   //!< 1024 / 22050 = 46.4ms
extern const double odf_hop_seconds;      //!< 256 / 22050 = 11.6ms
extern const double odf_band_edges_hz[odf::band_count + 1];
//! Magnitudes are compressed as log(1 + gamma * m) before differencing, so a
//! quiet passage contributes onsets on the same scale as a loud one.
extern const double odf_gamma;

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
double tapped_bpm(const std::vector<double> & acf, double beat_lag, int rhythm,
                  double frame_rate);

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
