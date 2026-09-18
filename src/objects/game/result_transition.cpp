#include "result_transition.h"
#include "../../libs/texture.h"

ResultTransition::ResultTransition(PlayerNum player_num)
    : player_num(player_num), is_finished(false), is_started(false) {

    move = dynamic_cast<MoveAnimation*>(global_tex.get_animation(5));
    if (!move) {
        spdlog::error("ResultTransition: animation 5 is not a MoveAnimation");
        return;
    }
    move->reset();

    if (!load("ResultTransition", "result_transition", static_cast<int>(player_num))) return;
    fn_start       = lua_object["start"];
    fn_update      = lua_object["update"];
    fn_draw        = lua_object["draw"];
    fn_is_finished = lua_object["is_finished"];
}

void ResultTransition::start() {
    move->start();
    call(fn_start, "ResultTransition:start");
}

void ResultTransition::update(double current_ms) {
    move->update(current_ms);
    is_started = move->is_started;
    is_finished = move->is_finished;

    if (!is_started) { is_finished = false; return; }

    call(fn_update, "ResultTransition:update", current_ms);
    auto done = call_r<bool>(fn_is_finished, "ResultTransition:is_finished");
    if (done.has_value()) is_finished = is_finished || done.value();
}

void ResultTransition::draw() {
    if (fn_draw.valid()) {
        call(fn_draw, "ResultTransition:draw");
        return;
    }
    draw_default();
}

void ResultTransition::draw_default() {
    auto footer_it = global_tex.textures.find(RESULT_TRANSITION::_1P_SHUTTER_FOOTER);
    if (footer_it == global_tex.textures.end() || !footer_it->second) return;
    const float tex_height = footer_it->second->height;

    const std::string player_str = (player_num == PlayerNum::P2) ? "2p" : "1p";
    uint32_t shutter_enum = (player_num == PlayerNum::TWO_PLAYER)
        ? RESULT_TRANSITION::_1P_SHUTTER
        : tex.get_enum("result_transition/" + player_str + "_shutter");
    float shutter_width = tex.screen_width / 5.0f;
    auto shutter_it = global_tex.textures.find(shutter_enum);
    if (shutter_it != global_tex.textures.end() && shutter_it->second)
        shutter_width = static_cast<float>(shutter_it->second->width);

    float x = 0;
    while (x < tex.screen_width) {
        if (player_num == PlayerNum::TWO_PLAYER) {
            global_tex.draw_texture(RESULT_TRANSITION::_1P_SHUTTER, {
                .frame = 0,
                .x = x,
                .y = (float)(-tex.screen_height + move->attribute)
            });
            global_tex.draw_texture(RESULT_TRANSITION::_2P_SHUTTER, {
                .frame = 0,
                .x = x,
                .y = (float)(tex.screen_height - move->attribute)
            });
            global_tex.draw_texture(RESULT_TRANSITION::_1P_SHUTTER_FOOTER, {
                .x = x,
                .y = (float)(-(tex_height * 3) + move->attribute)
            });
            global_tex.draw_texture(RESULT_TRANSITION::_2P_SHUTTER_FOOTER, {
                .x = x,
                .y = (float)(tex.screen_height + (tex_height * 2) - move->attribute)
            });
        } else {
            global_tex.draw_texture(tex.get_enum("result_transition/" + (player_str + "_shutter")), {
                .frame = 0,
                .x = x,
                .y = (float)(-tex.screen_height + move->attribute)
            });
            global_tex.draw_texture(tex.get_enum("result_transition/" + (player_str + "_shutter")), {
                .frame = 0,
                .x = x,
                .y = (float)(tex.screen_height - move->attribute)
            });
            global_tex.draw_texture(tex.get_enum("result_transition/" + (player_str + "_shutter_footer")), {
                .x = x,
                .y = (float)(-(tex_height * 3) + move->attribute)
            });
            global_tex.draw_texture(tex.get_enum("result_transition/" + (player_str + "_shutter_footer")), {
                .x = x,
                .y = (float)(tex.screen_height + (tex_height * 2) - move->attribute)
            });
        }
        x += shutter_width;
    }
}
