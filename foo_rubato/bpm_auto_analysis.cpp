#include "stdafx.h"
#include "bpm_auto_analysis.h"
#include "preferences.h"
#include "bpm_math.h"

using namespace bpm_math;

/***** BPM Analysis *****/

bpm_auto_analysis::bpm_auto_analysis(metadb_handle_ptr p_track):
	m_track(p_track)
{
	seconds_to_read = bpm_config_seconds_to_read;
	num_bpms_to_calc = bpm_config_num_bpms_to_calc;
	offset_pct_min = (double)bpm_config_offset_pct_min / 100.0;
	offset_pct_max = (double)bpm_config_offset_pct_max / 100.0;
	// Do some checks to make sure the input settings are OK
	if (offset_pct_min > offset_pct_max)
	{
		std::swap(offset_pct_min, offset_pct_max);
	}
	else if (offset_pct_min == offset_pct_max)
	{
		offset_pct_min--;
	}
	offset_pct_inc = (offset_pct_max - offset_pct_min) / (double)num_bpms_to_calc;

	sample_rate = 0;
	channel_count = 0;
	max_samples = 0;
	sample_count = 0;
	sample_downmix = 0;

	// Starting value and order of enum make this possible. Saves us from a big switch statement.
	fft_window_size = 64 * (int)pow(2, (double)bpm_config_fft_window_size);
	fft_window_slide = 64 * (int)pow(2, (double)bpm_config_fft_window_slide);
	fft_bin_size = fft_window_size/2 + 1;
	num_fft_windows = 0;
	sample_window_offset = 0;
	seconds_in_fft = 0;
	sample_count = 0;
	sample_downmix = 0;
	window_type = bpm_config_fft_window_type;

	do_peak_picking = false;
	flux_constant = 2;
	flux_rms = 0;
	median_window_size = fft_window_size/25;
	actual_median_window_size = median_window_size;

	bpm_min = bpm_config_bpm_min;
	bpm_max = bpm_config_bpm_max;
	// Do some checks to make sure the input settings are OK
	if (bpm_min > bpm_max)
	{
		std::swap(bpm_min, bpm_max);
	}
	else if (bpm_min == bpm_max)
	{
		bpm_max++;
	}
	bpm_precision = bpm_config_bpm_precision;
	tempo_comparisons = 0;
	tempo_offset_size = 0;
	tempo_offset_multiple = 1;
	offset_floor = 0;
	offset_ceiling = 0;
	offset_actual = 0;
	interpolate_flux = bpm_config_interpolate_flux;
	interpolated_peak = 0;
	sum = 0;
	max_sum = 0;
	estimated_bpm = 0;
	candidate_selection = bpm_config_candidate_selection;
}

bpm_auto_analysis::~bpm_auto_analysis()
{

}

double bpm_auto_analysis::run_safe(threaded_process_status & thread_status, abort_callback & p_abort)
{
	try
	{
		return run(thread_status, p_abort);
	}
	catch (const std::exception &exc)
	{
		FB2K_console_formatter() << "foo_rubato: Error analysing " << m_track->get_path() << ": " << exc;
		return 0.0;
	}
}

double bpm_auto_analysis::run(threaded_process_status & thread_status, abort_callback & p_abort)
{
	double bpm_result = 0;
	int progress = 0;
	int max_progress = (int)((offset_pct_max - offset_pct_min) / offset_pct_inc) * 6; // Multiply by 6 due to the 6 steps in the loop below

	thread_status.set_progress_secondary(0, max_progress);

	if (m_track->get_length() < seconds_to_read)
	{
		thread_status.set_progress_secondary(max_progress, max_progress);
		FB2K_console_formatter() << "foo_rubato: Error analysing " << m_track->get_path() << ". Track length too short. BPM set to zero.";
		return 0;
	}

	for (double offset_pct = offset_pct_min; offset_pct < offset_pct_max; offset_pct+=offset_pct_inc)
	{
		// If we got an error back, skip this file's analysis. It'll be missing important values such as sample rate which are needed. BPM is set to zero.
		if (!read_file(offset_pct, p_abort))
		{
			bpm_list.push_back(0);
			break;
		}
		thread_status.set_progress_secondary(++progress, max_progress);
		if (p_abort.is_aborting()) break;
		calc_stft();
		thread_status.set_progress_secondary(++progress, max_progress);
		if (p_abort.is_aborting()) break;
		calc_spectral_flux();
		thread_status.set_progress_secondary(++progress, max_progress);
		if (p_abort.is_aborting()) break;
		pick_peaks();
		thread_status.set_progress_secondary(++progress, max_progress);
		if (p_abort.is_aborting()) break;
		calc_bpm();
		thread_status.set_progress_secondary(++progress, max_progress);
		if (p_abort.is_aborting()) break;
		clean_up();
		thread_status.set_progress_secondary(++progress, max_progress);
		if (p_abort.is_aborting()) break;
	}

	std::sort(bpm_list.begin(), bpm_list.end());

	// Aborting before the first pass completes leaves nothing to choose from.
	if (bpm_list.empty())
	{
		return 0;
	}

	switch (candidate_selection)
	{
		case BPM_MEAN:
			bpm_result = mean(bpm_list);
			break;
		case BPM_MODE:
			bpm_result = mode(bpm_list, 0.8);
			break;
		case BPM_MEDIAN:
		default:
			bpm_result = bpm_list[bpm_list.size()/2];
			break;
	}

	if (bpm_config_output_debug)
	{
		FB2K_console_formatter() << "\n";
		FB2K_console_formatter() << "foo_rubato: Estimated BPMs (sorted) for " << pfc::string_filename_ext(m_track->get_path()) << " are:";
		for (unsigned i = 0; i < bpm_list.size(); i++)
			FB2K_console_formatter() << "BPM " << i+1 << " = " << bpm_list[i];

		FB2K_console_formatter() << "foo_rubato: Calculated BPM = " << bpm_result;
		FB2K_console_formatter() << "\n";
	}

	return bpm_result;
}

bool bpm_auto_analysis::read_file(double offset_pct, abort_callback &p_abort)
{
	if (!input_file.is_open())
	{
		// Open the file if it isn't already open
		input_file.open(m_file, m_track, 0, p_abort, false, false);
	}

	if (!input_file.is_open())
	{
		FB2K_console_formatter() << "foo_rubato: Error analysing " << m_track->get_path() << ". File could not be opened for analysis";
		return false;
	}

	if (input_file.can_seek())
	{
		input_file.seek(m_track->get_length()*offset_pct, p_abort);
	}
	else
	{
		FB2K_console_formatter() << "foo_rubato: Warning - Failed to seek file " << m_track->get_path() << ". BPM result will be of first " << seconds_to_read << " seconds only.";
	}

	// Grab and ignore the first chunk as it will most likely have an odd sample chunk size
	// TODO: Handle case where we reach EOF (run() returns false)
	if (!input_file.run(chunk, p_abort))
	{
		FB2K_console_formatter() << "foo_rubato: Error analysing " << m_track->get_path() << ". Unexpected end of file found.";
		return false;
	}

	sample_rate = chunk.get_srate();
	channel_count = chunk.get_channels();

	max_samples = sample_rate*seconds_to_read;

	if (bpm_config_output_debug)
	{
		FB2K_console_formatter() << "\n\n\nFile path: " << input_file.get_path();
		FB2K_console_formatter() << "Sample rate: " << sample_rate << "Hz";
		FB2K_console_formatter() << "Num channels: " << channel_count;
	}

	// Reset the audio buffer before filling it
	audio_buffer.resize(0);

	while (audio_buffer.size() < max_samples)
	{
		// Get a chunk of audio from the file so we can extract sample/channel properties
		input_file.run(chunk, p_abort);

		// Sample is a unit of interleaved PCM data (ie 1 sample corresponds to 2 values for stereo PCM data, 6 for 5.1 etc)
		sample_count = chunk.get_sample_count();
		data_ptr = chunk.get_data();


		// Increase buffer size by sample_count, but only if necessary
		if (audio_buffer.capacity() <= audio_buffer.size() + sample_count)
		{
			audio_buffer.reserve(audio_buffer.size() + sample_count);
		}

		// For each sample in the audio chunk
		for (t_size sample_num = 0; sample_num < sample_count; sample_num++)
		{
			sample_downmix = 0;

			// For each of the interleaved channels
			for (unsigned channel_num = 0; channel_num < channel_count; channel_num++)
			{
				sample_downmix += data_ptr[0];
				data_ptr++;
			}

			// Take average of summed channel data to get single sample
			sample_downmix = sample_downmix / channel_count;

			audio_buffer.push_back(sample_downmix);
		}
	}

	if (bpm_config_output_debug)
	{
		FB2K_console_formatter() << "Buffer size is " << audio_buffer.size();
		FB2K_console_formatter() << "Allocated size is " << audio_buffer.capacity();
	}

	return true;
}

void bpm_auto_analysis::calc_stft()
{
	if (bpm_config_fft_window_slide == FFT_SLIDE_AUTO)
	{
		// Human hearing can only determine difference of 10ms ([1])
		fft_window_slide = sample_rate/100;
	}

	// A sample rate under 100Hz makes the automatic slide zero, and the
	// subtraction below underflows if the decoder handed us less audio than one
	// window. Either one used to crash the analysis outright (#24710, #24506).
	if (fft_window_slide < 1)
	{
		fft_window_slide = 1;
	}
	// Work out the number of sliding windows available. Every stage after this
	// one, and clean_up(), is written around num_fft_windows, so a shortfall is
	// recorded as zero windows rather than returning early - that keeps every
	// allocation matched with its delete.
	if (audio_buffer.size() < static_cast<size_t>(fft_window_size))
	{
		num_fft_windows = 0;
		FB2K_console_formatter() << "foo_rubato: Error analysing " << m_track->get_path()
		                         << ". Not enough audio decoded for one FFT window. BPM set to zero.";
	}
	else
	{
		num_fft_windows = (int)((audio_buffer.size() - fft_window_size) / fft_window_slide) + 1;
	}
	// Work out number of seconds we're actually processing
	seconds_in_fft = (double)(fft_window_slide * (num_fft_windows - 1) + fft_window_size) / (double)sample_rate;
	// stft is a 2D array of num_fft_windows by fft_bin_size
	stft = new double*[num_fft_windows];
	// Output array will be out[r0,r1,r2,...,rn/2, i(n+1)/2-1,...,i2,i1]
	m_fft = bpm_fft::g_create();
	m_fft->create_plan(fft_window_size);
	double * fft_in = m_fft->get_input_buffer();
	double * fft_out = m_fft->get_output_buffer();

	// Window will slide along buffer
	for (int fft_window = 0; fft_window < num_fft_windows; fft_window++)
	{
		// Current offset into audio buffer
		sample_window_offset = fft_window*fft_window_slide;

		// Copy from the audio buffer an FFT window sized array for FFT processing
		for (int sample_num = 0; sample_num < fft_window_size; sample_num++)
		{
			switch (window_type)
			{
				case FFT_WINDOW_HAMMING:
					fft_in[sample_num] = (double)audio_buffer[sample_window_offset + sample_num] * window_hamming(sample_num, fft_window_size);
					break;
				case FFT_WINDOW_HANNING:
					fft_in[sample_num] = (double)audio_buffer[sample_window_offset + sample_num] * window_hanning(sample_num, fft_window_size);
					break;
				case FFT_WINDOW_GAUSIAN:
					fft_in[sample_num] = (double)audio_buffer[sample_window_offset + sample_num] * window_gauss(sample_num, fft_window_size);
					break;
				case FFT_WINDOW_RECT:
				default:
					break;
			}
		}

		// Do the FFT
		m_fft->execute_plan();

		stft[fft_window] = new double[fft_bin_size];
		// Copy the FFT result into the 2D STFT array
		for (int fft_bin = 0; fft_bin < fft_bin_size; fft_bin++)
		{
			stft[fft_window][fft_bin] = abs(fft_out[fft_bin]);
			// TODO: Make the fft result processing configurable
//			stft[fft_window][fft_bin] = fft_out[fft_bin];
//			stft[fft_window][fft_bin] = pow(fft_out[fft_bin],2);
//			stft[fft_window][fft_bin] = pow(fft_out[fft_bin],2)*fft_bin;
		}
	}

}

void bpm_auto_analysis::calc_spectral_flux()
{
	spectral_flux = new double[num_fft_windows];
	std::fill_n(spectral_flux, num_fft_windows, 0.0);

	for (int fft_window = 0; fft_window < num_fft_windows; fft_window++)
	{
		for (int fft_bin = 0; fft_bin < fft_bin_size; fft_bin++)
		{
			if (fft_window > 0)
			{
				if (stft[fft_window][fft_bin] > stft[fft_window-1][fft_bin])
				{
					// TODO: Make the spectral weight configurable
					// No weighting
//					spectral_flux[fft_window-1] += stft[fft_window][fft_bin];
					// Difference
//					spectral_flux[fft_window-1] += stft[fft_window][fft_bin] - stft[fft_window-1][fft_bin];
//					spectral_flux[fft_window-1] += pow(stft[fft_window][fft_bin] - stft[fft_window-1][fft_bin], 2);
					// Multiply by fft_bin to apply weight to higher frequencies ([2])
					// I've found this weighting to give the best results, but others may perform better for different genres
					spectral_flux[fft_window-1] += pow(stft[fft_window][fft_bin] - stft[fft_window-1][fft_bin], 2) * fft_bin;
					// Weight formula variation ([2])
//					spectral_flux[fft_window-1] += (stft[fft_window][fft_bin] - stft[fft_window-1][fft_bin]) * fft_bin * fft_bin;
					// Approximate pshychoacoustic weighting ([2])
//					spectral_flux[fft_window-1] += log(stft[fft_window][fft_bin] / stft[fft_window-1][fft_bin]);
				}
			}
			else
			{
				// Assume difference of fft_bins at fft_windows -1 and 0 is 0 (no change in energy)
				spectral_flux[fft_window] = 0;
			}
		}

		// Only necessary if stft isn't absolute values (see post FFT processing in calc_stft())
		//spectral_flux[fft_window-1] = (spectral_flux[fft_window-1] + abs(spectral_flux[fft_window-1]))/2;
	}

}

void bpm_auto_analysis::pick_peaks()
{
	median_window = new double[median_window_size];
	peaks = new double[num_fft_windows];
	std::fill_n(peaks, num_fft_windows, 0.0);

	if (do_peak_picking)
	{
		// Use a sliding window across the flux output to obtain a subset of values
		// Sort the subset, then grab the median value. If the current flux value exceeds
		// the the median multiplied by some scaling factor, summed with the rms of the
		// flux, consider it a peak (or beat). Otherwise it will be zero.
		for (int fft_window = 0; fft_window < num_fft_windows; fft_window++)
		{
			// Case where median window would exceed lower range of flux array
			if (fft_window < median_window_size/2)
			{
				actual_median_window_size = fft_window + median_window_size/2;

				for (int i = 0; i < actual_median_window_size; i++)
				{
					median_window[i] = spectral_flux[i];
				}
			}
			// Case where median window would exceed upper range of flux array
			else if (fft_window > num_fft_windows - median_window_size/2)
			{
				actual_median_window_size = num_fft_windows - fft_window + median_window_size/2;

				for (int i = 0; i < actual_median_window_size; i++)
				{
					median_window[i] = spectral_flux[fft_window-median_window_size/2+i];
				}
			}
			// Median window won't exceed ends of array
			else
			{
				actual_median_window_size = median_window_size;

				for (int i = 0; i < actual_median_window_size; i++)
				{
					median_window[i] = spectral_flux[fft_window-(median_window_size/2)+i];
				}
			}
			
			std::sort(median_window, median_window + actual_median_window_size);

			if (spectral_flux[fft_window] > (median_window[actual_median_window_size/2] * flux_constant + flux_rms))
				peaks[fft_window] = spectral_flux[fft_window];
		}

	}
}

void bpm_auto_analysis::calc_bpm()
{
	double tempo_inc = 1;
	max_sum = 0;

	// With no windows there is nothing to correlate, and seconds_in_fft is zero,
	// which would turn the tempo offset below into a NaN.
	if (num_fft_windows < 1 || seconds_in_fft <= 0)
	{
		bpm_list.push_back(0);
		return;
	}

	switch (bpm_precision)
	{
		case BPM_PRECISION_1:
			tempo_inc = 1; break;
		case BPM_PRECISION_1DP:
			tempo_inc = 0.1; break;
		case BPM_PRECISION_2DP:
			tempo_inc = 0.01; break;
		default:
			tempo_inc = 1; break;
	}

	for (double tempo = bpm_min; tempo <= bpm_max; tempo+=tempo_inc)
	{
		tempo_offset_size = (double)num_fft_windows * (60/tempo) / seconds_in_fft;
		tempo_comparisons = (int)tempo_offset_size;

		for (int tempo_offset = 0; tempo_offset < tempo_comparisons; tempo_offset++)
		{
			sum = 0;
			tempo_offset_multiple = 1;
			while (tempo_offset_size * tempo_offset_multiple + tempo_offset < num_fft_windows)
			{
				offset_actual = tempo_offset_size * tempo_offset_multiple + tempo_offset;
				offset_floor = (int)floor(offset_actual);
				offset_ceiling = (int)ceil(offset_actual);

				if (do_peak_picking)
				{
					if (interpolate_flux && (offset_ceiling > offset_floor))
					{
						interpolated_peak = peaks[offset_floor]*(offset_ceiling - offset_actual) + peaks[offset_ceiling]*(offset_actual - offset_floor);
					}
					else
					{
						interpolated_peak = peaks[offset_floor];
					}
				}
				else
				{
					if (interpolate_flux && (offset_ceiling > offset_floor))
					{
						interpolated_peak = spectral_flux[offset_floor]*(offset_ceiling - offset_actual) + spectral_flux[offset_ceiling]*(offset_actual - offset_floor);
					}
					else
					{
						interpolated_peak = spectral_flux[offset_floor];
					}
				}

				sum += interpolated_peak + 100000;

				tempo_offset_multiple++;
			}

			sum = sum / (tempo_offset_multiple - 1);

			if (sum > max_sum)
			{
				max_sum = sum;
				estimated_bpm = tempo;
			}
			// Check to see if the tempo may be double the current tempo by checking if the sum is greater than 90% of the current max sum
			// TODO: Be a little more scientific about the 90% value. ie some scaled percentage based on tempo
			else if (tempo > estimated_bpm*2 - 2 &&
					 tempo < estimated_bpm*2 + 2 &&
					 sum > max_sum*0.9)
			{
				max_sum = sum;
				estimated_bpm = tempo;
			}

		}

	}

	bpm_list.push_back(estimated_bpm);

	if (bpm_config_output_debug)
	{
		FB2K_console_formatter() << "Num FFT windows: " << num_fft_windows;
		FB2K_console_formatter() << "Seconds in FFT: " << seconds_in_fft;
	}

}

void bpm_auto_analysis::clean_up()
{
	input_file.close();

	m_fft.release();

	for (int i = 0; i < num_fft_windows; i++)
	{
		delete[] stft[i];
	}
	delete[] stft;
	delete[] spectral_flux;
	delete[] median_window;
	delete[] peaks;
}
