#include "AnimPlayer.h"

#include "AnimBank.h"
#include "AnimDataFile.h"
#include "AnimDecoder.h"
#include "AnimRigMap.h"
#include "../Utilities/State.h"

namespace Anim {

namespace {

void reset_model_pose_to_rest() {
    const size_t n = S.bone_rot_deltas.size() / 4;
    for (size_t i = 0; i < n; ++i) {
        S.bone_rot_deltas[i * 4 + 0] = 0.0f;
        S.bone_rot_deltas[i * 4 + 1] = 0.0f;
        S.bone_rot_deltas[i * 4 + 2] = 0.0f;
        S.bone_rot_deltas[i * 4 + 3] = 1.0f;
    }
    S.bone_anim_rot_absolute.clear();
    S.bone_anim_rot_present.clear();
    S.bone_anim_trans_delta.clear();
    S.bone_anim_trans_present.clear();
    S.bone_anim_pose_active = false;
}

}

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
    reset_model_pose_to_rest();
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

    const uint32_t model_bones = (uint32_t)(S.bone_rot_deltas.size() / 4);
    if (model_bones == 0) return;

    DecodedPose pose;
    if (!global_decoder().decode(*clip_, time_, pose) || !pose.ok) {

        return;
    }
    reset_model_pose_to_rest();
    S.bone_anim_rot_absolute.assign((size_t)model_bones * 4, 0.0f);
    S.bone_anim_rot_present.assign(model_bones, 0);
    S.bone_anim_trans_delta.assign((size_t)model_bones * 3, 0.0f);
    S.bone_anim_trans_present.assign(model_bones, 0);
    for (uint32_t i = 0; i < model_bones; ++i) {
        S.bone_anim_rot_absolute[(size_t)i * 4 + 3] = 1.0f;
    }
    S.bone_anim_pose_active = true;

    const uint32_t decoded_tracks = (uint32_t)(pose.bone_quats.size() / 4);
    const uint32_t track_count = std::min<uint32_t>(h.bone_count,
                                                    decoded_tracks);
    const std::vector<int> track_to_model = resolve_clip_track_model_bones(
        *clip_, S.mdl_info, model_bones, track_count);
    for (uint32_t track = 0; track < track_count; ++track) {
        const int model_bone = (track < track_to_model.size())
            ? track_to_model[(size_t)track]
            : -1;

        if (model_bone < 0 || model_bone >= (int)model_bones) continue;
        const size_t dst = (size_t)model_bone * 4;
        const size_t src = (size_t)track * 4;
        S.bone_anim_rot_absolute[dst + 0] = pose.bone_quats[src + 0];
        S.bone_anim_rot_absolute[dst + 1] = pose.bone_quats[src + 1];
        S.bone_anim_rot_absolute[dst + 2] = pose.bone_quats[src + 2];
        S.bone_anim_rot_absolute[dst + 3] = pose.bone_quats[src + 3];
        S.bone_anim_rot_present[(size_t)model_bone] = 1;

        if (pose.bone_trans.size() >= (size_t)(track + 1u) * 3) {
            const bool model_root =
                (size_t)model_bone < S.mdl_info.Bones.size() &&
                S.mdl_info.Bones[(size_t)model_bone].ParentID < 0;
            if (!model_root) continue;
            const size_t t_src = (size_t)track * 3;
            const size_t t_dst = (size_t)model_bone * 3;
            const float tx = pose.bone_trans[t_src + 0];
            const float ty = pose.bone_trans[t_src + 1];
            const float tz = pose.bone_trans[t_src + 2];
            S.bone_anim_trans_delta[t_dst + 0] = tx;
            S.bone_anim_trans_delta[t_dst + 1] = ty;
            S.bone_anim_trans_delta[t_dst + 2] = tz;
            if (tx * tx + ty * ty + tz * tz > 1e-10f)
                S.bone_anim_trans_present[(size_t)model_bone] = 1;
        }
    }
}

AnimPlayer& global_player() {
    static AnimPlayer inst;
    return inst;
}

}
