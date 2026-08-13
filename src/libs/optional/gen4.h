#pragma once

// Reading the data of the arcade "gen 4" games (Nijiiro and its regional
// builds). Their datatable and fumen files are AES-256-CBC encrypted and then
// gzipped; the keys are derived from a string built into the executable, one
// for the tables and one for the charts.
//
// See research notes in issue #24 for how the formats were established.

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace gen4 {

// DATATABLE_SEED and FUMEN_SEED, the seeds the game derives its two keys
// from, are supplied at compile time (see .env and GEN4_ENABLED in
// CMakeLists.txt); they are not ours to publish in source.

// seed XOR 0x01 -> SHA256 -> uppercase hex -> (SHA256 -> uppercase hex) x1000
// -> MD5 -> uppercase hex, and those 32 ASCII characters are the AES-256 key.
std::vector<uint8_t> derive_key(const std::string& seed);

// Decrypts and inflates one file. Returns an empty vector and logs why if the
// file cannot be read, the padding is wrong (which means the wrong key) or the
// gzip stream is damaged.
std::vector<uint8_t> load_encrypted(const fs::path& path, const std::vector<uint8_t>& key);

// True if the file looks like one of these containers rather than a plain one:
// too short to hold an IV plus a block, or not a multiple of the block size,
// and it cannot be.
bool looks_encrypted(const std::vector<uint8_t>& raw);

// One song as the game's own tables describe it. The five difficulty slots are
// in the game's order: easy, normal, hard, oni, ura.
struct SongEntry {
    std::string id;                 // "10jiku", also the fumen folder name
    int         unique_id = 0;
    int         genre_no  = 0;
    std::string sound_file;         // "sound/song_10jiku", no extension
    int         stars[5]  = {};
    bool        branch[5] = {};
    std::map<std::string, std::string> title;     // YataiDON language code -> text
    std::map<std::string, std::string> subtitle;
};

// The datatable of one game data root, ie. the folder holding datatable/,
// fumen/ and sound/. Loading is all-or-nothing and happens once.
class Library {
public:
    // Reads datatable/musicinfo.bin and datatable/wordlist.bin. Returns false
    // and logs if either is missing or will not decrypt.
    bool load(const fs::path& data_root);

    bool loaded() const { return is_loaded; }
    const fs::path& root() const { return data_root; }

    const SongEntry* find(const std::string& id) const;
    const std::vector<SongEntry>& songs() const { return entries; }

    // The chart of one difficulty, decrypted and inflated, or empty if that
    // difficulty does not exist.
    std::vector<uint8_t> load_chart(const std::string& id, int difficulty) const;

    // Which of the five difficulties this song actually ships a chart for.
    bool has_difficulty(const std::string& id, int difficulty) const;

private:
    bool                   is_loaded = false;
    fs::path               data_root;
    std::vector<SongEntry> entries;
    std::vector<uint8_t>   fumen_key;
};

// The library covering a path, loading it the first time it is asked for.
// Returns null when the path is not inside a game data root.
const Library* library_for(const fs::path& path);

// The data root a path belongs to, or an empty path. A root is recognised by
// holding datatable/musicinfo.bin next to a fumen/ folder.
fs::path find_data_root(const fs::path& path);

// Building blocks, exposed for tests.
std::vector<uint8_t> sha256(const uint8_t* data, size_t len);
std::vector<uint8_t> aes256_cbc_decrypt(const std::vector<uint8_t>& key,
                                        const uint8_t* iv,
                                        const uint8_t* data, size_t len);
std::vector<uint8_t> gzip_inflate(const uint8_t* data, size_t len);

}  // namespace gen4
