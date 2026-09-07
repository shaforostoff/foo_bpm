#include "stdafx.h"

#include <string>

#include "bpm_result_dialog.h"
#include "preferences.h"
#include "format_bpm.h"
#include "file_info_filter_bpm.h"

using std::string;

bpm_result_dialog::bpm_result_dialog(metadb_handle_list_cref p_tracks, const pfc::list_t<file_info_impl> &p_infos,
                                     const std::vector<double> &p_bpm_results, const std::vector<pfc::string8> &p_rhythms,
                                     const std::vector<double> &p_spreads,
                                     const std::vector<double> &p_initial_bpms):
	m_tracks(p_tracks),
	m_infos(p_infos),
	m_bpm_results(p_bpm_results),
	m_rhythms(p_rhythms),
	m_spreads(p_spreads),
	m_initial_bpms(p_initial_bpms),
	m_adjusted(p_bpm_results.size(), false)
{
	// Read before anything here can write to m_infos. Kept as the string the
	// file carried rather than a parsed number: it is being shown for
	// comparison, and rounding someone's tap on the way to the screen would
	// defeat the point.
	m_tag_bpms.resize(p_infos.get_size());
	for (t_size i = 0; i < p_infos.get_size(); i++)
	{
		const char * value = p_infos[i].meta_get(bpm_config_bpm_tag, 0);
		if (value != NULL) m_tag_bpms[i] = value;
	}
}

namespace
{
	//! The fluctuation column: how far the tempo moves over the track, as a
	//! plus-or-minus in BPM. Blank where bpmcore could not measure one, which
	//! it reports as zero - a track under about half a minute has too few
	//! windows to draw a spread from.
	//!
	//! One decimal: the figure is a description of a performance, not a
	//! measurement to carry around, and the second decimal is noise.
	pfc::string8 format_spread(double bpm_spread)
	{
		pfc::string8 out;
		// U+00B1, spelt out so the file's own encoding cannot come into it.
		if (bpm_spread > 0) out << "\xc2\xb1" << pfc::format_float(bpm_spread, 0, 1);
		return out;
	}

	//! The opening-tempo column, blank where there was no beat near the start
	//! to measure one from. Formatted like the BPM column beside it, so the two
	//! can be read against each other at a glance.
	pfc::string8 format_initial(double initial_bpm)
	{
		pfc::string8 out;
		if (initial_bpm > 0) out << format_bpm(initial_bpm).get_ptr();
		return out;
	}

	//! What the BPM tag held before the scan. Shown as the file carried it,
	//! including any decimal point - on this collection a whole number is a
	//! hand tap and a decimal is machine-written, and that distinction is
	//! worth more than a tidy column.
	pfc::string8 format_tag_bpm(const pfc::string8 & tag_bpm)
	{
		return tag_bpm;
	}

	//! The rhythm tag, or an empty string when writing it is switched off.
	//!
	//! Writing it is switched off outright at the moment: the empty string
	//! stops file_info_filter_bpm from touching the tag at all. The detected
	//! rhythm is still shown in the results window, and the advanced-config
	//! entries that used to control this are still there and now do nothing -
	//! restore the two lines below to give them their effect back.
	pfc::string8 rhythm_tag_or_empty()
	{
		pfc::string8 tag;
	//	if (!bpm_config_write_rhythm_tag.get()) return tag;
	//	bpm_config_rhythm_tag.get(tag);
		return tag;
	}
}

LRESULT bpm_result_dialog::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	if (bpm_config_auto_write_tag)
	{
		const pfc::string8 rhythm_tag = rhythm_tag_or_empty();
		metadb_io_v2::get()->update_info_async(
			m_tracks,
			fb2k::service_new<file_info_filter_bpm>(m_tracks, bpm_config_bpm_tag, m_bpm_results,
			                                        rhythm_tag.is_empty() ? nullptr : rhythm_tag.get_ptr(),
			                                        m_rhythms, m_adjusted, m_initial_bpms),
			core_api::get_main_window(),
			metadb_io_v2::op_flag_background | metadb_io_v2::op_flag_delay_ui,
			NULL);

		DestroyWindow();
	}
	else
	{
		CListViewCtrl result_list = GetDlgItem(ID_BPM_RESULT_LIST);

		// Built in sequence rather than at fixed indices: the tag column is
		// only present when there is something to put in it.
		const bool have_tag_bpm =
			std::any_of(m_tag_bpms.begin(), m_tag_bpms.end(),
			            [](const pfc::string8 & v) { return !v.is_empty(); });

		unsigned col = 0;
		listview_helper::insert_column(result_list, col++, "Title", 270);
		// TODO: Remember status of scan result (ie. success, ambiguous, double, half)
	//	 listview_helper::insert_column(result_list, col++, "Status", 60);
		// The BPM for the whole side, what the file already said, the tempo it
		// opens at and how much it moves all read as one group, so they sit
		// together, with the measurement next to the tap it can be judged by.
		m_col_bpm = static_cast<int>(col);
		listview_helper::insert_column(result_list, col++, "BPM", 50);
		if (have_tag_bpm)
		{
			m_col_tag_bpm = static_cast<int>(col);
			listview_helper::insert_column(result_list, col++, "BPM from tag", 60);
		}
		m_col_initial = static_cast<int>(col);
		listview_helper::insert_column(result_list, col++, "Initial BPM", 60);
		m_col_spread = static_cast<int>(col);
		listview_helper::insert_column(result_list, col++, "Fluctuation", 70);
		m_col_rhythm = static_cast<int>(col);
		listview_helper::insert_column(result_list, col++, "Rhythm", 70);
		// TODO: Allow selection of an alternate BPM
	//	 listview_helper::insert_column(result_list, col++, "BPM (Alt)", 50);

		result_list.SetExtendedListViewStyle(LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT);// | LVS_EX_CHECKBOXES);

		string title_column;

		for (t_size index = 0; index < m_infos.get_size(); index++)
		{
			if (m_infos[index].meta_exists("TITLE"))
				title_column = m_infos[index].meta_get("TITLE", 0);
			else
				title_column = pfc::string_filename(m_tracks[index]->get_path());

			// listview_helper indexes rows as unsigned; on a 64 bit build the
			// loop counter is wider than that, so narrow it explicitly.
			const unsigned row = pfc::downcast_guarded<unsigned>(index);

			listview_helper::insert_item(result_list, row, title_column.c_str(), 0);

			format_bpm bpm_value(m_bpm_results[index]);

			listview_helper::set_item_text(result_list, row, m_col_bpm, bpm_value);
			if (m_col_tag_bpm >= 0 && index < m_tag_bpms.size())
				listview_helper::set_item_text(result_list, row, m_col_tag_bpm,
				                               format_tag_bpm(m_tag_bpms[index]));
			if (index < m_initial_bpms.size())
				listview_helper::set_item_text(result_list, row, m_col_initial,
				                               format_initial(m_initial_bpms[index]));
			if (index < m_spreads.size())
				listview_helper::set_item_text(result_list, row, m_col_spread,
				                               format_spread(m_spreads[index]));
			if (row < m_rhythms.size())
				listview_helper::set_item_text(result_list, row, m_col_rhythm, m_rhythms[row]);
		}

		SizeColumnsToContents();

		pfc::string_formatter bpm_tag_label;
		bpm_tag_label << "BPM will be written to %" << bpm_config_bpm_tag.get_ptr() << "% tag.";
		uSetDlgItemText(m_hWnd, ID_RESULT_BPM_TAG, bpm_tag_label);

		EnableScaleBPMButtons();
	}

	return 0;
}

LRESULT bpm_result_dialog::OnOK(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	const pfc::string8 rhythm_tag = rhythm_tag_or_empty();
	metadb_io_v2::get()->update_info_async(
		m_tracks,
		fb2k::service_new<file_info_filter_bpm>(m_tracks, bpm_config_bpm_tag, m_bpm_results,
		                                        rhythm_tag.is_empty() ? nullptr : rhythm_tag.get_ptr(),
		                                        m_rhythms, m_adjusted, m_initial_bpms),
		core_api::get_main_window(),
		metadb_io_v2::op_flag_background | metadb_io_v2::op_flag_delay_ui,
		NULL);

	DestroyWindow();
	return 0;
}

LRESULT bpm_result_dialog::OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	DestroyWindow();
	return 0;
}

LRESULT bpm_result_dialog::OnDoubleBPMClicked(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	ScaleSelectionBPM(2.0);
	return 0;
}

LRESULT bpm_result_dialog::OnHalveBPMClicked(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	ScaleSelectionBPM(0.5);
	return 0;
}

LRESULT bpm_result_dialog::OnItemChanged(LPNMHDR pnmh)
{
	EnableScaleBPMButtons();

	return 0;
}

void bpm_result_dialog::OnClose()
{
	DestroyWindow();
}

void bpm_result_dialog::PostNcDestroy()
{
	delete this;
}

bool bpm_result_dialog::pretranslate_message(MSG *p_msg)
{
	if (m_hWnd != NULL)
	{
		if (IsDialogMessage(p_msg))
		{
			return true;
		}
	}

	return false;
}

void bpm_result_dialog::EnableScaleBPMButtons()
{
	CListViewCtrl listView(GetDlgItem(ID_BPM_RESULT_LIST));

	UINT selected = listView.GetSelectedCount();

	GetDlgItem(ID_DOUBLE_BPM_BUTTON).EnableWindow(selected > 0);
	GetDlgItem(ID_HALVE_BPM_BUTTON).EnableWindow(selected > 0);
}

void bpm_result_dialog::ScaleSelectionBPM(double p_factor)
{
	CWindow result_list = GetDlgItem(ID_BPM_RESULT_LIST);

	int listview_index = -1;
	while ((listview_index = ListView_GetNextItem(result_list, listview_index, LVIS_SELECTED)) != -1)
	{
		m_bpm_results[listview_index] = m_bpm_results[listview_index] * p_factor;
		m_adjusted[listview_index] = true;

		format_bpm bpm_value(m_bpm_results[listview_index]);

		m_infos[listview_index].meta_set(bpm_config_bpm_tag, bpm_value);
		listview_helper::set_item_text(result_list, listview_index, m_col_bpm, bpm_value);

		// The fluctuation and the opening tempo are both quoted in BPM at the
		// level the BPM column shows, so they follow the same factor.
		if (static_cast<std::size_t>(listview_index) < m_initial_bpms.size())
		{
			m_initial_bpms[listview_index] *= p_factor;
			listview_helper::set_item_text(result_list, listview_index, m_col_initial,
			                               format_initial(m_initial_bpms[listview_index]));
		}
		if (static_cast<std::size_t>(listview_index) < m_spreads.size())
		{
			m_spreads[listview_index] *= p_factor;
			listview_helper::set_item_text(result_list, listview_index, m_col_spread,
			                               format_spread(m_spreads[listview_index]));
		}
	}

	// A doubled BPM can be a digit wider than the one it replaced.
	SizeColumnsToContents();
}

//! Every column but the title is sized to the widest string in it, header
//! included, because their contents are generated and there is no useful width
//! to guess for them. The title then takes whatever is left, which is the only
//! way three sized columns and a title fit a dialog of fixed width without a
//! horizontal scrollbar - the hardcoded widths already overflowed it slightly
//! before the fluctuation column was added.
//!
//! Measured with LVM_GETSTRINGWIDTH rather than left to LVSCW_AUTOSIZE: on the
//! last column LVSCW_AUTOSIZE_USEHEADER stretches to fill the control instead
//! of fitting the text, and plain LVSCW_AUTOSIZE ignores the header, so a
//! column whose header is wider than its values comes out clipped. Both are
//! the wrong answer here, where "Fluctuation" is wider than any of its cells.
void bpm_result_dialog::SizeColumnsToContents()
{
	CListViewCtrl result_list = GetDlgItem(ID_BPM_RESULT_LIST);
	if (result_list == NULL) return;

	// ATL asserts on an absent header in a debug build, so it is checked
	// rather than assumed: the list is populated before every call here, but
	// that is not a property this function can see.
	CHeaderCtrl header_ctrl = result_list.GetHeader();
	if (header_ctrl == NULL) return;
	const int count = header_ctrl.GetItemCount();
	if (count <= 0) return;
	// Room for the cell's own padding, which GETSTRINGWIDTH does not include.
	const int padding = 14;
	const int rows = result_list.GetItemCount();

	int used = 0;
	for (int col = 1; col < count; col++)
	{
		TCHAR text[256] = {};
		LVCOLUMN header = {};
		header.mask = LVCF_TEXT;
		header.pszText = text;
		header.cchTextMax = static_cast<int>(std::size(text));
		int widest = 0;
		if (result_list.GetColumn(col, &header))
			widest = result_list.GetStringWidth(text);

		for (int row = 0; row < rows; row++)
		{
			result_list.GetItemText(row, col, text, static_cast<int>(std::size(text)));
			widest = std::max(widest, result_list.GetStringWidth(text));
		}
		result_list.SetColumnWidth(col, widest + padding);
		used += result_list.GetColumnWidth(col);
	}

	// The dialog cannot be resized, so this is settled once. A vertical
	// scrollbar appears as soon as the list is longer than the window, and its
	// width comes out of the client area, so it is allowed for whether or not
	// it is showing yet - a few pixels of slack in the title beats a horizontal
	// scrollbar under a list that is only one row too long.
	CRect client;
	result_list.GetClientRect(&client);
	const int scrollbar = GetSystemMetrics(SM_CXVSCROLL);
	const int title_min = 80;
	const int left = client.Width() - used - scrollbar;
	result_list.SetColumnWidth(0, std::max(title_min, left));
}
