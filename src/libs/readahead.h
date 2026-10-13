#pragma once

#include <string>

// Warms the OS file cache with the screens likely to come next, so their textures and sounds
// come from memory instead of the disk when the player gets there. On a hard drive a screen's
// few hundred small PNGs take seconds to read cold; reading them while the player is still
// on the previous screen hides that. Runs on one background thread at low CPU and I/O
// priority (POSIX: posix_fadvise WILLNEED), holds no data itself, and a newer request
// replaces an unfinished one.
namespace readahead {

// Called once a screen has initialized.
void after_screen(const std::string& screen_name);

// Stops the thread (at shutdown).
void stop();

}  // namespace readahead
