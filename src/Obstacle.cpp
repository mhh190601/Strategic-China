#include "Obstacle.h"

bool Obstacle::Contains(float px, float py) const {
    return px >= x && px <= x + w && py >= y && py <= y + h;
}
