#ifndef __BPM_AUTO_ANALYSIS_THREAD_H__
#define __BPM_AUTO_ANALYSIS_THREAD_H__

#include <vector>

#include <SDK/foobar2000.h>

class bpm_auto_analysis_thread : public threaded_process_callback
{
	public:
		bpm_auto_analysis_thread(metadb_handle_list_cref p_tracks);
		void start();

	private:
		void run(threaded_process_status & p_status, abort_callback & p_abort) override;
		void on_done(ctx_t p_wnd, bool p_was_aborted) override;

		pfc::list_t<metadb_handle_ptr> m_tracks;
		pfc::list_t<file_info_impl> m_infos;
		std::vector<double> m_bpm_results;
		std::vector<pfc::string8> m_rhythms;
		//! How much the tempo moves over each track, in BPM; 0 where the track
		//! was too short to measure it. Kept as a number rather than formatted
		//! here so that doubling or halving a result scales it too.
		std::vector<double> m_spreads;
		//! The tempo each track opens at, in BPM; 0 where its opening had no
		//! beat to measure.
		std::vector<double> m_initial_bpms;
};

#endif // __BPM_AUTO_ANALYSIS_THREAD_H__
