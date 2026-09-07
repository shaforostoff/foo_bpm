#pragma once

// Signal-processing maths used by the automatic analysis.
//
// This used to live in foo_bpm.h alongside `using namespace std` and a set of
// hardcoded d:\ debug paths. The formulas are unchanged; see the window
// function references in README.md.

#include <algorithm>
#include <cmath>
#include <vector>

namespace bpm_math
{
	constexpr double pi = 3.14159265358979323846;
	constexpr double e  = 2.71828182845904523536;

	inline double sinc(double x) { return std::sin(pi * x) / (pi * x); }

	// Window functions ([4])
	inline double window_hamming(double sample_num, double fft_window_size)
	{
		return 0.54 - 0.46 * std::cos((2 * pi * sample_num) / (fft_window_size - 1));
	}

	inline double window_hanning(double sample_num, double fft_window_size)
	{
		return 0.5 * (1 - std::cos((2 * pi * sample_num) / (fft_window_size - 1)));
	}

	inline double window_gauss(double sample_num, double fft_window_size)
	{
		return std::pow(e, -0.5 * std::pow(((sample_num - (fft_window_size - 1)) / 2) /
		                                   (0.4 * (fft_window_size - 1) / 2), 2));
	}

	inline double rms(const double * x, size_t size)
	{
		double total = 0;
		for (size_t i = 0; i < size; i++) total += x[i] * x[i];
		return size ? std::sqrt(total / static_cast<double>(size)) : 0.0;
	}

	inline double mean(const std::vector<double> & values)
	{
		if (values.empty()) return 0.0;
		double sum = 0;
		for (double v : values) sum += v;
		return sum / static_cast<double>(values.size());
	}

	//! Mean of the longest run of values that stay within `tolerance` of each
	//! other. Expects `values` sorted ascending.
	inline double mode(const std::vector<double> & values, double tolerance)
	{
		if (values.empty()) return 0.0;

		size_t current_length = 1, max_length = 1;
		double current_sum = values[0], max_sum = values[0];

		for (size_t i = 1; i < values.size(); i++)
		{
			if (values[i] < values[i - 1] + tolerance &&
			    values[i] > values[i - 1] - tolerance)
			{
				current_length++;
				current_sum += values[i];

				if (current_length > max_length)
				{
					max_length = current_length;
					max_sum = current_sum;
				}
			}
			else
			{
				current_length = 1;
				current_sum = values[i];
			}
		}

		return max_sum / static_cast<double>(max_length);
	}
}
