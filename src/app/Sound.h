#pragma once

namespace opal {

enum class ShutterSound : int { Modern = 0, Vintage = 1 };

void PlayShutterSound(ShutterSound sound);

}
