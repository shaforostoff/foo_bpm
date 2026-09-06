#ifndef BPMCORE_H
#define BPMCORE_H

// Tempo and rhythm analysis for Argentine tango recordings.
//
// This library is deliberately free of foobar2000, Windows and any other host:
// it takes mono PCM and standard C++, and nothing else. The foobar2000
// component is a thin shell around it, and the same sources are meant to build
// on macOS, Linux and ARM without change.
//
// Two things come out of one pass over a track:
//
//   * the tempo, expressed on the metrical level a dancer taps - the beat for
//     a tango, the bar for a vals or a milonga;
//   * which of those three rhythms it is, or none of them.
//
// The two are not independent. Deciding the tapped level needs the rhythm, so
// the classifier runs first and the tempo is reported on the level that rhythm
// implies. See docs/tango-analysis.md for how both were derived and measured.

#include <cstddef>
#include <vector>

namespace bpmcore
{

enum rhythm_class
{
	rhythm_tango = 0,
	rhythm_vals,
	rhythm_milonga,
	rhythm_other,
	rhythm_class_count
};

//! "Tango", "Vals", "Milonga" or "Other". Never null.
const char * rhythm_name(int cls);

struct analysis
{
	bool ok = false;         //!< false when the track was too short or too quiet
	double bpm = 0;          //!< tempo on the level a dancer taps
	int rhythm = rhythm_other;
	double confidence = 0;   //!< classifier probability for `rhythm`, 0..1
	double beat_bpm = 0;     //!< the underlying beat, before the level is chosen
	int meter = 0;           //!< beats per bar the grid settled on
	double duration = 0;     //!< seconds of audio analysed
};

//! Optional host hook. Analysis stops early and returns `ok == false` when
//! `cancelled` goes true.
class listener
{
public:
	virtual ~listener() {}
	virtual bool cancelled() { return false; }
	virtual void progress(double fraction) { (void)fraction; }
};

struct options
{
	//! Threads used for the spectral stage, which is over 90% of the run time.
	//! 0 asks the library to decide from the hardware; 1 keeps everything on
	//! the calling thread. The answer is identical either way.
	int threads = 0;
};

//! Longest stretch of audio analysed. A tango side is two to three minutes;
//! the cap only bounds memory on a mis-tagged long file.
double max_seconds();

//! Analyse mono PCM in one call.
analysis analyse(const float * mono, std::size_t count, unsigned sample_rate,
                 listener * l = nullptr, const options * opt = nullptr);

//! Accumulates audio as a decoder produces it, then analyses the lot.
//!
//! The envelope has to be normalised by the track's overall level before it is
//! compressed, and that is not knowable until the whole side has been seen, so
//! the audio is buffered rather than streamed. Mono floats cost about 10MB for
//! a three minute track, which is cheaper than decoding twice.
class collector
{
public:
	explicit collector(unsigned sample_rate);

	unsigned sample_rate() const { return m_rate; }
	std::size_t size() const { return m_mono.size(); }
	//! True once `max_seconds` has been reached; the host can stop decoding.
	bool full() const { return m_mono.size() >= m_limit; }

	//! Both sample types are accepted because hosts differ: foobar2000 hands
	//! over doubles on 64 bit and floats on 32 bit, for instance.
	void add_interleaved(const float * data, std::size_t frames, unsigned channels);
	void add_interleaved(const double * data, std::size_t frames, unsigned channels);
	void add_mono(const float * data, std::size_t count);
	void add_mono(const double * data, std::size_t count);

	analysis finish(listener * l = nullptr, const options * opt = nullptr);

private:
	std::vector<float> m_mono;
	unsigned m_rate;
	std::size_t m_limit;
};

}   // namespace bpmcore

#endif // BPMCORE_H
