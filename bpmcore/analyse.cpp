#include "internal.h"

#include <algorithm>
#include <cmath>

namespace bpmcore
{

namespace
{
	// Five seconds of lag covers two bars of the slowest vals and eight beats of
	// the fastest milonga.
	const double acf_max_lag_seconds = 5.0;

	const double buffer_max_seconds = 900.0;
}

double max_seconds() { return buffer_max_seconds; }

analysis analyse(const float * mono, std::size_t count, unsigned sample_rate,
                 listener * l, const options * opt)
{
	analysis result;
	const int threads = opt != nullptr ? opt->threads : 0;

	odf o;
	if (!compute_odf(mono, count, sample_rate, o, l, threads)) return result;
	result.duration = o.duration;

	std::vector<float> novelty;
	mix_bands(o, novelty);
	make_novelty(novelty, o.frame_rate);

	std::vector<double> acf;
	autocorrelate(novelty, acf,
	              static_cast<int>(std::lround(acf_max_lag_seconds * o.frame_rate)),
	              o.frame_rate);
	if (acf.empty()) return result;

	if (l != nullptr && l->cancelled()) return result;

	const grid g = find_grid(acf, o.frame_rate);
	if (g.beat_lag <= 0) return result;

	// The rhythm has to be settled before the tempo can be, because the
	// metrical level a dancer taps is different for each of them.
	std::vector<double> features;
	build_features(o, novelty, acf, g, features);
	result.rhythm = classify(features, &result.confidence);

	result.beat_bpm = lag_to_bpm(g.beat_lag, o.frame_rate);
	result.meter = g.meter;
	result.bpm = tapped_bpm(acf, g.beat_lag, result.rhythm, g.meter, o.frame_rate);
	result.ok = result.bpm > 0;

	if (l != nullptr) l->progress(1.0);
	return result;
}

collector::collector(unsigned sample_rate)
	: m_rate(sample_rate),
	  m_limit(static_cast<std::size_t>(buffer_max_seconds * (sample_rate ? sample_rate : 1)))
{
	// Three minutes covers almost every tango side; the buffer grows past this
	// only for the rare long track.
	m_mono.reserve(std::min<std::size_t>(m_limit, static_cast<std::size_t>(m_rate) * 240));
}

namespace
{
	template<typename sample_t>
	void append_mono(std::vector<float> & mono, std::size_t limit,
	                 const sample_t * data, std::size_t count)
	{
		if (data == nullptr || mono.size() >= limit) return;
		const std::size_t take = std::min(count, limit - mono.size());
		mono.reserve(mono.size() + take);
		for (std::size_t i = 0; i < take; i++) mono.push_back(static_cast<float>(data[i]));
	}

	template<typename sample_t>
	void append_interleaved(std::vector<float> & mono, std::size_t limit,
	                        const sample_t * data, std::size_t frames, unsigned channels)
	{
		if (data == nullptr || channels == 0 || mono.size() >= limit) return;
		const std::size_t take = std::min(frames, limit - mono.size());
		const double scale = 1.0 / channels;
		mono.reserve(mono.size() + take);
		for (std::size_t i = 0; i < take; i++)
		{
			double sum = 0;
			for (unsigned c = 0; c < channels; c++) sum += *data++;
			mono.push_back(static_cast<float>(sum * scale));
		}
	}
}

void collector::add_mono(const float * data, std::size_t count)
{
	append_mono(m_mono, m_limit, data, count);
}

void collector::add_mono(const double * data, std::size_t count)
{
	append_mono(m_mono, m_limit, data, count);
}

void collector::add_interleaved(const float * data, std::size_t frames, unsigned channels)
{
	append_interleaved(m_mono, m_limit, data, frames, channels);
}

void collector::add_interleaved(const double * data, std::size_t frames, unsigned channels)
{
	append_interleaved(m_mono, m_limit, data, frames, channels);
}

analysis collector::finish(listener * l, const options * opt)
{
	return analyse(m_mono.data(), m_mono.size(), m_rate, l, opt);
}

}   // namespace bpmcore
