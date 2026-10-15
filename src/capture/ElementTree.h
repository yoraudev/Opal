#pragma once
#include <windows.h>

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "core/Geometry.h"

namespace opal {

struct UiElementRect {
  RectI rect;
  int controlType = 0;
};

class ElementTree {
 public:
  ElementTree();
  ~ElementTree();

  void Request(HWND window);
  bool IsReady(HWND window) const;
  std::vector<RectI> CandidatesAt(HWND window, POINT pt, const RectI& clip) const;

 private:
  struct State {
    std::mutex mutex;
    std::condition_variable cv;
    bool quit = false;
    std::vector<HWND> queue;
    HWND inFlight = nullptr;
    std::map<HWND, std::vector<UiElementRect>> cache;
  };
  static void Worker(std::shared_ptr<State> state);
  std::shared_ptr<State> state_;
};

}
