#include "app/Sound.h"

#include <windows.h>
#include <mmsystem.h>

namespace opal {

void PlayShutterSound(ShutterSound sound) {
  const int id = sound == ShutterSound::Vintage ? 102 : 101;
  PlaySoundW(MAKEINTRESOURCEW(id), GetModuleHandleW(nullptr), SND_RESOURCE | SND_ASYNC | SND_NODEFAULT);
}

}
