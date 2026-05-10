// AnimPlayer — one-clip-at-a-time playback driver.
//
// Phase E scaffold: real state machine + clock + apply-to-skeleton
// hookup. The actual per-bone bit-packed decode lives in a TODO — it
// stays the rest pose for now, so the user can hit Play, the clock
// advances, the skeleton overlay holds steady (or animates if a real
// decoder ever fills bone_rot_deltas with non-identity quats), and
// the UI's "playing X.XXs" indicator updates. Drops cleanly back to
// Stopped when looping is off and the clip ends.

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
    // Don't change state — a paused scrub stays paused, a stopped
    // scrub stays stopped (with the new playhead position visible
    // via the scrubber even though no tick is happening).
}

void AnimPlayer::stop() {
    state_ = State::Stopped;
    clip_  = nullptr;
    time_  = 0.0f;
    // Leave S.bone_rot_deltas alone — the rest-pose identity quats
    // were written by MP_Build, and clearing them here would force
    // the user-facing R-rotate edits to also wipe. Stop just unhooks
    // playback; the existing pose keeps showing.
}

void AnimPlayer::tick(float dt) {
    if (state_ != State::Playing || !clip_) return;
    time_ += dt;
    // Real clip duration = frame_count / fps. The TOC's "duration"
    // field is just fps (turned out to be 30.0 for every clip in
    // retail), so we get the frame count from the data-file header.
    // Fall back to 1.0 so a missing/unparseable header doesn't cause
    // a divide-by-zero or freeze the clock at 0.
    float dur = clip_duration_seconds(*clip_);
    if (dur <= 0.0f) dur = 1.0f;
    if (time_ >= dur) {
        if (loop_) {
            // fmod equivalent without the math header pull. Keeps a
            // consistent fractional offset across long ticks.
            while (time_ >= dur) time_ -= dur;
        } else {
            time_  = dur;
            state_ = State::Stopped;
        }
    }
}

void AnimPlayer::apply_to_skeleton() {
    if (state_ == State::Stopped || !clip_) return;

    // Refuse to write if the loaded skeleton's bone count doesn't
    // match the clip's. Mis-matched bone counts mean the clip was
    // authored for a different rig — applying its rotations would
    // produce nonsense. Skipping leaves the rest pose visible, and
    // the UI can surface this via a future "incompatible" tag.
    auto h = global_data_file().parse_clip_header(*clip_);
    if (!h.ok) return;

    // Belt-and-suspenders against a corrupt header that read an
    // absurd bone_count (would otherwise multiply to a huge
    // `expected` and overflow on 32-bit, then loop OOB below).
    // Same bound the decoder enforces.
    if (h.bone_count == 0 || h.bone_count > 4096) return;

    const size_t expected = (size_t)h.bone_count * 4;
    if (S.bone_rot_deltas.size() < expected) return;

    // Real decode path: AnimDecoder pulls the bytes through the
    // mmap'd data file, decodes the 8-frame block containing this
    // time, and lerps within it. The per-channel decoders are
    // currently stubbed (rest pose) — see
    // AnimDecoder::decode_block + docs/ANIM_FORMAT.md Phase F. As
    // each channel ports in the model picks up motion automatically.
    DecodedPose pose;
    if (!global_decoder().decode(*clip_, time_, pose) || !pose.ok) {
        // Decode failed (header bad, data file missing) — leave
        // S.bone_rot_deltas alone so the model keeps its existing
        // pose. We don't stamp identity here: a partially-rotated
        // pose from a previous successful frame is preferable to a
        // sudden snap to rest.
        return;
    }

    // Stamp the decoded quats into S.bone_rot_deltas. Translation
    // slots aren't currently consumed by ModelPreview's skinning
    // shader (which applies rotation deltas only on top of the rest
    // skeleton); when we wire bone-translation through too, this is
    // where it goes.
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

} // namespace Anim
