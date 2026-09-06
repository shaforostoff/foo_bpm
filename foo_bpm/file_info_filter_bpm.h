#ifndef __FILE_INFO_FILTER_BPM_H__
#define __FILE_INFO_FILTER_BPM_H__

#include <vector>

#include <SDK/foobar2000.h>

class file_info_filter_bpm : public file_info_filter
{
public:
	//! `p_rhythm_tag` may be null, and `p_rhythms` empty, when only the BPM is
	//! to be written - the manual tapping dialog has no rhythm to report.
	file_info_filter_bpm(const metadb_handle_list & p_tracks, const char * p_bpm_tag,
	                     const std::vector<double> & p_bpm_results,
	                     const char * p_rhythm_tag = nullptr,
	                     const std::vector<pfc::string8> & p_rhythms = std::vector<pfc::string8>());
	file_info_filter_bpm(metadb_handle_ptr p_track, const char * p_bpm_tag, double p_bpm_result);
	bool apply_filter(metadb_handle_ptr p_track, t_filestats p_stats, file_info & p_info);

private:
	metadb_handle_list m_tracks;
	std::vector<double> m_bpm_results;
	std::vector<pfc::string8> m_rhythms;
	pfc::string8 m_bpm_tag;
	pfc::string8 m_rhythm_tag;
};

#endif // __FILE_INFO_FILTER_BPM_H__
