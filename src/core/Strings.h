#pragma once

namespace opal {

#define OPAL_STRINGS(X)                                                                          \
  X(AppName, L"Opal", L"Opal")                                                                   \
  X(Tagline, L"macOS-style screenshots for Windows", L"Windows için macOS tarzı ekran görüntüleri") \
  X(TrayCapture, L"Capture…", L"Ekran görüntüsü al…")                                          \
  X(TrayScreen, L"Capture Screen", L"Ekranı yakala")                                            \
  X(TrayRegion, L"Capture Region", L"Alan yakala")                                              \
  X(TrayWindow, L"Capture Window", L"Pencere yakala")                                           \
  X(TrayElement, L"Capture Element", L"Öğe yakala")                                             \
  X(TrayOpenFolder, L"Open Captures Folder", L"Yakalamalar klasörünü aç")                       \
  X(TraySettings, L"Settings…", L"Ayarlar…")                                                   \
  X(TrayQuit, L"Quit Opal", L"Opal'dan çık")                                                    \
  X(ModeScreen, L"Screen", L"Ekran")                                                            \
  X(ModeWindow, L"Window", L"Pencere")                                                          \
  X(ModeRegion, L"Region", L"Alan")                                                             \
  X(ModeElement, L"Element", L"Öğe")                                                            \
  X(Options, L"Options", L"Seçenekler")                                                         \
  X(Capture, L"Capture", L"Yakala")                                                             \
  X(SaveTo, L"Save to", L"Kaydet")                                                              \
  X(Timer, L"Timer", L"Zamanlayıcı")                                                            \
  X(TimerNone, L"None", L"Yok")                                                                 \
  X(Timer5, L"5 Seconds", L"5 saniye")                                                          \
  X(Timer10, L"10 Seconds", L"10 saniye")                                                       \
  X(ShowPointer, L"Show Mouse Pointer", L"Fare imlecini göster")                                \
  X(ShowThumbnail, L"Show Floating Thumbnail", L"Yüzen küçük resmi göster")                     \
  X(WindowShadow, L"Window Shadow", L"Pencere gölgesi")                                         \
  X(DestClipboard, L"Clipboard", L"Pano")                                                       \
  X(DestFolder, L"Folder", L"Klasör")                                                           \
  X(DestBoth, L"Clipboard + Folder", L"Pano + Klasör")                                          \
  X(HintRegion, L"Drag to select   ·   Space: window   ·   Esc: cancel",                         \
    L"Seçmek için sürükle   ·   Boşluk: pencere   ·   Esc: iptal")                              \
  X(HintWindow, L"Click a window   ·   Alt: no shadow   ·   Space: region",                        \
    L"Bir pencereye tıkla   ·   Alt: gölgesiz   ·   Boşluk: alan")                              \
  X(HintElement, L"Click an element or drag around it   ·   Scroll: bigger/smaller   ·   Alt: no snap", \
    L"Bir öğeye tıkla ya da etrafını sürükle   ·   Tekerlek: büyük/küçük   ·   Alt: yakalama kapalı") \
  X(HintScreen, L"Click a screen to capture it", L"Yakalamak için bir ekrana tıkla")            \
  X(HintBarRegion, L"Drag to select, then press Capture or Enter",                              \
    L"Sürükleyerek seç, sonra Yakala'ya ya da Enter'a bas")                                    \
  X(Copy, L"Copy", L"Kopyala")                                                                  \
  X(SaveAs, L"Save As…", L"Farklı kaydet…")                                                    \
  X(ShowInFolder, L"Show in Folder", L"Klasörde göster")                                        \
  X(Markup, L"Markup", L"İşaretle")                                                             \
  X(Delete, L"Delete", L"Sil")                                                                  \
  X(Close, L"Close", L"Kapat")                                                                  \
  X(Done, L"Done", L"Bitti")                                                                    \
  X(SaveChangesQ, L"Save your markup before closing?",                                          \
    L"Kapatmadan önce işaretlemeler kaydedilsin mi?")                                           \
  X(MarkupTitle, L"Opal — Markup", L"Opal — İşaretleme")                                       \
  X(Color, L"Color", L"Renk")                                                                   \
  X(Minimize, L"Minimize", L"Simge durumuna küçült")                                            \
  X(Maximize, L"Maximize", L"Ekranı kapla")                                                     \
  X(ToolSelect, L"Select", L"Seç")                                                              \
  X(ToolPen, L"Pen", L"Kalem")                                                                  \
  X(ToolHighlighter, L"Highlighter", L"Fosforlu kalem")                                         \
  X(ToolArrow, L"Arrow", L"Ok")                                                                 \
  X(ToolRect, L"Rectangle", L"Dikdörtgen")                                                      \
  X(ToolEllipse, L"Ellipse", L"Elips")                                                          \
  X(ToolText, L"Text", L"Metin")                                                                \
  X(ToolBlur, L"Blur", L"Bulanıklaştır")                                                        \
  X(ToolPixelate, L"Pixelate", L"Pikselleştir")                                                 \
  X(ToolCrop, L"Crop", L"Kırp")                                                                 \
  X(Undo, L"Undo", L"Geri al")                                                                  \
  X(Redo, L"Redo", L"Yinele")                                                                   \
  X(Settings, L"Settings", L"Ayarlar")                                                          \
  X(PageGeneral, L"General", L"Genel")                                                          \
  X(PageShortcuts, L"Shortcuts", L"Kısayollar")                                                 \
  X(PageCapture, L"Capture", L"Yakalama")                                                       \
  X(PageOutput, L"Output", L"Çıktı")                                                            \
  X(PageAbout, L"About", L"Hakkında")                                                           \
  X(LaunchAtStartup, L"Launch at startup", L"Windows ile başlat")                               \
  X(LaunchAtStartupDesc, L"Opal waits quietly in the tray.", L"Opal sistem tepsisinde sessizce bekler.") \
  X(UsePrintScreen, L"Use the Print Screen key", L"Print Screen tuşunu kullan")                 \
  X(UsePrintScreenDesc, L"Print Screen opens Opal instead of the Snipping Tool.",               \
    L"Print Screen, Ekran Alıntısı yerine Opal'ı açar.")                                        \
  X(UseWinShiftS, L"Use Win+Shift+S", L"Win+Shift+S kısayolunu kullan")                         \
  X(UseWinShiftSDesc, L"Win+Shift+S opens Opal instead of the Snipping Tool.",                  \
    L"Win+Shift+S, Ekran Alıntısı yerine Opal'ı açar.")                                         \
  X(DisableSnipKey, L"Turn off Windows' own Print Screen shortcut",                              \
    L"Windows'un kendi Print Screen kısayolunu kapat")                                          \
  X(DisableSnipKeyDesc, L"Changes the Windows setting “Use the Print screen key to open screen capture”.", \
    L"Windows'taki “Ekran yakalamayı açmak için Print Screen tuşunu kullan” ayarını değiştirir.") \
  X(PlaySound, L"Play capture sound", L"Yakalama sesi çal")                                     \
  X(ShutterSoundLabel, L"Shutter sound", L"Deklanşör sesi")                                   \
  X(SoundModern, L"DSLR", L"DSLR")                                                              \
  X(SoundVintage, L"Film camera", L"Film makinesi")                                           \
  X(Language, L"Language", L"Dil")                                                              \
  X(LangSystem, L"System", L"Sistem")                                                           \
  X(Appearance, L"Appearance", L"Görünüm")                                                      \
  X(ThemeSystem, L"System", L"Sistem")                                                          \
  X(ThemeLight, L"Light", L"Açık")                                                              \
  X(ThemeDark, L"Dark", L"Koyu")                                                                \
  X(ShortcutBar, L"Show capture bar", L"Yakalama çubuğunu göster")                              \
  X(ShortcutScreen, L"Capture screen", L"Ekranı yakala")                                        \
  X(ShortcutRegion, L"Capture region", L"Alan yakala")                                          \
  X(ShortcutWindow, L"Capture window", L"Pencere yakala")                                       \
  X(ShortcutElement, L"Capture element", L"Öğe yakala")                                         \
  X(PressKeys, L"Press keys…", L"Tuşlara basın…")                                              \
  X(NotSet, L"Not set", L"Atanmadı")                                                            \
  X(Clear, L"Clear", L"Temizle")                                                                \
  X(ShortcutsDesc, L"Click a shortcut, then press the new key combination. Esc cancels.",       \
    L"Bir kısayola tıklayın, ardından yeni tuş kombinasyonuna basın. Esc iptal eder.")         \
  X(ShortcutInUse, L"Already used by another action", L"Başka bir işlem tarafından kullanılıyor") \
  X(SectionWindows, L"Window captures", L"Pencere yakalamaları")                                \
  X(WindowShadowDesc, L"Adds a soft macOS-style shadow. Hold Alt while clicking to skip it.",   \
    L"macOS tarzı yumuşak gölge ekler. Atlamak için tıklarken Alt'a basılı tutun.")             \
  X(SectionGeneralCapture, L"While capturing", L"Yakalarken")                                   \
  X(ShowLoupe, L"Show magnifier", L"Büyüteç göster")                                            \
  X(FreezeScreen, L"Freeze the screen", L"Ekranı dondur")                                       \
  X(SectionElement, L"Element captures", L"Öğe yakalamaları")                                   \
  X(SnapEdges, L"Snap to edges", L"Kenarlara yapış")                                            \
  X(SnapEdgesDesc, L"Finds the exact bounds of panels, cards and buttons.",                     \
    L"Panellerin, kartların ve düğmelerin tam sınırlarını bulur.")                              \
  X(AutoCorners, L"Detect rounded corners", L"Yuvarlak köşeleri algıla")                        \
  X(AutoCornersDesc, L"Measures the corner radius and cuts the capture to match.",              \
    L"Köşe yarıçapını ölçer ve görüntüyü ona göre keser.")                                      \
  X(ElementShadow, L"Shadow on elements", L"Öğelerde gölge")                                    \
  X(SectionThumbnail, L"Floating thumbnail", L"Yüzen küçük resim")                              \
  X(ThumbnailDuration, L"Keep on screen", L"Ekranda kalma süresi")                              \
  X(SectionAfterCapture, L"After capture", L"Yakaladıktan sonra")                               \
  X(CopyToClipboard, L"Copy to clipboard", L"Panoya kopyala")                                   \
  X(SaveToFolder, L"Save to folder", L"Klasöre kaydet")                                         \
  X(Folder, L"Folder", L"Klasör")                                                               \
  X(Change, L"Change…", L"Değiştir…")                                                          \
  X(Open, L"Open", L"Aç")                                                                       \
  X(FilePrefix, L"File name", L"Dosya adı")                                                     \
  X(FilePrefixDesc, L"Files are named “%s 2026-10-15 at 14.03.27.png”.",                        \
    L"Dosyalar “%s 2026-10-15 14.03.27.png” şeklinde adlandırılır.")                            \
  X(Version, L"Version", L"Sürüm")                                                              \
  X(AboutBody, L"Press Print Screen anywhere. Pick a window for a perfect rounded shot, or an element inside an app and Opal finds its edges and corners for you.", \
    L"Her yerde Print Screen'e basın. Kusursuz yuvarlak köşeli bir görüntü için bir pencere seçin ya da bir uygulamanın içindeki bir öğeyi seçin; Opal kenarlarını ve köşelerini sizin için bulur.") \
  X(SettingsLog, L"Open log", L"Günlüğü aç")                                                     \
  X(Seconds, L"s", L" sn")                                                                      \
  X(CaptureFailed, L"Couldn't capture that.", L"Bu yakalanamadı.")                              \
  X(SaveFailed, L"Couldn't save the file.", L"Dosya kaydedilemedi.")                            \
  X(AlreadyRunning, L"Opal is running in the tray.", L"Opal sistem tepsisinde çalışıyor.")

enum class S : int {
#define OPAL_STR_ENUM(id, en, tr) id,
  OPAL_STRINGS(OPAL_STR_ENUM)
#undef OPAL_STR_ENUM
  Count
};

enum class Lang { En, Tr };

void SetLanguage(const wchar_t* code);
Lang CurrentLanguage();
const wchar_t* T(S id);

}
