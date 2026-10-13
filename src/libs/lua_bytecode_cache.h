#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct lua_State;

// Compiled Lua chunks kept in cache/lua_bytecode/, so a script is parsed once rather than on
// every launch. Skins ship generated data as Lua tables (several MB for a collab backdrop);
// compiling those takes about a second, loading their bytecode a few tens of milliseconds.
// An entry is used only for the same path spelling, source size and mtime and Lua release;
// debug info is kept, so error lines and chunk names stay those of the source.
namespace lua_bytecode_cache {

// Like luaL_loadfilex(L, path, nullptr): pushes the chunk and returns LUA_OK, or pushes an
// error message and returns the error status.
int load(lua_State* L, const std::string& path);

// Routes require's Lua file searcher and the global loadfile through load().
void install(lua_State* L);

// Compiles, on a background thread, every .lua under the given script directories that has
// no current entry. Paths are spelled "<dir>/<relative path>", as require resolves them
// from a package.path entry "<dir>/?.lua".
void prewarm(const std::vector<std::string>& script_dirs);

// Stops a running prewarm and waits for it.
void stop_prewarm();

}  // namespace lua_bytecode_cache
