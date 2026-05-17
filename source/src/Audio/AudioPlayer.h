#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace AudioPlayer {

struct State;

bool ensure_initialized();

void shutdown();

bool load_wav_bytes(const std::vector<uint8_t>& bytes,
                    const std::string& display_name,
                    std::string* err_out = nullptr);

bool load_wav_path(const std::string& path,
                   std::string* err_out = nullptr);

void start_load_bytes_async(std::vector<uint8_t> bytes,
                            std::string display_name);

bool is_loading();

bool has_source();

void play();
void pause();
void toggle_play();
void stop();

void seek_fraction(float t);

void set_volume(float v);
float get_volume();

void set_loop(bool loop);
bool get_loop();

bool is_playing();

double get_time_sec();

double get_duration_sec();

std::string get_display_name();

int get_sample_rate();
int get_channels();

int get_waveform_peaks(float* low_out, float* high_out, int max_slots);

}
