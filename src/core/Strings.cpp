#include "core/Strings.h"

#include <windows.h>

#include <cwchar>

namespace opal {
namespace {

const wchar_t* const kEn[] = {
#define OPAL_STR_EN(id, en, tr) en,
    OPAL_STRINGS(OPAL_STR_EN)
#undef OPAL_STR_EN
};
const wchar_t* const kTr[] = {
#define OPAL_STR_TR(id, en, tr) tr,
    OPAL_STRINGS(OPAL_STR_TR)
#undef OPAL_STR_TR
};

Lang g_lang = Lang::En;

}

void SetLanguage(const wchar_t* code) {
  if (code && std::wcscmp(code, L"tr") == 0) {
    g_lang = Lang::Tr;
  } else if (code && std::wcscmp(code, L"en") == 0) {
    g_lang = Lang::En;
  } else {
    g_lang = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_TURKISH ? Lang::Tr : Lang::En;
  }
}

Lang CurrentLanguage() { return g_lang; }

const wchar_t* T(S id) {
  const int i = int(id);
  if (i < 0 || i >= int(S::Count)) return L"";
  return g_lang == Lang::Tr ? kTr[i] : kEn[i];
}

}
