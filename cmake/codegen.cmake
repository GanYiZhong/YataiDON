find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(YATAIDON_SKINS_DIR "${CMAKE_SOURCE_DIR}/Skins" CACHE PATH "Skins used for code generation and iOS bundling")

set(SKIN_CONFIG_JSON  "${YATAIDON_SKINS_DIR}/PyTaikoGreen/Graphics/skin_config.json")
set(SKIN_CONFIG_GEN_H "${CMAKE_BINARY_DIR}/generated/skin_config_generated.h")

add_custom_command(
    OUTPUT  ${SKIN_CONFIG_GEN_H}
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/tools/gen_skin_config.py"
            "${SKIN_CONFIG_JSON}"
            "${SKIN_CONFIG_GEN_H}"
    DEPENDS "${SKIN_CONFIG_JSON}" "${CMAKE_SOURCE_DIR}/tools/gen_skin_config.py"
    COMMENT "Generating skin_config_generated.h from skin_config.json"
)
add_custom_target(skin_config_gen DEPENDS ${SKIN_CONFIG_GEN_H})
