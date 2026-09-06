#ifndef __BPM_ANALYSIS_H__
#define __BPM_ANALYSIS_H__

#include <SDK/foobar2000.h>

#include <bpmcore/bpmcore.h>

//! Decode one track and analyse it with whichever engine is selected.
//!
//! All the signal processing lives in bpmcore, which knows nothing about
//! foobar2000; this is the shell that feeds it audio and reports progress.
bpmcore::analysis bpm_analyse(const metadb_handle_ptr & track,
                              threaded_process_status & status,
                              abort_callback & abort);

#endif // __BPM_ANALYSIS_H__
