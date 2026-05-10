#pragma once
// AnimPlayer — drives a single animation clip on the loaded model's
// skeleton. One global instance; the model preview ticks it once per
// frame, the per-clip decoder writes the resulting per-bone rotations
// into S.bone_rot_deltas, and ModelPreview's existing skinning shader
// + skeleton overlay render the result without any further hookup.
//
// Phase E scaffold: state machine + tick loop + apply-to-skeleton are
// real. The actual bit-packed decode is a stub (writes identity
// quaternions, so the model holds its rest pose while "playing").
// Once the per-bone body format is reverse-engineered the decoder
// plugs in here without changing the public API or the UI.

#include <cstdint>

namespace Anim {

struct AnimClip;

class AnimPlayer {
public:
    enum class State { Stopped, Playing, Paused };

    // Start playing `clip`. The clip pointer must remain valid for as
    // long as playback continues — we don't copy any data, just hold
    // the pointer (clips live in S.anim_clips for the session).
    // Calling play() while another clip is active replaces it.
    void play(const AnimClip* clip, bool loop = true);
    void pause();
    void resume();
    void stop();

    // Jump the playhead to `seconds` along the active clip without
    // changing playback state. Out-of-range values are clamped to
    // [0, duration]. No-op if no clip is loaded.
    void seek(float seconds);

    // Advance the playback clock by `dt` seconds. Wraps to 0 when
    // looping and the clip's duration is exceeded; transitions to
    // Stopped (and freezes the last applied pose) when not looping.
    // Cheap when stopped — no decode, no S writes.
    void tick(float dt);

    // Decode the current frame and stamp the result into
    // S.bone_rot_deltas. Bone count must match the loaded skeleton's
    // mp.bone_count; mismatches are treated as a no-op so a clip
    // recorded for a different skeleton can't crash the renderer.
    //
    // No-op when stopped or no clip loaded.
    void apply_to_skeleton();

    State        state()    const { return state_; }
    float        time()     const { return time_;  }
    bool         is_loop()  const { return loop_;  }
    const AnimClip* clip()  const { return clip_;  }

    // Toggle the loop flag on the active clip. Persists across
    // play()/pause()/resume() but resets to whatever the next play()
    // call passes in.
    void set_loop(bool on)        { loop_ = on; }

private:
    State           state_ = State::Stopped;
    const AnimClip* clip_  = nullptr;
    float           time_  = 0.0f;
    bool            loop_  = true;
};

// Process-wide singleton. Function-local-static, initialised on first
// use — avoids static-init-order issues with State and the model
// preview globals.
AnimPlayer& global_player();

} // namespace Anim
