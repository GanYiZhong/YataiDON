#pragma once

#include <chrono>
#include <rapidjson/document.h>
#include <map>
#include <unordered_map>
#include <set>

inline double get_current_ms() {
    using namespace std::chrono;
    auto now = high_resolution_clock::now();
    return duration<double, std::milli>(now.time_since_epoch()).count();
}

extern double g_frame_ms;

// Returns time frozen at frame start — use this for game/note position calculations
// so render-time variance doesn't cause jitter.
inline double get_frame_ms() {
    return (g_frame_ms > 0.0) ? g_frame_ms : get_current_ms();
}

class BaseAnimation {
protected:
    double delay;
    double delay_saved;
    double start_ms;
    bool unlocked;
    bool loop;
    bool lock_input;

    double easeIn(double progress, const std::string& ease_type);

    double easeOut(double progress, const std::string& ease_type);

    double applyEasing(double progress, const std::optional<std::string>& ease_in_opt,
                      const std::optional<std::string>& ease_out_opt);

public:
    double attribute;
    double duration;
    bool is_finished;
    bool is_started;
    bool is_reversing;

    BaseAnimation(double duration, double delay = 0.0, bool loop = false, bool lock_input = false);

    virtual ~BaseAnimation() = default;

    virtual void update(double current_time_ms);

    virtual void restart();

    void start();

    void pause();

    void unpause();

    virtual void reset();

    virtual std::unique_ptr<BaseAnimation> copy() const = 0;

    bool isFinished() const { return is_finished; }
    bool isStarted() const { return is_started; }
};

class FadeAnimation : public BaseAnimation {
private:
    double initial_opacity;
    double final_opacity;
    double initial_opacity_saved;
    double final_opacity_saved;
    std::optional<std::string> ease_in;
    std::optional<std::string> ease_out;
    std::optional<double> reverse_delay;
    std::optional<double> reverse_delay_saved;

public:
    FadeAnimation(double duration, double initial_opacity = 1.0, bool loop = false,
                  bool lock_input = false, double final_opacity = 0.0, double delay = 0.0,
                  std::optional<std::string> ease_in = std::nullopt,
                  std::optional<std::string> ease_out = std::nullopt,
                  std::optional<double> reverse_delay = std::nullopt);

    void restart() override;

    void update(double current_time_ms) override;

    std::unique_ptr<BaseAnimation> copy() const override;
};

class MoveAnimation : public BaseAnimation {
private:
    int total_distance;
    int total_distance_saved;
    int start_position_saved;
    std::optional<std::string> ease_in;
    std::optional<std::string> ease_out;
    std::optional<double> reverse_delay;
    std::optional<double> reverse_delay_saved;
    // Optional MID-WAY KEY VALUE ("waypoint"), for arcade tweens that are two
    // straight segments through a point that is NOT between start and end - the
    // 演奏オプション board slides 650 -> -75 -> 0 (an overshoot past its resting
    // place) and slides back out 0 -> -75 -> 650. With `waypoint` set the curve
    // is piecewise LINEAR: start -> start+waypoint over [0, waypoint_at], then
    // -> start+total_distance over [waypoint_at, 1], and ease_in/ease_out are
    // ignored. Absent (the default) nothing changes for any existing animation.
    std::optional<int> waypoint;
    double waypoint_at = 0.5;

public:
    int start_position;
    MoveAnimation(double duration, int total_distance = 0, bool loop = false,
                  bool lock_input = false, int start_position = 0, double delay = 0.0,
                  std::optional<double> reverse_delay = std::nullopt,
                  std::optional<std::string> ease_in = std::nullopt,
                  std::optional<std::string> ease_out = std::nullopt,
                  std::optional<int> waypoint = std::nullopt,
                  double waypoint_at = 0.5);

    void restart() override;

    void update(double current_time_ms) override;

    std::unique_ptr<BaseAnimation> copy() const override;
};

class TextureChangeAnimation : public BaseAnimation {
private:
    struct TextureFrame {
        double start;
        double end;
        int index;
    };
    std::vector<TextureFrame> textures;

public:
    TextureChangeAnimation(double duration, const std::vector<std::tuple<double, double, int>>& textures,
                          bool loop = false, bool lock_input = false, double delay = 0.0);

    void reset() override;

    void update(double current_time_ms) override;

    std::unique_ptr<BaseAnimation> copy() const override;
};

class TextStretchAnimation : public BaseAnimation {
public:
    TextStretchAnimation(double duration, double delay = 0.0, bool loop = false, bool lock_input = false);

    void update(double current_time_ms) override;

    std::unique_ptr<BaseAnimation> copy() const override;
};

class TextureResizeAnimation : public BaseAnimation {
private:
    double initial_size;
    double final_size;
    double initial_size_saved;
    double final_size_saved;
    std::optional<std::string> ease_in;
    std::optional<std::string> ease_out;
    std::optional<double> reverse_delay;
    std::optional<double> reverse_delay_saved;

public:
    TextureResizeAnimation(double duration, double initial_size = 1.0, bool loop = false,
                          bool lock_input = false, double final_size = 0.0, double delay = 0.0,
                          std::optional<double> reverse_delay = std::nullopt,
                          std::optional<std::string> ease_in = std::nullopt,
                          std::optional<std::string> ease_out = std::nullopt);

    void restart() override;

    void update(double current_time_ms) override;

    std::unique_ptr<BaseAnimation> copy() const override;
};

// ROUND 54 (r54-anim-engine-referrals): generic `sample` animation type - its
// attribute is one field of one track of a generated Scripts/anim/*.lua table
// (see src/libs/sample_table.h), i.e. real per-frame data off the arcade's own
// Lumen timeline instead of a hand-fitted fade/move/resize ramp.
//
// animation.json keys:
//   "type": "sample", "table": "caution", "track": "#48@5",
//   "field": "a" (default),
//   "range": [f0, f1]  clip-frame window (default: the whole table range);
//                      the animation runs delay -> delay + (f1-f0) frames and
//                      is_finished exactly at the window's end, so consumer
//                      sequencing that keys off is_finished is preserved,
//   "delay", "loop", "lock_input"  as every other type,
//   "scale" / "offset"  attribute = raw * scale + offset (default 1 / 0),
//   "step": true  hold each row's value instead of lerping (frame counters),
//   "duration"  optional override of the window's natural length in ms.
//
// FAIL-SOFT (ANIM_PIPELINE rule): when the table/track/field cannot be
// resolved the parser falls back to the entry's nested "fallback" animation
// object if present, else to a constant attribute from "default" (0.0).
struct SampleTable;
struct SampleTrack;

class SampleAnimation : public BaseAnimation {
private:
    const SampleTable* table;      // never null (parser falls back instead)
    const SampleTrack* track_ptr;  // never null
    int field_col;
    double f0, f1;                 // clip-frame window
    double scale_v, offset_v;
    bool step;
    std::string track_name, field_name;   // kept for copy()

public:
    SampleAnimation(const SampleTable* table, const SampleTrack* trk,
                    const std::string& track_name, const std::string& field_name,
                    int field_col, double f0, double f1, double scale_v,
                    double offset_v, bool step, double duration, double delay,
                    bool loop, bool lock_input);

    void update(double current_time_ms) override;

    std::unique_ptr<BaseAnimation> copy() const override;
};

using namespace rapidjson;

class AnimationParser {
private:
    std::map<int, Value> raw_anims;
    Document::AllocatorType* allocator;

    // Helper to get a value from a JSON object with type checking
    template<typename T>
    std::optional<T> getOptional(const Value& obj, const char* key);

    Value resolveValue(const Value& ref_obj, std::set<int>& visited);

    Value findRefs(int anim_id, std::set<int>& visited);

    std::unique_ptr<BaseAnimation> createAnimation(const Value& anim_obj);

public:
    std::unordered_map<int, std::unique_ptr<BaseAnimation>> parse_animations(const Value& animation_json);

    std::unordered_map<int, std::unique_ptr<BaseAnimation>> parseAnimationsFromString(const std::string& json_str);
};
