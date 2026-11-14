#include "supernova_video_geometry.h"
#include <cassert>

int main()
{
    using SupernovaVideo::fit;
    auto r = fit(400, 240, 800, 480);
    assert(r.x == 0 && r.y == 0 && r.width == 800 && r.height == 480);
    r = fit(640, 480, 800, 480);
    assert(r.x == 80 && r.y == 0 && r.width == 640 && r.height == 480);
    r = fit(1920, 1080, 800, 480);
    assert(r.x == 0 && r.y == 15 && r.width == 800 && r.height == 450);
    r = fit(480, 800, 800, 480);
    assert(r.x == 256 && r.y == 0 && r.width == 288 && r.height == 480);
    assert(fit(0, 240, 800, 480).width == 0);
    assert(fit(1000, 1, 800, 480).width == 0);
}
