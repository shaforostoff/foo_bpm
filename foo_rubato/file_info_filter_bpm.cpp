#include "stdafx.h"

#include "file_info_filter_bpm.h"

#include "format_bpm.h"
#include "globals.h"
#include "version.h"

file_info_filter_bpm::file_info_filter_bpm(const metadb_handle_list & p_tracks, const char * p_bpm_tag,
                                           const std::vector<double> & p_bpm_results,
                                           const char * p_rhythm_tag,
                                           const std::vector<pfc::string8> & p_rhythms,
                                           const std::vector<bool> & p_adjusted)
	: m_bpm_tag(p_bpm_tag), m_rhythm_tag(p_rhythm_tag != nullptr ? p_rhythm_tag : "")
	, m_from_analysis(true)
{
	const bool have_rhythms = p_rhythms.size() == p_bpm_results.size() && !m_rhythm_tag.is_empty();
	const bool have_adjusted = p_adjusted.size() == p_bpm_results.size();
	pfc::dynamic_assert(p_tracks.get_count() == p_bpm_results.size());
	pfc::array_t<t_size> order;
	order.set_size(p_tracks.get_count());
	order_helper::g_fill(order.get_ptr(), order.get_size());
	p_tracks.sort_get_permutation_t(pfc::compare_t<metadb_handle_ptr, metadb_handle_ptr>, order.get_ptr());
	m_tracks.set_count(order.get_size());
	m_bpm_results.resize(order.get_size());
	if (have_rhythms) m_rhythms.resize(order.get_size());
	if (have_adjusted) m_adjusted.resize(order.get_size());

	// The tracks are sorted so apply_filter can bsearch them; every parallel
	// array has to follow the same permutation.
	for(t_size n = 0; n < order.get_size(); n++)
	{
		m_tracks[n] = p_tracks[order[n]];
		m_bpm_results[n] = p_bpm_results[order[n]];
		if (have_rhythms) m_rhythms[n] = p_rhythms[order[n]];
		if (have_adjusted) m_adjusted[n] = p_adjusted[order[n]];
	}
}

file_info_filter_bpm::file_info_filter_bpm(metadb_handle_ptr p_track, const char * p_bpm_tag, double p_bpm_result)
	: m_bpm_tag(p_bpm_tag)
	, m_from_analysis(false)
{
	m_tracks.add_item(p_track);
	m_bpm_results.push_back(p_bpm_result);
}

bool file_info_filter_bpm::apply_filter(metadb_handle_ptr p_track, t_filestats p_stats, file_info & p_info)
{
	t_size index;
	if (m_tracks.bsearch_t(pfc::compare_t<metadb_handle_ptr, metadb_handle_ptr>, p_track, index))
	{
		format_bpm bpm_value(m_bpm_results[index]);
		p_info.meta_set(m_bpm_tag, bpm_value);

		// Record what produced the number, or clear an attribution that is no
		// longer true. Only a BPM the analysis stands behind is stamped: for one
		// tapped by hand, or doubled or halved by the user, an attribution left
		// over from an earlier scan would be claiming credit for a number the
		// analysis did not produce.
		const bool adjusted = index < m_adjusted.size() && m_adjusted[index];
		if (m_from_analysis && !adjusted)
		{
			p_info.meta_set(BPM_ALGORITHM_TAG, FOO_RUBATO_ALGORITHM);
		}
		else
		{
			p_info.meta_remove_field(BPM_ALGORITHM_TAG);
		}

		if (index < m_rhythms.size() && !m_rhythm_tag.is_empty() && !m_rhythms[index].is_empty())
		{
			p_info.meta_set(m_rhythm_tag, m_rhythms[index]);
		}
		return true;
	}
	else
	{
		return false;
	}
}
