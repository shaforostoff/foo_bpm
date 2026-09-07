#ifndef __FILE_INFO_FILTER_BPM_H__
#define __FILE_INFO_FILTER_BPM_H__

#include <vector>

#include <SDK/foobar2000.h>

class file_info_filter_bpm : public file_info_filter
{
public:
	//! Results of the automatic analysis, so the BPM written carries an
	//! algorithm attribution. `p_rhythm_tag` may be null, and `p_rhythms`
	//! empty, when the rhythm is not to be written. `p_adjusted` marks tracks
	//! whose BPM the user doubled or halved in the results dialog before
	//! committing: the analysis no longer stands behind those, so they are
	//! treated like a hand-tapped value and lose the attribution.
	//! `p_initial_bpms` is the tempo each track opens at, 0 where there was
	//! none to measure.
	file_info_filter_bpm(const metadb_handle_list & p_tracks, const char * p_bpm_tag,
	                     const std::vector<double> & p_bpm_results,
	                     const char * p_rhythm_tag = nullptr,
	                     const std::vector<pfc::string8> & p_rhythms = std::vector<pfc::string8>(),
	                     const std::vector<bool> & p_adjusted = std::vector<bool>(),
	                     const std::vector<double> & p_initial_bpms = std::vector<double>());
	//! One BPM the user tapped by hand. No analysis produced it, so any
	//! attribution already on the file is now false and is removed rather
	//! than written - see BPM_ALGORITHM_TAG.
	file_info_filter_bpm(metadb_handle_ptr p_track, const char * p_bpm_tag, double p_bpm_result);
	bool apply_filter(metadb_handle_ptr p_track, t_filestats p_stats, file_info & p_info);

private:
	metadb_handle_list m_tracks;
	std::vector<double> m_bpm_results;
	std::vector<double> m_initial_bpms;
	std::vector<pfc::string8> m_rhythms;
	std::vector<bool> m_adjusted;
	pfc::string8 m_bpm_tag;
	pfc::string8 m_rhythm_tag;
	//! True for analysis results, false for a hand-tapped BPM.
	bool m_from_analysis;
};

#endif // __FILE_INFO_FILTER_BPM_H__
