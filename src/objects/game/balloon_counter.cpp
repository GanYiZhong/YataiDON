#include "balloon_counter.h"
#include "../../libs/texture.h"
#include <algorithm>   // ROUND 108: std::min/std::max for the geki inflation ladder

BalloonCounter::BalloonCounter(int count, bool is_2p)
 : balloon_count(0), balloon_total(count), is_popped(false), is_2p(is_2p) {
     fade = (FadeAnimation*)tex.get_animation(7);
     stretch = (TextStretchAnimation*)tex.get_animation(6);
     fade->reset();
     stretch->reset();
}

void BalloonCounter::update_count(int count) {
    if (balloon_count != count) {
        balloon_count = count;
        fade->start();
        stretch->start();
        if (balloon_count == balloon_total) {
            is_popped = true;
        }
    }
}

void BalloonCounter::update(double current_ms, int count) {
    stretch->update(current_ms);
    if (is_popped) fade->update(current_ms);

    if (count != 0) update_count(count);
}

void BalloonCounter::draw(float y) {
    // CORRECTED BY ROUND 107 (reverting ROUND 106, restoring ROUND 104).
    //
    // The 2P balloon rig is a PURE TRANSLATION of the whole 1P rig -- one and
    // the same offset applied to the body, the bubble and the digits, with no
    // mirroring of anything. ROUND 106 replaced that with a per-child
    // reflection about the lane strip; that model is wrong and is removed.
    //
    // 1. CABINET REFERENCE (the thing ROUND 104 and ROUND 106 both lacked).
    //    The user supplied a genuine 39.06 duet frame with a balloon live on
    //    both seats. In it the 2P count bubble sits ABOVE the 2P lane in the
    //    same relative place 1P's sits above the 1P lane, its tail points
    //    DOWN-LEFT at its own balloon exactly like 1P's (it is NOT flipped and
    //    it is NOT at the bottom of the screen), and the 2P balloon body sits
    //    low and left beside the 2P character. Converted to 1080p, bubble-to-
    //    bubble is ~440-480 px and body-to-body ~440 px -- the SAME move for
    //    both rig parts, and that amount is `enso_post.bin`'s own 444.
    //
    // 2. THE TABLE SAYS TRANSLATION TOO, without any image.
    //    `datatable/enso_post.bin` (39.06) part 27 `onpu/action_fusen` appears
    //    with TWO different playerNo-0 bases: y 0 -> 444 (targets 1/3/6/8) and
    //    y 146 -> 590 (targets 4/5). A reflection `y2 = C - y1` cannot give the
    //    same delta from two different bases; a translation can, and +444 fits
    //    both. The same discriminator refutes the reflection for the lane
    //    family as well: `lane`/`taiko`/`lane_hit`/`lane_left`/`hit_effect`/
    //    `combo_number` are 276 -> 540 AND 422 -> 686 (constant +264, not
    //    constant sum), and `name_plate` is 485 -> 595 AND 631 -> 741
    //    (constant +110, which is exactly our shipped `game_nameplate_2p`).
    //    ROUND 106 checked those documents only on target 1, where
    //    `delta = 264` and `sum = 816` are algebraically the same statement,
    //    so its reflection rule fitted a tie it never broke.
    //
    // 3. WHY A UNIFORM TRANSLATION IS THE RIGHT SHAPE (this part of ROUND 106
    //    is correct and is kept): inside `action_fusen.nulm`, `main`
    //    (sprite 55) holds `don` / `fusen` / `fukidashi` at CONSTANT local
    //    transforms across its whole 85-frame timeline, and the movie never
    //    repositions the rig per seat. The only per-seat datum is the single
    //    stage y in `enso_post.bin`. One document, one offset, every child.
    //
    // `game.cpp:638-639` already draws the lanes at the cabinet's own
    // 184*1.5 = 276 and 360*1.5 = 540, and `BalloonCounter::draw(y)` is
    // lane-relative, so the offset the ENGINE adds for 2P is 444 - 264 = 180
    // (1080p px) -- the value the child skin ships in
    // `balloon_counter_2p_offset {x: 0, y: 180}`. Child skin_config values are
    // loaded with scale 1.0 (`texture.cpp:109`), so 180 here IS 180 px.
    //
    // ROUND 106's counter-argument -- that the body must sit on the judgement
    // ring, so +180 "must" be wrong -- is REFUTED by the reference: in the real
    // cabinet frame the 2P balloon body is low and left beside the character
    // and is not centred on the ring at all. The ring was an assumption with no
    // cabinet duet frame behind it.
    //
    // When the key is absent the legacy path runs unchanged (230*screen_scale,
    // digits *1.1, vertically mirrored bubble), so PyTaikoGreen stays
    // bit-identical.
    const SkinInfo* p2 = tex.skin_entry("balloon_counter_2p_offset");
    const bool have_p2_key = is_2p && p2;

    const float legacy_y = is_2p ? 230 * tex.screen_scale : 0.0f;
    // ONE offset for the whole rig. Body, bubble and digits all take it.
    const float rig_x = have_p2_key ? p2->x : 0.0f;
    const float rig_y = have_p2_key ? p2->y : 0.0f;

    const float x_offset = rig_x;
    const float y_offset = have_p2_key ? rig_y : legacy_y;
    const float digit_y_offset = have_p2_key ? rig_y : (legacy_y * 1.1f);
    const float body_x = rig_x;
    const float body_y = have_p2_key ? rig_y : 0.0f;
    // The cabinet flips NOTHING: the 2P bubble's tail points down-left at its
    // own balloon exactly like 1P's. `Mirror::VERTICAL` survives only on the
    // legacy no-key path, for bit-identical PyTaikoGreen.
    const Mirror bubble_mirror = (is_2p && !have_p2_key) ? Mirror::VERTICAL
                                                         : Mirror::NONE;

    if (is_popped) {
        tex.draw_texture(BALLOON::POP, {.frame=7, .x=body_x, .y=y + body_y, .fade=fade->attribute});
    } else if (balloon_count >= 1) {
        // ROUND 108 — the cabinet's inflation ladder, and a reachability bug.
        //
        // `action_fusen.nulm` sprite 30 (`fusen`, 91 frames) is the balloon
        // body. Walked frame by frame this round with
        // `lumen_anim_dump --sprite 30 --range 0,90 --all --leaves`, it has SIX
        // addressable resting states, one per frame label, 15 frames apart:
        //
        //     geki_05 f0  -> shape 17   (119x72, smallest)   = our POP crop 0
        //     geki_04 f15 -> shape 13   (217x154)            = our POP crop 2
        //     geki_03 f30 -> shape 11   (256x186)            = our POP crop 3
        //     geki_02 f45 -> shape  9   (325x270)            = our POP crop 4
        //     geki_01 f60 -> shape  7   (343x306)            = our POP crop 5
        //     geki_00 f75 -> shape  5   (376x352, fullest)   = our POP crop 6
        //
        // Our `pop.png` cells are the cabinet's own shapes: every alpha ink box
        // matches byte-for-byte (119x72 / 169x114 / 217x154 / 256x186 /
        // 325x270 / 343x306 / 376x352, all at cell x=4). The art was never the
        // problem.
        //
        // Crop 1 (shape 15, 169x114) is a TRANSITION-ONLY frame in the cabinet:
        // it exists only at sprite-30 frames 5..9, which are passed through
        // when the timeline plays forward out of `geki_05`. It is never a
        // resting state, so it is not in the ladder below.
        //
        // THE BUG: `min(6, (balloon_count - 1) * 6 / balloon_total)` can never
        // return 6, because `(count-1)*6/total < 6` for every `count <= total`.
        // The fully inflated balloon (`geki_00`, crop 6) was UNREACHABLE and
        // the balloon popped straight out of crop 5. That is a real 「氣球不夠
        // 脹大」 defect independent of any 2P question.
        //
        // NOT ESTABLISHED, recorded as such: the cabinet's own count -> state
        // rule lives in `EnsoGraphicFusen`, which was not disassembled this
        // round. The uniform-sixths mapping below is OURS, inherited in shape
        // from the previous line; only the six-state ART LADDER is verified
        // cabinet data. The one cabinet duet frame available shows 8 remaining
        // at `geki_04` (measured: orange-hue ink 173x141 / 179x138 stage px vs
        // shape 13's 173x135), but that frame's balloon TOTAL is unknown, so it
        // cannot discriminate between candidate mappings.
        static const int GEKI[6] = {0, 2, 3, 4, 5, 6};
        const int step = (balloon_total > 0) ? (balloon_count * 6 / balloon_total) : 0;
        const int balloon_index = GEKI[std::min(5, std::max(0, step))];
        tex.draw_texture(BALLOON::POP, {.frame=balloon_index, .x=body_x, .y=y + body_y, .fade=fade->attribute});
    }
    if (balloon_count > 0) {
        tex.draw_texture(BALLOON::BUBBLE, {.mirror = bubble_mirror, .x=x_offset, .y=y + y_offset, .fade=fade->attribute});
        std::string counter = std::to_string(std::max(0, balloon_total - balloon_count));
        // `drumroll_counter_margin` is shared with the drumroll fan, but the two
        // arcade rigs use different digit pitches (fan 80, balloon 64 — the fan's
        // 96x112 cells at scale 1.0 vs 0.8 in `action_fusen`). Optional skin key
        // `balloon_counter_margin {x}` splits them; without it the shared key is
        // used exactly as before.
        float margin = tex.skin_config[SC::DRUMROLL_COUNTER_MARGIN].x;
        if (const SkinInfo* m = tex.skin_entry("balloon_counter_margin"); m && m->x > 0)
            margin = m->x;
        float total_width = counter.length() * margin;
        for (int i = 0; i < counter.size(); i++) {
            char digit = counter[i];
            tex.draw_texture(BALLOON::COUNTER, {.frame=digit - '0', .x=x_offset - (total_width / 2.0f) + (i * margin), .y=y - (float)stretch->attribute + digit_y_offset, .y2=(float)stretch->attribute, .fade=fade->attribute});
        }
    }
}

bool BalloonCounter::is_finished() const {
    return fade->is_finished;
}
