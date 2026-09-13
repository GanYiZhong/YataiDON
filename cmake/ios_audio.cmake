# SDL's AudioQueue backend queues at least twice its 15 ms minimum even when
# the app requests 128 frames. Keep the change confined to this iOS target.
set(IOS_AUDIO_QUEUE_MIN_MS "4" CACHE STRING "iOS playback queue half-duration target in ms (15 restores SDL default)")
if(NOT IOS_AUDIO_QUEUE_MIN_MS MATCHES "^[0-9]+$" OR
   IOS_AUDIO_QUEUE_MIN_MS LESS 1 OR IOS_AUDIO_QUEUE_MIN_MS GREATER 40)
  message(FATAL_ERROR "IOS_AUDIO_QUEUE_MIN_MS must be an integer from 1 to 40")
endif()
set(_ios_coreaudio "${SDL3_SOURCE_DIR}/src/audio/coreaudio/SDL_coreaudio.m")
file(READ "${_ios_coreaudio}" _ios_audio_source)
if(NOT _ios_audio_source MATCHES "YATAIDON_IOS_AUDIO_QUEUE_MIN_MS")
  set(_ios_audio_anchor "    device->hidden->numAudioBuffers = numAudioBuffers;")
  string(FIND "${_ios_audio_source}" "${_ios_audio_anchor}" _ios_audio_pos)
  if(_ios_audio_pos EQUAL -1)
    message(FATAL_ERROR "SDL AudioQueue implementation changed; review the iOS latency patch")
  endif()
  set(_ios_audio_replacement [=[
#if defined(SDL_PLATFORM_IOS) && defined(YATAIDON_IOS_AUDIO_QUEUE_MIN_MS)
    if (!device->recording) {
        numAudioBuffers = (msecs < YATAIDON_IOS_AUDIO_QUEUE_MIN_MS)
            ? (int)SDL_ceil(YATAIDON_IOS_AUDIO_QUEUE_MIN_MS / msecs) * 2 : 3;
        SDL_Log("iOS latency: AudioQueue %d x %.2f ms = %.2f ms capacity (not measured output latency)",
                numAudioBuffers, msecs, numAudioBuffers * msecs);
    }
#endif
    device->hidden->numAudioBuffers = numAudioBuffers;
]=])
  string(REPLACE "${_ios_audio_anchor}" "${_ios_audio_replacement}" _ios_audio_source "${_ios_audio_source}")
  file(WRITE "${_ios_coreaudio}" "${_ios_audio_source}")
endif()
target_compile_definitions(SDL3-static PRIVATE
  YATAIDON_IOS_AUDIO_QUEUE_MIN_MS=${IOS_AUDIO_QUEUE_MIN_MS}.0)
