#pragma once

// ROUND 54 (r54-anim-engine-referrals): C++ access to the skin's generated
// Scripts/anim/*.lua tables (scratchpad/anim_export.py output, the same files
// Scripts/anim/sampler.lua reads).  Each table is per-frame data sampled off
// the arcade's own Lumen timelines; between two kept rows every field is
// LINEAR, so sampling is a plain lerp (see ANIM_PIPELINE.md / sampler.lua).
//
// Everything here is FAIL-SOFT by contract (the ROUND 8 pipeline rule): a
// missing / unparsable table returns nullptr and the caller keeps whatever
// hand-tuned behaviour it had before.  Nothing throws across this boundary.

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

struct SampleTrack {
    // rows[i][0] = arcade frame, rows[i][1..] = fields in table order.
    std::vector<std::vector<double>> rows;

    double first_frame() const { return rows.empty() ? 0.0 : rows.front()[0]; }
    double last_frame()  const { return rows.empty() ? 0.0 : rows.back()[0]; }
};

struct SampleTable {
    std::string name;          // table name, e.g. "title_idle"
    double fps = 60.0;
    double range0 = 0.0, range1 = 0.0;
    std::vector<std::string> fields;
    std::unordered_map<std::string, int> field_idx;   // field name -> column
    std::map<std::string, double> labels;
    std::unordered_map<std::string, SampleTrack> tracks;

    const SampleTrack* track(const std::string& track_name) const {
        auto it = tracks.find(track_name);
        return it == tracks.end() ? nullptr : &it->second;
    }

    // Column index for a field name; -1 when the table does not carry it.
    int field(const std::string& field_name) const {
        auto it = field_idx.find(field_name);
        return it == field_idx.end() ? -1 : it->second;
    }

    // Lerped (or, with step=true, held-at-previous-row) value of one field at
    // an arcade frame.  Frames outside the track's row span clamp, exactly as
    // sampler.lua's Clip:at does.
    double sample(const SampleTrack& trk, int field_col, double frame,
                  bool step = false) const;

    double ms_per_frame() const { return 1000.0 / (fps > 0 ? fps : 60.0); }
};

// Loads (and caches for the process lifetime) Scripts/anim/<name>.lua from the
// child skin, falling back to the parent skin.  Returns nullptr on any failure
// after logging one warning.  Tables are pure `return { ... }` data files, so
// they are executed in a private Lua state with no libraries opened.
const SampleTable* get_sample_table(const std::string& name);

// ---------------------------------------------------------------------------
// Whole-clip draw maps: a hand-written Scripts/anim/<name>.lua companion that
// tells the engine how to PLAY a full exported clip - which tracks to draw, in
// which order, with which skin texture and which shape-space origin.  Used by
// the TITLE idle loop (title_idle_map); the schema is documented in
// Scripts/anim/ANIM_COVERAGE.md.
// ---------------------------------------------------------------------------

struct ClipMapDraw {
    std::string track;    // track name inside the data table
    std::string tex;      // "subset/name" skin texture, e.g. "camera/idle_c3"
    float ox = 0.0f;      // shape-space bbox origin (bake offset)
    float oy = 0.0f;
};

struct ClipMap {
    std::string table;    // name of the data table this map plays
    double intro0 = 0.0, intro1 = -1.0;   // played once (intro1 < intro0 = none)
    double loop0 = 0.0, loop1 = 0.0;      // then looped
    std::vector<ClipMapDraw> draws;       // in draw order
};

// Same lookup/caching/fail-soft rules as get_sample_table.
const ClipMap* get_clip_map(const std::string& name);
