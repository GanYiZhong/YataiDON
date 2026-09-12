# Opt-in Simulator executable. It uses the real client but never production
# credentials or the game's Documents/scores database.
if(NOT IOS OR NOT CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
  message(FATAL_ERROR "Network integration checks require an iOS Simulator build")
endif()
if(NOT NETWORK_ENABLED)
  message(FATAL_ERROR "Enable networking to build its integration checks")
endif()
add_executable(network_integration client.cpp ../../src/libs/network.cpp)
set_target_properties(network_integration PROPERTIES MACOSX_BUNDLE FALSE)
target_include_directories(network_integration PRIVATE
  "$<TARGET_PROPERTY:YataiDON,INCLUDE_DIRECTORIES>" ../../src/libs)
target_compile_definitions(network_integration PRIVATE PLATFORM_IOS
  NETWORK_URL="http://127.0.0.1:18765" NETWORK_AUTH_KEY="yataidon-integration-test"
  CLIENT_VERSION="1.0.2")
target_link_libraries(network_integration PRIVATE cpr::cpr spdlog::spdlog
  raylib SDL3::SDL3-static tomlplusplus::tomlplusplus SQLite3_lib)
add_dependencies(network_integration skin_config_gen texture_ids_gen)
