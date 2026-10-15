#pragma once
#include <windows.h>

#include "capture/WindowList.h"
#include "core/Image.h"

namespace opal {

bool CaptureWindowContent(const WindowInfo& w, Image* out);

}
