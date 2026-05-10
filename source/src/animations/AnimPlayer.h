#pragma once

#include <cstdint>

namespace Anim {

struct AnimClip;

class AnimPlayer {
public:
    enum class State { Stopped, Playing, Paused };

    void play(const AnimClip* clip, bool loop = true);
    void pause();
    void resume();
    void stop();

    void seek(float seconds);

    void tick(float dt);

    void apply_to_skeleton();

    State        state()    const { return state_; }
    float        time()     const { return time_;  }
    bool         is_loop()  const { return loop_;  }
    const AnimClip* clip()  const { return clip_;  }

    void set_loop(bool on)        { loop_ = on; }

private:
    State           state_ = State::Stopped;
    const AnimClip* clip_  = nullptr;
    float           time_  = 0.0f;
    bool            loop_  = true;
};

AnimPlayer& global_player();

}
