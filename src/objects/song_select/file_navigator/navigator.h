#pragma once

#include "box_song.h"
#include "genre_bg.h"
#include <queue>
#include <array>
#include <unordered_map>

class SongSelectScript;

struct CourseStats {
    int total       = 0;
    int full_combos = 0;
    int clears      = 0;
};

struct InlineState {
    std::unique_ptr<FolderBox> saved_folder_box;
    int folder_index;
    int first_song_index;
    int songs_count;
    bool fading_out = false;
};

using Statistics = std::map<int, std::map<int, CourseStats>>;

class Navigator {
private:
    std::vector<fs::path> root_paths;
    std::vector<std::unique_ptr<BaseBox>> items;
    std::map<std::pair<std::string, std::string>, fs::path> song_files;
    int open_index;
    bool is_init      = false;
    bool is_preloaded = false;
    bool built_hide_dan = false;
    bool reloading_roots = false;
    bool genre_box_defs_built = false;
    std::map<GenreIndex, BoxDef> genre_box_defs;

    std::optional<InlineState>  inline_state;
    std::optional<fs::path>     pending_inline_path;
    FolderBox*                  pending_inline_folder = nullptr;
    BoxDef                      pending_inline_box_def;
    bool is_inline = false;

    std::optional<GenreBG> genre_bg;
    int        genre_bg_start;
    int        genre_bg_end;
    std::optional<float> genre_bg_end_pos;
    GenreIndex bg_genre_index;
    GenreIndex last_bg_genre_index;
    // init() runs before the wheel is loaded (load_all_roots fills it on a worker
    // thread), so the backdrop genre it reads is a guess. While this is set the
    // backdrop keeps following the box under the cursor without a cross-fade;
    // navigating/opening takes over from there.
    bool bg_genre_pending = true;

    FadeAnimation* background_fade_change;
    MoveAnimation* background_move;

    std::thread              loader_thread;
    std::thread              song_files_thread;
    std::mutex               pending_mutex;
    std::queue<std::unique_ptr<BaseBox>> pending_boxes;
    std::queue<std::unique_ptr<BaseBox>> pending_inline_boxes;
    std::atomic<bool>        loading_complete{false};
    std::atomic<bool>        abort_loading{false};

    // Set when a folder is collapsed on returning from a song, so reopening
    // that same folder puts the cursor back on the song that was played.
    std::optional<fs::path>  reopen_folder_path;
    std::optional<fs::path>  reopen_song_path;
    // Where the cursor was when the whole wheel had to be rebuilt, so coming
    // back out of a folder or a game does not dump it on the first box.
    std::optional<fs::path>  restore_cursor_path;

    std::optional<fs::path>  recent_folder_path;
    std::optional<fs::path>  favorite_folder_path;
    std::set<std::string>    favorite_songs;
    std::unordered_map<std::string, bool>    def_file_cache;
    std::unordered_map<std::string, BoxDef>  box_def_cache;

    bool awaiting_diff_sort = false;
    // ROUND 15: {course, level, order}. `order` is the arcade 表示順 row
    // (1 = default, 2 = un-cleared first, 3 = un-FC first, 4 = un-DFC first).
    std::optional<std::array<int,3>> diff_sort_filter;
    std::optional<std::pair<int,int>> last_diff_sort_result;

    bool vertical_gallery = false;

    void navigate(int delta, bool snap);
    void set_positions(bool init, float duration);
    bool is_song_file(const fs::path& path);
    bool is_osu_song_folder(const fs::path& path);
    bool is_gen4_song_folder(const fs::path& path);
    bool is_gen4_root(const fs::path& path);
    bool is_green_root(const fs::path& path);
    fs::path green_root_at(const fs::path& path);
    bool is_green_song_folder(const fs::path& path);
    void load_green_genres(const fs::path& data_root);
    bool load_green_genre_songs(const fs::path& genre_path, const BoxDef& box_def);
    bool only_gen4_songs();
    const BoxDef* box_def_for_genre(GenreIndex genre);
    void load_gen4_genres(const fs::path& data_root);
    bool load_gen4_genre_songs(const fs::path& genre_path, const BoxDef& box_def);
    bool has_def_file(const std::filesystem::path& path);
    fs::path find_box_def_folder(const fs::path& song_path);
    void setup_back_box(const fs::path& path, bool has_children);
    bool has_child_folders(const fs::path& path);

    void wait_for_song_files();
    void enqueue_box(std::unique_ptr<BaseBox> box);
    void enqueue_inline_box(std::unique_ptr<BaseBox> box);
    void parse_song_list(const fs::path& path, BoxDef box_def, bool inline_mode);
    void load_current_directory_async(const fs::path path);
    void load_all_roots();
    void load_collection_difficulty(const fs::path& path, const BoxDef& box_def, int course, int level, int order = 1);
    void load_from_song_list(const fs::path& path, const BoxDef& box_def, bool mark_favorite);
    void load_collection_new(const fs::path& path, const BoxDef& box_def);
    void load_collection_recommended(const fs::path& path, const BoxDef& box_def);
    void load_collection_search(const fs::path& path, const BoxDef& box_def);
    void load_songs_inline_async(const fs::path path, BoxDef box_def);
    void promote_recent_box(const SongBox* song);
    // Close an open folder immediately, with no fade-out, and leave the
    // cursor on the folder itself.
    void collapse_inline_now();
    void flush_pending_boxes();
    void exit_inline();
    void begin_inline_load();

public:
    Navigator();
    ~Navigator();

    bool is_processing = false;
    bool hide_dan = false;
    bool inline_streaming = false;
    fs::path current_path;
    std::string current_search;

    SongSelectScript* script = nullptr;

    void join_loader();
    void preload(std::vector<fs::path> songs_paths);
    void init(std::vector<fs::path> songs_paths);
    void add_to_recent(const SongBox* song);
    void toggle_favorite(SongBox* song);
    void refresh_scores();
    BoxDef parse_box_def(const fs::path& path);
    bool needs_diff_sort() const { return awaiting_diff_sort; }
    bool diff_sort_ready() { return awaiting_diff_sort; }
    void apply_diff_sort(int course, int level, int order = 1);
    void cancel_diff_sort();
    void load_current_directory(const fs::path path);
    bool jump_to_song(const std::string& hash);
    // ROUND 26 (r26-gaugesliver): extracted from jump_to_song so the automation
    // harness can jump straight to a known song folder for scripted, real-hit-
    // timing playthroughs (song_select.cpp's automation_take_song_jump poll)
    // without needing a scores.db diff-hash lookup. jump_to_song(hash) now just
    // resolves hash -> path and delegates here.
    bool jump_to_song_path(const fs::path& song_path);
    void enter_diff_select();
    void exit_diff_select();
    float get_diff_fade_in();
    bool is_directory(BaseBox* item);
    bool is_song(BaseBox* item);
    BaseBox* get_current_item();
    Statistics get_statistics(const fs::path& path);

    std::optional<fs::path> find_song_by_title(const std::string& title, const std::string& subtitle);

    void move_left();
    void move_right();
    void skip_left();
    void skip_right();

    void update(double current_ms);
    void draw_background();
    void draw();
    void draw_diff_select_bg();
    void draw_score_history();

    MoveAnimation* background_move_anim() const { return background_move; }
    FadeAnimation* background_fade_anim() const { return background_fade_change; }
    // The folder currently open inline, or nullptr at the root. Data-out for
    // skins (Navigator:current_folder() in Lua); the pointer stays owned by
    // the navigator and must not be kept across a folder change.
    FolderBox* lua_current_folder() const {
        if (!inline_state.has_value()) return nullptr;
        return inline_state->saved_folder_box.get();
    }
    int bg_genre_frame() const { return genre_to_ref_frame(bg_genre_index); }
    int last_bg_genre_frame() const { return genre_to_ref_frame(last_bg_genre_index); }
};

extern Navigator navigator;
