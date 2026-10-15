#include "capture/ElementTree.h"

#include <ole2.h>
#include <uiautomation.h>
#include <wrl/client.h>

#include <algorithm>
#include <thread>

#include "core/Log.h"

namespace opal {
namespace {

using Microsoft::WRL::ComPtr;

bool IsUsefulControlType(int t) {
  switch (t) {
    case UIA_TextControlTypeId:
    case UIA_SeparatorControlTypeId:
    case UIA_ScrollBarControlTypeId:
    case UIA_ThumbControlTypeId:
    case UIA_TitleBarControlTypeId:
    case UIA_ToolTipControlTypeId:
      return false;
    default:
      return true;
  }
}

std::vector<UiElementRect> FetchTree(IUIAutomation* uia, HWND hwnd) {
  std::vector<UiElementRect> out;
  ComPtr<IUIAutomationElement> root;
  if (FAILED(uia->ElementFromHandle(hwnd, &root)) || !root) return out;

  ComPtr<IUIAutomationCacheRequest> cache;
  uia->CreateCacheRequest(&cache);
  cache->AddProperty(UIA_BoundingRectanglePropertyId);
  cache->AddProperty(UIA_ControlTypePropertyId);
  cache->put_AutomationElementMode(AutomationElementMode_None);

  ComPtr<IUIAutomationCondition> control, onscreen, notOffscreen, cond;
  uia->get_ControlViewCondition(&control);
  VARIANT v;
  v.vt = VT_BOOL;
  v.boolVal = VARIANT_TRUE;
  uia->CreatePropertyCondition(UIA_IsOffscreenPropertyId, v, &onscreen);
  uia->CreateNotCondition(onscreen.Get(), &notOffscreen);
  uia->CreateAndCondition(control.Get(), notOffscreen.Get(), &cond);

  ComPtr<IUIAutomationElementArray> all;
  const ULONGLONG t0 = GetTickCount64();
  if (FAILED(root->FindAllBuildCache(TreeScope_Descendants, cond.Get(), cache.Get(), &all)) || !all) return out;
  int n = 0;
  all->get_Length(&n);
  out.reserve(size_t(std::min(n, 50000)));
  for (int i = 0; i < n && i < 50000; ++i) {
    ComPtr<IUIAutomationElement> e;
    if (FAILED(all->GetElement(i, &e)) || !e) continue;
    RECT r{};
    CONTROLTYPEID type = 0;
    if (FAILED(e->get_CachedBoundingRectangle(&r))) continue;
    e->get_CachedControlType(&type);
    if (!IsUsefulControlType(type)) continue;
    RectI rr{r.left, r.top, r.right, r.bottom};
    if (rr.Width() < 6 || rr.Height() < 6) continue;
    out.push_back({rr, type});
  }
  Log(L"UIA: %d elements for %p in %llu ms (%zu kept)", n, hwnd, GetTickCount64() - t0, out.size());
  return out;
}

}

ElementTree::ElementTree() : state_(std::make_shared<State>()) {
  std::thread([s = state_] { Worker(s); }).detach();
}

ElementTree::~ElementTree() {
  {
    std::lock_guard lock(state_->mutex);
    state_->quit = true;
  }
  state_->cv.notify_all();
}

void ElementTree::Request(HWND window) {
  {
    std::lock_guard lock(state_->mutex);
    if (state_->cache.contains(window) || state_->inFlight == window) return;
    auto& q = state_->queue;
    if (std::find(q.begin(), q.end(), window) != q.end()) return;
    q.push_back(window);
  }
  state_->cv.notify_one();
}

bool ElementTree::IsReady(HWND window) const {
  std::lock_guard lock(state_->mutex);
  return state_->cache.contains(window);
}

std::vector<RectI> ElementTree::CandidatesAt(HWND window, POINT pt, const RectI& clip) const {
  std::vector<RectI> out;
  {
    std::lock_guard lock(state_->mutex);
    auto it = state_->cache.find(window);
    if (it == state_->cache.end()) return out;
    for (const auto& e : it->second) {
      if (!e.rect.Contains(pt.x, pt.y)) continue;
      RectI r = e.rect.Intersect(clip);
      if (r.Width() >= 8 && r.Height() >= 8) out.push_back(r);
    }
  }
  std::sort(out.begin(), out.end(), [](const RectI& a, const RectI& b) { return a.Area() < b.Area(); });
  std::vector<RectI> unique;
  for (const auto& r : out) {
    bool dup = false;
    for (const auto& u : unique) {
      if (std::abs(u.left - r.left) <= 2 && std::abs(u.top - r.top) <= 2 && std::abs(u.right - r.right) <= 2 &&
          std::abs(u.bottom - r.bottom) <= 2) {
        dup = true;
        break;
      }
    }
    if (!dup) unique.push_back(r);
  }
  return unique;
}

void ElementTree::Worker(std::shared_ptr<State> s) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ComPtr<IUIAutomation> uia;
  if (FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia)))) {
    CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
  }
  if (uia) {
    ComPtr<IUIAutomation2> uia2;
    if (SUCCEEDED(uia.As(&uia2))) {
      uia2->put_ConnectionTimeout(2000);
      uia2->put_TransactionTimeout(3000);
    }
  }
  for (;;) {
    HWND next = nullptr;
    {
      std::unique_lock lock(s->mutex);
      s->cv.wait(lock, [&] { return s->quit || !s->queue.empty(); });
      if (s->quit) break;
      next = s->queue.back();
      s->queue.pop_back();
      s->inFlight = next;
    }
    std::vector<UiElementRect> tree;
    if (uia) tree = FetchTree(uia.Get(), next);
    std::lock_guard lock(s->mutex);
    s->inFlight = nullptr;
    if (s->quit) break;
    s->cache[next] = std::move(tree);
  }
  uia.Reset();
  CoUninitialize();
}

}
