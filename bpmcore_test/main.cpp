// Verification and benchmark harness for bpmcore.
//
// It links the analysis and nothing else - no foobar2000, no pfc - which is
// both the point of the split and what lets this run in CI.
//
//   model <cases file>
//       feed stored feature vectors straight to the classifier and check the
//       exported trees reproduce the class and probability scikit-learn gave.
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
			bpmcore::tapped_bpm(acf, g.beat_lag, cls, o.frame_rate);
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
		"       bpmcore_test pipeline <raw f32 mono file> <sample rate>\n"
		"       bpmcore_test bench <raw f32 mono file> <sample rate> [repeats]\n");
	return 64;
}
