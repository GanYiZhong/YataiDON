#include "entry_overlay.h"
#include "../../libs/network.h"

void EntryOverlay::update(double current_ms) {
    online = network.is_online();
}
void EntryOverlay::draw(float x, float y) {}
