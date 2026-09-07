#include "stdafx.h"
#include "bpm_auto_analysis_thread.h"
#include "bpm_analysis.h"
#include "preferences.h"
#include "bpm_result_dialog.h"

/***** Threading *****/

bpm_auto_analysis_thread::bpm_auto_analysis_thread(metadb_handle_list_cref p_tracks)
{
	m_tracks.add_items(p_tracks);
	m_infos.set_size(m_tracks.get_count());
}

void bpm_auto_analysis_thread::start()
{
	// A track whose info foobar2000 has not read yet cannot be checked for a
	// BPM tag and has no title to show, so it goes - but it says so. Dropping
	// tracks the user selected without a word is how a selection of twenty
	// comes back as one row.
	{
		bit_array_bittable mask(m_tracks.get_count());

		// For each item in the playlist selection
		for (t_size index = 0; index < m_tracks.get_count(); index++)
		{
			const bool have_info = m_tracks[index]->get_info(m_infos[index]);

			if (!have_info)
			{
				FB2K_console_formatter() << "foo_rubato: no info read yet for "
				                         << m_tracks[index]->get_path()
				                         << ", not analysing it";
			}

			mask.set(index, !have_info);
		}

		m_tracks.remove_mask(mask);
		m_infos.remove_mask(mask);
	}

	if (m_tracks.get_count() == 0) return;

	// Everything selected is analysed. It used to be that one selected track
	// without a BPM tag made every track that had one disappear, silently, so
	// asking for twenty could return a single row; it also left the "BPM from
	// tag" column - which is there so a measurement can be read against the
	// tap beside it - impossible to fill in exactly that case. Analysing
	// writes nothing to the files on its own: the results window is where that
	// is decided.
	//
	// Unless the results window is skipped. With "write tags automatically" on
	// the numbers go straight to the files, so re-analysing a track overwrites
	// whatever its BPM tag held - a hand tap included - with nothing shown
	// first. That is the one case worth asking about.
	if (bpm_config_auto_write_tag)
	{
		t_size tagged = 0;

		for (t_size index = 0; index < m_infos.get_size(); index++)
		{
			if (m_infos[index].meta_exists(bpm_config_bpm_tag)) tagged++;
		}

		const t_size total = m_tracks.get_count();

		if (tagged == total)
		{
			pfc::string_formatter message;
			message << (total == 1 ? "The selected track already has a "
			                       : "All of the selected tracks already have a ")
			        << bpm_config_bpm_tag.get_ptr() << " tag, and \"write tags "
			        << "automatically\" is on - so analysing "
			        << (total == 1 ? "it" : "them")
			        << " overwrites what the tag holds without showing you the "
			        << "results first.\n\nAnalyse anyway?";

			if (uMessageBox(core_api::get_main_window(), message.get_ptr(),
			                "Rubato BPM Analyzer", MB_YESNO | MB_ICONQUESTION) != IDYES)
			{
				return;
			}
		}
		else if (tagged > 0)
		{
			pfc::string_formatter message;
			message << tagged << " of the " << total << " selected tracks already have a "
			        << bpm_config_bpm_tag.get_ptr() << " tag, and \"write tags "
			        << "automatically\" is on - so analysing them overwrites what "
			        << "those tags hold without showing you the results first.\n\n"
			        << "Yes - analyse all " << total << "\n"
			        << "No - analyse only the " << (total - tagged) << " with no "
			        << bpm_config_bpm_tag.get_ptr() << " tag\n"
			        << "Cancel - analyse nothing";

			const int response = uMessageBox(core_api::get_main_window(), message.get_ptr(),
			                                 "Rubato BPM Analyzer",
			                                 MB_YESNOCANCEL | MB_ICONQUESTION);

			if (response != IDYES && response != IDNO) return;

			if (response == IDNO)
			{
				bit_array_bittable mask(total);

				for (t_size index = 0; index < total; index++)
				{
					mask.set(index, m_infos[index].meta_exists(bpm_config_bpm_tag));
				}

				m_tracks.remove_mask(mask);
				m_infos.remove_mask(mask);
			}
		}
	}

	threaded_process::g_run_modeless( 
		this,
		threaded_process::flag_show_abort | 
		threaded_process::flag_show_delayed |
		threaded_process::flag_show_minimize |
		threaded_process::flag_show_progress_dual |
		threaded_process::flag_show_item,
		core_api::get_main_window(),
		"Analysing BPMs..."
		);
}

void bpm_auto_analysis_thread::run(threaded_process_status & p_status, abort_callback & p_abort)
{
	m_bpm_results.resize(0);
	m_rhythms.resize(0);
	m_spreads.resize(0);
	m_initial_bpms.resize(0);

	p_status.set_progress(0, m_tracks.get_size());

	// For each item in the playlist selection
	for (t_size index = 0; index < m_tracks.get_size(); index++)
    {
		// Skip the file if it doesn't exist
		if (!filesystem::g_exists(m_tracks[index]->get_path(), p_abort))
		{
			m_tracks.remove_by_idx(index);
			m_infos.remove_by_idx(index);
			index--;
		}
		else
		{
			p_status.set_item_path(m_tracks[index]->get_location().get_path());

			bpmcore::analysis result;
			try
			{
				result = bpm_analyse(m_tracks[index], p_status, p_abort);
			}
			catch (const exception_aborted &)
			{
				throw;
			}
			catch (const std::exception & exc)
			{
				FB2K_console_formatter() << "foo_rubato: error analysing "
				                         << m_tracks[index]->get_path() << ": " << exc;
			}
			m_bpm_results.push_back(result.bpm);
			m_rhythms.push_back(result.ok ? bpmcore::rhythm_name(result.rhythm) : "");
			m_spreads.push_back(result.ok ? result.bpm_spread : 0.0);
			m_initial_bpms.push_back(result.ok ? result.initial_bpm : 0.0);

			p_status.set_progress(index+1, m_tracks.get_size());
		}

		if (p_abort.is_aborting()) break;
	}
}

void bpm_auto_analysis_thread::on_done(ctx_t p_wnd, bool p_was_aborted)
{

	if (!p_was_aborted && core_api::assert_main_thread())
	{
		bpm_result_dialog* m_result_dialog =
			new bpm_result_dialog(m_tracks, m_infos, m_bpm_results, m_rhythms, m_spreads,
			                      m_initial_bpms);

		m_result_dialog->Create(core_api::get_main_window(), NULL);
		if (m_result_dialog->IsWindow())
		{
			m_result_dialog->ShowWindow(SW_SHOWNORMAL);
		}
	}
}
