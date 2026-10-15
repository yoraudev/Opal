#pragma once
#include "core/Image.h"

namespace opal {

bool GrabScreen(const RectI& area, bool includeCursor, Image* out);

}
