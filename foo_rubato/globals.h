#ifndef __GLOBALS_H__
#define __GLOBALS_H__

#include <SDK/foobar2000.h>
#include <SDK/advconfig_impl.h>
#include "guid.h"

// The tag recording which analysis produced the BPM, and its version. Unlike
// the BPM and rhythm tag names this one is not configurable: it is an
// attribution rather than a place to put data, and a reader looking for it
// has to know what it is called. The value is FOO_RUBATO_ALGORITHM, from the
// generated version.h. Named to match the KeyAlgorithm and TuningAlgorithm
// fields other taggers write.
#define BPM_ALGORITHM_TAG "BpmAlgorithm"

// Config variables
// General
extern cfg_int bpm_config_bpm_precision;
extern cfg_string bpm_config_bpm_tag;
extern cfg_bool bpm_config_auto_write_tag;
// Auto
extern cfg_int bpm_config_seconds_to_read;
extern cfg_int bpm_config_num_bpms_to_calc;
extern cfg_int bpm_config_offset_pct_min;
extern cfg_int bpm_config_offset_pct_max;
extern cfg_int bpm_config_fft_window_size;
extern cfg_int bpm_config_fft_window_slide;
extern cfg_int bpm_config_fft_window_type;
extern cfg_int bpm_config_bpm_min;
extern cfg_int bpm_config_bpm_max;
extern cfg_bool bpm_config_interpolate_flux;
extern cfg_int bpm_config_candidate_selection;
extern cfg_bool bpm_config_output_debug;
// Manual
extern cfg_int bpm_config_taps_to_average;
extern cfg_int bpm_config_seconds_to_reset_average;

// Advanced preferences. These live under Preferences > Advanced > Tools >
// Rubato BPM Analyzer rather than on the component's own page: the page's
// STFT and candidate-selection controls only apply to the legacy engine, and
// the switch that turns the legacy engine back on does not belong next to
// them.
extern advconfig_checkbox_factory bpm_config_use_legacy_engine;
extern advconfig_checkbox_factory bpm_config_write_rhythm_tag;
extern advconfig_string_factory bpm_config_rhythm_tag;

#endif