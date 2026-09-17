# Portable replacement for `sed -i "112a #include <ctype.h> ..." src/rcore.c || true`.
# Inserts the include after line 112 of src/rcore.c, exactly as the sed `112a`
# command did, but with plain CMake so it works where PATCH_COMMAND runs under
# cmd.exe (no sed, no `true`).
#
# Guarded on the include already being present, so re-running is a no-op --
# and if a future raylib master already includes <ctype.h> itself, this
# correctly does nothing at all.
set(_line "#include <ctype.h>                 // Required for: isalpha() [Used in IsPathFile()]")
file(READ src/rcore.c _content)
string(FIND "${_content}" "#include <ctype.h>" _already)
if(NOT _already EQUAL -1)
    return()
endif()
# Anchor on the stdarg.h include immediately preceding the original insertion
# point (line 112), rather than counting lines -- upstream can reflow the file
# above this point without breaking the patch.
set(_anchor "#include <stdarg.h>                 // Required for: va_list, va_start(), va_end() [Used in TraceLog()]")
string(FIND "${_content}" "${_anchor}" _anchor_pos)
if(_anchor_pos EQUAL -1)
    message(WARNING "src/rcore.c: stdarg.h anchor not found; ctype.h patch skipped")
    return()
endif()
string(LENGTH "${_anchor}" _anchor_len)
math(EXPR _offset "${_anchor_pos} + ${_anchor_len} + 1")
string(SUBSTRING "${_content}" 0 ${_offset} _head)
string(SUBSTRING "${_content}" ${_offset} -1 _tail)
file(WRITE src/rcore.c "${_head}${_line}\n${_tail}")
