#ifndef __BPM_ANALYSIS_H__
#define __BPM_ANALYSIS_H__

#include <SDK/foobar2000.h>

#include <bpmcore/bpmcore.h>

//! Where one track's progress goes while it is being analysed.
//!
//! Not threaded_process_status: several tracks are scanned side by side, and
//! that interface is handed to the one worker thread the progress dialog
//! started, with nothing promising it is safe from more. Each scanning thread
//! reports through its own sink instead, and whichever thread owns the dialog
//! reads them.
class bpm_analysis_progress
{
public:
	virtual ~bpm_analysis_progress() {}
	//! How far through this one track, 0 to 1. Called from the thread doing
	//! the analysing, which is not the thread driving the dialog.
	virtual void fraction(double f) { (void) f; }
};

//! Decode one track and analyse it.
//!
//! All the signal processing lives in bpmcore, which knows nothing about
//! foobar2000; this is the shell that feeds it audio and reports progress.
//!
//! `analysis_threads` goes straight to bpmcore: 0 to spread the spectral stage
//! across the machine, 1 to keep it on the calling thread - which is what to
//! ask for when whole tracks are already running in parallel.
bpmcore::analysis bpm_analyse(const metadb_handle_ptr & track,
                              bpm_analysis_progress & progress,
                              abort_callback & abort,
                              int analysis_threads = 0);

#endif // __BPM_ANALYSIS_H__
