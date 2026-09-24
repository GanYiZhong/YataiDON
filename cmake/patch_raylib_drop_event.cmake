# raylib master (SDL3 backend) uses DROP_EVENT_DATA in rcore_desktop_sdl.c
# without ever defining it -- an incomplete SDL2->SDL3 migration. Adds the
# missing define alongside the existing SDL_DROPFILE migration alias, guarded
# so re-running (or a future upstream fix) is a no-op.
set(_file src/platforms/rcore_desktop_sdl.c)
file(READ ${_file} _content)
string(FIND "${_content}" "#define DROP_EVENT_DATA" _already)
if(NOT _already EQUAL -1)
    return()
endif()

set(_sdl3_anchor "#define SDL_DROPFILE  SDL_EVENT_DROP_FILE")
string(FIND "${_content}" "${_sdl3_anchor}" _sdl3_pos)
if(_sdl3_pos EQUAL -1)
    message(WARNING "${_file}: SDL3 DROPFILE anchor not found; drop-event patch skipped")
    return()
endif()
string(REPLACE "${_sdl3_anchor}" "${_sdl3_anchor}\n#define DROP_EVENT_DATA event.drop.data" _content "${_content}")

set(_sdl2_anchor "#else // SDL2 fallback")
string(FIND "${_content}" "${_sdl2_anchor}" _sdl2_pos)
if(_sdl2_pos EQUAL -1)
    message(WARNING "${_file}: SDL2 fallback anchor not found; drop-event patch skipped")
    return()
endif()
string(REPLACE "${_sdl2_anchor}" "${_sdl2_anchor}\n\n#define DROP_EVENT_DATA event.drop.file" _content "${_content}")

file(WRITE ${_file} "${_content}")
