#pragma once
#include <d2d1_1.h>

namespace opal {

enum class Icon {
  Screen, Window, Region, Element, ChevronDown, ChevronRight, Close, Timer, Pointer, Folder,
  Clipboard, Pen, Highlighter, Arrow, Rect, Ellipse, Text, Blur, Pixelate, Crop, Undo, Redo,
  Copy, Download, Trash, Check, Gear, Keyboard, Camera, Info, Sparkle, Select, Plus, Minus,
  Shadow, Photo,
};

void DrawIcon(ID2D1DeviceContext* dc, ID2D1Brush* brush, Icon icon, const D2D1_RECT_F& box,
              float strokeWidth = 1.5f);

}
