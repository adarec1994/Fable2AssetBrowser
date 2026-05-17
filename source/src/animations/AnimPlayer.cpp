#include "AnimPlayer.h"

#include "AnimBank.h"
#include "AnimDataFile.h"
#include "AnimDecoder.h"
#include "../Utilities/State.h"

namespace Anim {

void AnimPlayer::play(const AnimClip* clip, bool loop) {
    clip_  = clip;
    time_  = 0.0f;
    loop_  = loop;
    state_ = clip ? State::Playing : State::Stopped;
}

void AnimPlayer::pause()  { if (state_ == State::Playing) state_ = State::Paused; }
void AnimPlayer::resume() { if (state_ == State::Paused)  state_ = State::Playing; }

void AnimPlayer::seek(float seconds) {
    if (!clip_) return;
    float dur = clip_duration_seconds(*clip_);
    if (dur <= 0.0f) dur = 1.0f;
    if (seconds < 0.0f)    seconds = 0.0f;
    if (seconds > dur)     seconds = dur;
    time_ = seconds;

}

void AnimPlayer::stop() {
    state_ = State::Stopped;
    clip_  = nullptr;
    time_  = 0.0f;

}

void AnimPlayer::tick(float dt) {
    if (state_ != State::Playing || !clip_) return;
    time_ += dt;

    float dur = clip_duration_seconds(*clip_);
    if (dur <= 0.0f) dur = 1.0f;
    if (time_ >= dur) {
        if (loop_) {

            while (time_ >= dur) time_ -= dur;
        } else {
            time_  = dur;
            state_ = State::Stopped;
        }
    }
}

void AnimPlayer::apply_to_skeleton() {
    if (state_ == State::Stopped || !clip_) return;

    auto h = global_data_file().parse_clip_header(*clip_);
    if (!h.ok) return;

    if (h.bone_count == 0 || h.bone_count > 4096) return;

    const size_t expected = (size_t)h.bone_count * 4;
    if (S.bone_rot_deltas.size() < expected) return;

    DecodedPose pose;
    if (!global_decoder().decode(*clip_, time_, pose) || !pose.ok) {

        return;
    }

    for (uint32_t b = 0; b < h.bone_count; ++b) {
        S.bone_rot_deltas[(size_t)b * 4 + 0] = pose.bone_quats[b * 4 + 0];
        S.bone_rot_deltas[(size_t)b * 4 + 1] = pose.bone_quats[b * 4 + 1];
        S.bone_rot_deltas[(size_t)b * 4 + 2] = pose.bone_quats[b * 4 + 2];
        S.bone_rot_deltas[(size_t)b * 4 + 3] = pose.bone_quats[b * 4 + 3];
    }
}

AnimPlayer& global_player() {
    static AnimPlayer inst;
    return inst;
}

}
