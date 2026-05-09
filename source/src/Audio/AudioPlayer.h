#pragma once
//
// AudioPlayer — in-app PCM playback using miniaudio.
//
// Owns one playback device that streams an int16 PCM buffer. The buffer
// is set once via load_pcm() (after decoding XMA2 -> PCM via FFmpeg, or
// reading a vanilla PCM WAV) and the player handles play/pause/seek/
// volume on top of it.
//
// All public methods are safe to call from the UI thread; the device
// callback runs on miniaudio's audio thread and only touches state via
// atomics.
//

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace AudioPlayer {

struct State;

// Lifecycle ------------------------------------------------------------------

// Create the singleton player. Safe to call multiple times — subsequent
// calls are a no-op. Returns true on success.
bool ensure_initialized();

// Tear down the singleton (stops playback, releases the device).
void shutdown();

// Source loading -------------------------------------------------------------

// Decode the given file bytes (RIFF/WAVE — either PCM, or XMA2 if FFmpeg
// is available) and load the resulting PCM into the player. Returns false
// if the format isn't supported. On success, playback is paused at frame 0;
// call play() to start.
//
// `display_name` is shown in the player UI title.
bool load_wav_bytes(const std::vector<uint8_t>& bytes,
                    const std::string& display_name,
                    std::string* err_out = nullptr);

// Convenience: read the file (or iso:// virtual path) and load it.
bool load_wav_path(const std::string& path,
                   std::string* err_out = nullptr);

// Whether a source is currently loaded (i.e. play() will do something).
bool has_source();

// Playback controls ----------------------------------------------------------

void play();
void pause();
void toggle_play();
void stop();             // pause + seek to 0

// Seek to fractional position [0,1]. Clamped.
void seek_fraction(float t);

// Volume in [0,1]. Linear gain.
void set_volume(float v);
float get_volume();

// Looping toggle (single-clip loop).
void set_loop(bool loop);
bool get_loop();

// Reporting ------------------------------------------------------------------

bool is_playing();

// Current playback position in seconds.
double get_time_sec();

// Total source duration in seconds.
double get_duration_sec();

// Display name of the currently-loaded source ("" if none).
std::string get_display_name();

// Sample rate / channels of the loaded source (0 if none).
int get_sample_rate();
int get_channels();

// Pre-computed waveform envelope for visualization. Each entry is a pair
// (peak_low, peak_high) in [-1,1] for a fixed-width slot of the source.
// Returns the number of slots written (or 0 if nothing's loaded). The
// envelope is computed once when a source is loaded and never changes.
int get_waveform_peaks(float* low_out, float* high_out, int max_slots);

} // namespace AudioPlayer
