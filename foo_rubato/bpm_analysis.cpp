#include "stdafx.h"
#include "bpm_analysis.h"
#include "bpm_auto_analysis.h"
#include "preferences.h"

#include <chrono>

/***** Analysis entry point *****/
//
// The tango engine runs one pass over the whole track: onset envelope, windowed
// autocorrelation, metrical grid, rhythm class, then the metrical level a
// dancer would tap. The legacy engine is the original 2009 algorithm, kept
// behind an advanced-preferences switch because the preferences page's STFT and
// candidate-selection settings only mean anything there.

namespace
{
	//! Bridges foobar2000's abort and progress reporting into the core.
	class fb2k_listener : public bpmcore::listener
	{
	public:
		fb2k_listener(threaded_process_status & status, abort_callback & abort)
			: m_status(status), m_abort(abort) {}

		bool cancelled() override { return m_abort.is_aborting(); }
		void progress(double fraction) override
		{
			// The envelope is most of the run time, so its progress is scaled
			// into the first three quarters of the secondary bar.
			m_status.set_progress_secondary(static_cast<t_size>(fraction * 750.0) + 250, 1000);
		}

	private:
		threaded_process_status & m_status;
		abort_callback & m_abort;
	};

	bpmcore::analysis run_tango_engine(const metadb_handle_ptr & track,
	                                   threaded_process_status & status,
	                                   abort_callback & abort)
	{
		bpmcore::analysis result;

		const auto started = std::chrono::steady_clock::now();

		input_helper input;
		service_ptr_t<file> nothing;
		// The whole side is read once, front to back, and never seeked. Saying
		// so lets a decoder skip building a seektable it will not be asked for,
		// and stops a format that carries looping metadata from being decoded
		// round and round until the length cap stops it.
		input.open(nothing, track, input_flag_simpledecode, abort, false, false);
		if (!input.is_open())
		{
			FB2K_console_formatter() << "foo_rubato: could not open " << track->get_path() << " for analysis.";
			return result;
		}

		status.set_progress_secondary(0, 1000);

		// The envelope has to be normalised by the track's overall level before
		// it is compressed, and that is not known until the whole side has been
		// decoded, so the audio is collected rather than streamed through.
		std::unique_ptr<bpmcore::collector> collector;
		unsigned sample_rate = 0;
		audio_chunk_impl chunk;
		while (input.run(chunk, abort))
		{
			abort.check();

			const unsigned channels = chunk.get_channels();
			const unsigned srate = chunk.get_srate();
			if (channels == 0 || srate == 0) continue;

			if (collector == nullptr)
			{
				sample_rate = srate;
				collector.reset(new bpmcore::collector(srate));
			}
			else if (srate != sample_rate)
			{
				// A file whose rate changes mid-stream would put the frame grid
				// on two different time bases; the tempo would be meaningless.
				FB2K_console_formatter() << "foo_rubato: sample rate changes within "
				                         << track->get_path() << "; analysis stopped at the change.";
				break;
			}

			collector->add_interleaved(chunk.get_data(), chunk.get_sample_count(), channels);
			if (collector->full()) break;
		}

		if (collector == nullptr || collector->size() == 0)
		{
			FB2K_console_formatter() << "foo_rubato: no audio decoded from " << track->get_path() << ".";
			return result;
		}

		status.set_progress_secondary(250, 1000);
		const auto decoded = std::chrono::steady_clock::now();

		fb2k_listener listener(status, abort);
		result = collector->finish(&listener);
		abort.check();
		const auto analysed = std::chrono::steady_clock::now();

		// Two numbers rather than one, because they have nothing to do with each
		// other and only one of them is this component's to fix. Reading a track
		// is the decoder's cost and dwarfs the rest on a slow codec or a network
		// share; the analysis runs at hundreds of times realtime.
		const double read_seconds =
			std::chrono::duration<double>(decoded - started).count();
		const double analysis_seconds =
			std::chrono::duration<double>(analysed - decoded).count();

		if (!result.ok)
		{
			FB2K_console_formatter() << "foo_rubato: could not measure a tempo in " << track->get_path()
			                         << " (" << pfc::format_float(result.duration, 0, 1) << "s decoded).";
			return result;
		}

		if (bpm_config_output_debug)
		{
			FB2K_console_formatter() << "foo_rubato: " << pfc::string_filename_ext(track->get_path())
				<< " -> " << pfc::format_float(result.bpm, 0, 2) << " BPM, "
				<< bpmcore::rhythm_name(result.rhythm)
				<< " (p=" << pfc::format_float(result.confidence, 0, 2) << "), beat "
				<< pfc::format_float(result.beat_bpm, 0, 2) << " BPM, " << result.meter << "/4 grid; "
				<< pfc::format_float(result.duration, 0, 1) << "s of audio, "
				<< pfc::format_float(read_seconds, 0, 2) << "s to read, "
				<< pfc::format_float(analysis_seconds, 0, 2) << "s to analyse";
		}

		return result;
	}
}

bpmcore::analysis bpm_analyse(const metadb_handle_ptr & track,
                              threaded_process_status & status,
                              abort_callback & abort)
{
	if (!bpm_config_use_legacy_engine.get())
	{
		return run_tango_engine(track, status, abort);
	}

	bpmcore::analysis result;
	bpm_auto_analysis legacy(track);
	result.bpm = legacy.run_safe(status, abort);
	result.ok = result.bpm > 0;
	return result;
}
