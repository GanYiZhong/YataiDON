#include "allnet_indicator.h"
#include "../../libs/network.h"

void AllNetIcon::update(double current_ms) {
    online = network.is_online();
}
void AllNetIcon::draw(float x, float y) {}
