#pragma once
#include <windows.h>
#include <wincodec.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/Image.h"

namespace opal::output {

bool EncodePng(const Image& img, std::vector<uint8_t>* png, IWICImagingFactory* wic = nullptr);
bool WriteFileBytes(const std::wstring& path, const std::vector<uint8_t>& bytes);
bool LoadImageFile(const std::wstring& path, Image* out);

std::wstring MakeCapturePath(const std::wstring& folder, const std::wstring& prefix);

bool CopyImage(HWND owner, const Image& img, const std::vector<uint8_t>* png);

}
