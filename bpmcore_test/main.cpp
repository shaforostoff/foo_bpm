// Verification and benchmark harness for bpmcore.
//
// It links the analysis and nothing else - no foobar2000, no pfc - which is
// both the point of the split and what lets this run in CI.
//
//   model <cases file>
//       feed stored feature vectors straight to the classifier and check the
//       exported trees reproduce the class and probability scikit-learn gave.
//
//   resample
//       check the resampler, and that one synthesised track analyses the same
//       at every input rate. Needs no audio on disk.
//
//   pipeline <raw f32 mono file> <sample rate>
//       run the whole chain and print the result, for comparison against the
//       Python reference implementation the model was developed with.
//
//   bench <raw f32 mono file> <sample rate> [repeats]
//       time the analysis.

#include <bpmcore/bpmcore.h>
// The harness reaches past the public interface for the classifier check, so
// that the feature count and the tree walk are not restated here.
#include <bpmcore/internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	std::string lower(std::string s)
	{
		for (char & c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}

	bool read_pcm(const char * path, std::vector<float> & out)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return false;
		in.seekg(0, std::ios::end);
		const std::streamoff bytes = in.tellg();
		in.seekg(0, std::ios::beg);
		if (bytes <= 0) return false;
		out.assign(static_cast<std::size_t>(bytes) / sizeof(float), 0.0f);
		in.read(reinterpret_cast<char *>(out.data()),
		        static_cast<std::streamsize>(out.size() * sizeof(float)));
		return in.good() || in.eof();
	}

	int run_pipeline(const char * path, unsigned sample_rate, int threads)
	{
		std::vector<float> mono;
		if (!read_pcm(path, mono))
		{
			std::fprintf(stderr, "cannot read %s\n", path);
			return 2;
		}
		bpmcore::options opt; opt.threads = threads;
		const bpmcore::analysis a = bpmcore::analyse(mono.data(), mono.size(), sample_rate, nullptr, &opt);
		if (!a.ok)
		{
			std::fprintf(stderr, "analysis failed\n");
			return 3;
		}
		// bpm rhythm confidence beat_bpm meter duration
		std::printf("%.6f %s %.6f %.6f %d %.3f\n", a.bpm, bpmcore::rhythm_name(a.rhythm),
		            a.confidence, a.beat_bpm, a.meter, a.duration);
		return 0;
	}

	int run_bench(const char * path, unsigned sample_rate, int repeats, int threads)
	{
		std::vector<float> mono;
		if (!read_pcm(path, mono))
		{
			std::fprintf(stderr, "cannot read %s\n", path);
			return 2;
		}
		const double audio_seconds = static_cast<double>(mono.size()) / sample_rate;
		double best = 1e18, total = 0;
		bpmcore::analysis a;
		for (int i = 0; i < repeats; i++)
		{
			const auto t0 = std::chrono::steady_clock::now();
			bpmcore::options opt; opt.threads = threads;
			a = bpmcore::analyse(mono.data(), mono.size(), sample_rate, nullptr, &opt);
			const double dt = std::chrono::duration<double>(
				std::chrono::steady_clock::now() - t0).count();
			best = std::min(best, dt);
			total += dt;
		}
		std::printf("threads=%d audio=%.1fs best=%.3fs mean=%.3fs realtime=%.0fx bpm=%.2f %s\n",
		            threads, audio_seconds, best, total / repeats, audio_seconds / best,
		            a.bpm, bpmcore::rhythm_name(a.rhythm));
		return 0;
	}

	//! Stage-by-stage timings, to show where the run time actually goes.
	int run_profile(const char * path, unsigned sample_rate, int repeats)
	{
		std::vector<float> mono;
		if (!read_pcm(path, mono))
		{
			std::fprintf(stderr, "cannot read %s\n", path);
			return 2;
		}
		const double audio = static_cast<double>(mono.size()) / sample_rate;

		double t_odf = 1e18, t_nov = 1e18, t_acf = 1e18, t_grid = 1e18, t_feat = 1e18, t_cls = 1e18;
		auto now = [] { return std::chrono::steady_clock::now(); };
		auto secs = [](std::chrono::steady_clock::time_point a,
		               std::chrono::steady_clock::time_point b)
		{ return std::chrono::duration<double>(b - a).count(); };

		for (int i = 0; i < repeats; i++)
		{
			bpmcore::odf o;
			auto t0 = now();
			if (!bpmcore::compute_odf(mono.data(), mono.size(), sample_rate, o, nullptr)) return 3;
			auto t1 = now();

			std::vector<float> novelty;
			bpmcore::mix_bands(o, novelty);
			bpmcore::make_novelty(novelty, o.frame_rate);
			auto t2 = now();

			std::vector<double> acf;
			bpmcore::autocorrelate(novelty, acf,
				static_cast<int>(std::lround(5.0 * o.frame_rate)), o.frame_rate);
			auto t3 = now();

			const bpmcore::grid g = bpmcore::find_grid(acf, o.frame_rate);
			auto t4 = now();

			std::vector<double> features;
			bpmcore::build_features(o, novelty, acf, g, features);
			auto t5 = now();

			double conf = 0;
			const int cls = bpmcore::classify(features, &conf);
			bpmcore::tapped_bpm(acf, g.beat_lag, cls, g.meter, o.frame_rate);
			auto t6 = now();

			t_odf = std::min(t_odf, secs(t0, t1));
			t_nov = std::min(t_nov, secs(t1, t2));
			t_acf = std::min(t_acf, secs(t2, t3));
			t_grid = std::min(t_grid, secs(t3, t4));
			t_feat = std::min(t_feat, secs(t4, t5));
			t_cls = std::min(t_cls, secs(t5, t6));
		}
		const double sum = t_odf + t_nov + t_acf + t_grid + t_feat + t_cls;
		std::printf("audio=%.1fs total=%.4fs (%.0fx realtime)\n", audio, sum, audio / sum);
		const char * names[] = { "envelope", "novelty", "autocorr", "grid", "features", "classify" };
		const double times[] = { t_odf, t_nov, t_acf, t_grid, t_feat, t_cls };
		for (int i = 0; i < 6; i++)
			std::printf("  %-9s %7.4fs  %5.1f%%\n", names[i], times[i], 100.0 * times[i] / sum);
		return 0;
	}

	const double test_pi = 3.14159265358979323846;

	//! A band-limited beat pattern, evaluated from time rather than sampled, so
	//! the same music can be produced at any rate.
	//!
	//! Every partial is under 4kHz and every envelope is smooth, so the highest
	//! rate here and the lowest represent it identically - which is what lets a
	//! cross-rate difference be blamed on the analysis rather than on the signal.
	void synth_beats(std::vector<float> & out, unsigned rate, double seconds,
	                 double beat_bpm, int meter)
	{
		const std::size_t n = static_cast<std::size_t>(seconds * rate);
		const double beat = 60.0 / beat_bpm;
		// One partial per band, so mix_bands has something in each of the six.
		const double partials[6] = { 80.0, 300.0, 640.0, 1000.0, 2200.0, 4000.0 };
		const double weights[6]  = { 1.00, 0.45, 0.40, 0.30, 0.25, 0.18 };
		out.assign(n, 0.0f);
		for (std::size_t i = 0; i < n; i++)
		{
			const double t = static_cast<double>(i) / rate;
			const double into = std::fmod(t, beat);
			if (into >= 0.12) continue;
			// Raised cosine in, exponential out: continuous, with a continuous
			// derivative at both ends, so the spectrum decays fast.
			const double env = 0.5 * (1.0 - std::cos(2.0 * test_pi * into / 0.12))
			                   * std::exp(-12.0 * into);
			double v = 0;
			for (int b = 0; b < 6; b++)
				v += weights[b] * std::sin(2.0 * test_pi * partials[b] * t);
			const long index = static_cast<long>(t / beat);
			const double accent = (index % meter) == 0 ? 1.0 : 0.55;
			out[i] = static_cast<float>(0.3 * accent * env * v);
		}
	}

	void synth_sine(std::vector<float> & out, unsigned rate, double seconds, double hz)
	{
		const std::size_t n = static_cast<std::size_t>(seconds * rate);
		out.assign(n, 0.0f);
		for (std::size_t i = 0; i < n; i++)
			out[i] = static_cast<float>(std::sin(2.0 * test_pi * hz * i / rate));
	}

	//! RMS of the middle half, which leaves out the filter's transient at each end.
	double middle_rms(const std::vector<float> & x)
	{
		if (x.size() < 8) return 0.0;
		const std::size_t lo = x.size() / 4, hi = x.size() - x.size() / 4;
		double sum = 0;
		for (std::size_t i = lo; i < hi; i++) sum += static_cast<double>(x[i]) * x[i];
		return std::sqrt(sum / (hi - lo));
	}

	std::vector<float> convert(unsigned from, unsigned to,
	                           const std::vector<float> & in, std::size_t block)
	{
		bpmcore::resampler rs(from, to);
		std::vector<float> out;
		if (!rs.valid() || block == 0) return out;
		out.reserve(rs.expected_output(in.size()));
		for (std::size_t at = 0; at < in.size(); at += block)
			rs.process(in.data() + at, std::min(block, in.size() - at), out);
		rs.flush(out);
		return out;
	}

	//! Checks the resampler, and the rate independence it buys.
	//!
	//! Every signal is synthesised here rather than read from disk, which is the
	//! point: this runs in CI with no audio to hand.
	int run_resample()
	{
		int failures = 0;
		int checks = 0;
		auto check = [&](bool ok, const char * what)
		{
			checks++;
			if (!ok) { std::fprintf(stderr, "resample: %s\n", what); failures++; }
		};

		// Which rates reproduce the model's analysis untouched.
		const unsigned exact[] = { 11025, 22050, 44100, 88200, 176400 };
		for (std::size_t i = 0; i < sizeof(exact) / sizeof(exact[0]); i++)
			check(bpmcore::rate_matches_model(exact[i]), "an exact rate was thought inexact");
		const unsigned inexact[] = { 0, 8000, 16000, 24000, 32000, 48000, 96000, 192000, 44056 };
		for (std::size_t i = 0; i < sizeof(inexact) / sizeof(inexact[0]); i++)
			check(!bpmcore::rate_matches_model(inexact[i]), "an inexact rate was thought exact");

		// Blocking must not change a sample. Each output reads the same
		// coefficients against the same input whatever the block size, so this is
		// an equality and not a tolerance.
		std::vector<float> beats;
		synth_beats(beats, 48000, 3.0, 124.0, 4);
		const std::vector<float> whole = convert(48000, 22050, beats, beats.size());
		check(!whole.empty(), "48kHz could not be converted at all");
		const std::size_t blocks[] = { 1, 7, 577, 4096 };
		for (std::size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++)
			check(convert(48000, 22050, beats, blocks[i]) == whole,
			      "streaming and one-shot conversion differ");

		// The output has to cover the input's duration, to the sample.
		const bpmcore::resampler geometry(48000, 22050);
		check(whole.size() == geometry.expected_output(beats.size()),
		      "converted length is not the expected length");
		check(std::fabs(static_cast<double>(whole.size()) / 22050.0 -
		                static_cast<double>(beats.size()) / 48000.0) <= 1.0 / 22050.0,
		      "converted duration does not match the input duration");

		// Flat to the top band edge, which is as high as the envelope reads.
		const double pass_hz[] = { 100.0, 1000.0, 5000.0, 7900.0 };
		for (std::size_t i = 0; i < sizeof(pass_hz) / sizeof(pass_hz[0]); i++)
		{
			std::vector<float> sine;
			synth_sine(sine, 48000, 2.0, pass_hz[i]);
			const double gain = middle_rms(convert(48000, 22050, sine, 8192)) / middle_rms(sine);
			std::printf("  %7.0fHz  passband gain %.5f\n", pass_hz[i], gain);
			check(std::fabs(gain - 1.0) <= 0.01, "the passband is not flat");
		}

		// An alias of anything above the stopband edge has to land far enough
		// below the signal to be invisible to a log-compressed flux.
		const double stop_hz[] = { 15000.0, 18000.0, 21000.0 };
		for (std::size_t i = 0; i < sizeof(stop_hz) / sizeof(stop_hz[0]); i++)
		{
			std::vector<float> sine;
			synth_sine(sine, 48000, 2.0, stop_hz[i]);
			const double gain = middle_rms(convert(48000, 22050, sine, 8192)) / middle_rms(sine);
			const double db = 20.0 * std::log10(gain > 1e-12 ? gain : 1e-12);
			std::printf("  %7.0fHz  alias %7.1f dB\n", stop_hz[i], db);
			check(db <= -70.0, "an alias was not rejected");
		}

		// What the whole exercise is for: one recording, six rates, one answer.
		// 48kHz used to be read through a 42.7ms window where the geometry asks
		// for 46.4ms, which moved the classifier and the metre with it.
		const unsigned rates[] = { 22050, 32000, 44100, 48000, 88200, 96000 };
		bpmcore::analysis reference;
		for (std::size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++)
		{
			std::vector<float> audio;
			synth_beats(audio, rates[i], 40.0, 124.0, 4);
			bpmcore::options opt; opt.threads = 1;
			const bpmcore::analysis a =
				bpmcore::analyse(audio.data(), audio.size(), rates[i], nullptr, &opt);
			std::printf("  %6uHz  %7.3f BPM  %-8s p=%.3f  beat %7.3f  metre %d\n",
			            rates[i], a.bpm, bpmcore::rhythm_name(a.rhythm), a.confidence,
			            a.beat_bpm, a.meter);
			check(a.ok, "a rate failed to analyse");
			if (!a.ok) continue;
			if (i == 0) { reference = a; continue; }
			// Tighter than the 2 BPM the estimate is measured against, and
			// tighter than a tap can resolve. The point is that the input rate
			// does not enter the answer at all.
			check(std::fabs(a.bpm - reference.bpm) <= 0.05, "BPM depends on the input rate");
			check(a.rhythm == reference.rhythm, "rhythm depends on the input rate");
			check(a.meter == reference.meter, "metre depends on the input rate");
		}

		std::printf("resample: %d checks, %d failures\n", checks, failures);
		return failures == 0 ? 0 : 1;
	}

	int run_model(const char * path)
	{
		std::ifstream in(path);
		if (!in)
		{
			std::fprintf(stderr, "cannot open %s\n", path);
			return 2;
		}

		int checked = 0, class_mismatch = 0;
		double worst_prob = 0;
		std::string line;
		while (std::getline(in, line))
		{
			if (line.empty()) continue;
			// path \t true class \t expected class \t expected probability \t features
			std::vector<std::string> fields;
			std::string field;
			std::istringstream ls(line);
			while (std::getline(ls, field, '\t')) fields.push_back(field);
			if (fields.size() < 5) continue;

			const std::string expect_class = fields[2];
			const double expect_prob = std::atof(fields[3].c_str());

			std::vector<double> features;
			std::istringstream fs(fields[4]);
			double v;
			while (fs >> v) features.push_back(v);
			if (static_cast<int>(features.size()) != bpmcore::feature_count)
			{
				std::fprintf(stderr, "case has %d features, expected %d\n",
				             static_cast<int>(features.size()),
				             (int)bpmcore::feature_count);
				return 5;
			}

			double confidence = 0;
			const int cls = bpmcore::classify(features, &confidence);
			// The trainer writes class names in lower case.
			const std::string got = bpmcore::rhythm_name(cls);
			if (lower(got) != lower(expect_class))
			{
				class_mismatch++;
				std::fprintf(stderr, "class mismatch: got %s expected %s\n",
				             got.c_str(), expect_class.c_str());
			}
			worst_prob = std::max(worst_prob, std::fabs(confidence - expect_prob));
			checked++;
		}

		std::printf("model: %d cases, %d class mismatches, worst probability delta %.3e\n",
		            checked, class_mismatch, worst_prob);
		if (checked == 0) return 6;
		// The trees are exported verbatim, so anything above float noise is a bug.
		return (class_mismatch == 0 && worst_prob < 1e-5) ? 0 : 1;
	}
}

int main(int argc, char ** argv)
{
	const std::string mode = argc >= 2 ? argv[1] : "";
	if (mode == "model" && argc >= 3) return run_model(argv[2]);
	if (mode == "resample") return run_resample();
	if (mode == "pipeline" && argc >= 4)
		return run_pipeline(argv[2], static_cast<unsigned>(std::atoi(argv[3])),
		                    argc >= 5 ? std::atoi(argv[4]) : 1);
	if (mode == "profile" && argc >= 4)
		return run_profile(argv[2], static_cast<unsigned>(std::atoi(argv[3])),
		                   argc >= 5 ? std::max(1, std::atoi(argv[4])) : 3);
	if (mode == "bench" && argc >= 4)
		return run_bench(argv[2], static_cast<unsigned>(std::atoi(argv[3])),
		                 argc >= 5 ? std::max(1, std::atoi(argv[4])) : 3,
		                 argc >= 6 ? std::atoi(argv[5]) : 0);

	std::fprintf(stderr,
		"usage: bpmcore_test model <cases file>\n"
		"       bpmcore_test resample\n"
		"       bpmcore_test pipeline <raw f32 mono file> <sample rate>\n"
		"       bpmcore_test bench <raw f32 mono file> <sample rate> [repeats]\n");
	return 64;
}
