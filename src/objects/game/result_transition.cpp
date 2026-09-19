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

    init_textures();

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

void ResultTransition::init_textures() {
    has_footer = global_tex.has_texture("result_transition/1p_shutter_footer");
    tex_height = has_footer ? global_tex.get_texture("result_transition/1p_shutter_footer")->height : 0.0f;

    const std::string player_str = (player_num == PlayerNum::P2) ? "2p" : "1p";
    const std::string shutter_name = (player_num == PlayerNum::TWO_PLAYER)
        ? "result_transition/1p_shutter"
        : "result_transition/" + player_str + "_shutter";
    shutter_width = tex.screen_width / 5.0f;
    if (global_tex.has_texture(shutter_name))
        shutter_width = static_cast<float>(global_tex.get_texture(shutter_name)->width);

    if (player_num == PlayerNum::TWO_PLAYER) {
        t_shutter_1p = global_tex.get_texture("result_transition/1p_shutter");
        t_shutter_2p = global_tex.get_texture("result_transition/2p_shutter");
        t_footer_1p  = global_tex.get_texture("result_transition/1p_shutter_footer");
        t_footer_2p  = global_tex.get_texture("result_transition/2p_shutter_footer");
    } else {
        t_shutter_player = tex.get_texture("result_transition/" + player_str + "_shutter");
        t_footer_player  = tex.get_texture("result_transition/" + player_str + "_shutter_footer");
    }
}

void ResultTransition::draw_default() {
    if (!has_footer) return;

    float x = 0;
    while (x < tex.screen_width) {
        if (player_num == PlayerNum::TWO_PLAYER) {
            global_tex.draw_texture(t_shutter_1p, {
                .frame = 0,
                .x = x,
                .y = (float)(-tex.screen_height + move->attribute)
            });
            global_tex.draw_texture(t_shutter_2p, {
                .frame = 0,
                .x = x,
                .y = (float)(tex.screen_height - move->attribute)
            });
            global_tex.draw_texture(t_footer_1p, {
                .x = x,
                .y = (float)(-(tex_height * 3) + move->attribute)
            });
            global_tex.draw_texture(t_footer_2p, {
                .x = x,
                .y = (float)(tex.screen_height + (tex_height * 2) - move->attribute)
            });
        } else {
            global_tex.draw_texture(t_shutter_player, {
                .frame = 0,
                .x = x,
                .y = (float)(-tex.screen_height + move->attribute)
            });
            global_tex.draw_texture(t_shutter_player, {
                .frame = 0,
                .x = x,
                .y = (float)(tex.screen_height - move->attribute)
            });
            global_tex.draw_texture(t_footer_player, {
                .x = x,
                .y = (float)(-(tex_height * 3) + move->attribute)
            });
            global_tex.draw_texture(t_footer_player, {
                .x = x,
                .y = (float)(tex.screen_height + (tex_height * 2) - move->attribute)
            });
        }
        x += shutter_width;
    }
}
