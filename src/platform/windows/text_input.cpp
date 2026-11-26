/**
 * @file src/platform/windows/text_input.cpp
 * @brief Watches whether an editable text field has keyboard focus on the host.
 */
// standard includes
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

// platform includes
#include <windows.h>
#include <objbase.h>
#include <uiautomation.h>

// local includes
#include "misc.h"
#include "src/logging.h"
#include "src/text_input.h"

using namespace std::literals;

namespace text_input {
  namespace {
    // Defined here so we don't depend on libuuid exporting them.
    constexpr GUID kClsidCUIAutomation = {0xff48dba4, 0x60ef, 0x4201, {0xaa, 0x87, 0x54, 0x10, 0x3e, 0xef, 0x59, 0x4e}};
    constexpr GUID kIidIUIAutomation = {0x30cbe57d, 0xd9d0, 0x452a, {0xab, 0x13, 0x7a, 0xc5, 0xac, 0x48, 0x25, 0xee}};
    constexpr GUID kIidIUIAutomationValuePattern = {0xa94cd8b1, 0x0844, 0x4cd6, {0x9d, 0x2d, 0x64, 0x05, 0x37, 0xab, 0x39, 0xe9}};

    // How often to check focus, and how long "no text box" must last before we report it.
    // The delay stops the keyboard from flickering while focus hops between controls.
    constexpr auto kPollInterval = 150ms;
    constexpr int kFalseConfirmPolls = 3;

    std::atomic<bool> g_focused {false};
    std::once_flag g_start_once;

    /**
     * @brief True if the foreground window's thread shows a text caret.
     * @details Catches classic Win32 edits, Word, Notepad and Chromium/Electron inputs.
     */
    bool foreground_has_caret() {
      HWND fg = GetForegroundWindow();
      if (!fg) {
        return false;
      }

      DWORD tid = GetWindowThreadProcessId(fg, nullptr);
      GUITHREADINFO gti {};
      gti.cbSize = sizeof(gti);
      if (!GetGUIThreadInfo(tid, &gti)) {
        return false;
      }

      return gti.hwndCaret != nullptr;
    }

    /**
     * @brief True if UI Automation says the focused element is an editable Edit control.
     * @details Catches WinUI/UWP/WPF text boxes that don't use a Win32 caret.
     */
    bool uia_focused_is_editable(IUIAutomation *uia) {
      if (!uia) {
        return false;
      }

      IUIAutomationElement *el = nullptr;
      if (FAILED(uia->GetFocusedElement(&el)) || !el) {
        return false;
      }

      bool editable = false;
      CONTROLTYPEID type = 0;
      if (SUCCEEDED(el->get_CurrentControlType(&type)) && type == UIA_EditControlTypeId) {
        editable = true;

        IUIAutomationValuePattern *value = nullptr;
        if (SUCCEEDED(el->GetCurrentPatternAs(UIA_ValuePatternId, kIidIUIAutomationValuePattern, (void **) &value)) && value) {
          BOOL read_only = FALSE;
          if (SUCCEEDED(value->get_CurrentIsReadOnly(&read_only)) && read_only) {
            editable = false;
          }
          value->Release();
        }
      }

      el->Release();
      return editable;
    }

    void watch_loop() {
      // Must happen before COM creates anything on this thread.
      platf::syncThreadDesktop();

      bool com_ok = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
      IUIAutomation *uia = nullptr;
      if (com_ok) {
        if (FAILED(CoCreateInstance(kClsidCUIAutomation, nullptr, CLSCTX_INPROC_SERVER, kIidIUIAutomation, (void **) &uia))) {
          uia = nullptr;
        }
      }
      if (!uia) {
        BOOST_LOG(warning) << "Text input watcher: UI Automation unavailable, using caret detection only"sv;
      }

      int false_streak = 0;
      while (true) {
        bool now = foreground_has_caret() || uia_focused_is_editable(uia);

        if (now) {
          false_streak = 0;
          if (!g_focused.exchange(true)) {
            BOOST_LOG(debug) << "Text input focused"sv;
          }
        } else if (++false_streak >= kFalseConfirmPolls) {
          if (g_focused.exchange(false)) {
            BOOST_LOG(debug) << "Text input unfocused"sv;
          }
        }

        std::this_thread::sleep_for(kPollInterval);
      }
    }
  }  // namespace

  bool is_focused() {
    std::call_once(g_start_once, [] {
      std::thread(watch_loop).detach();
    });
    return g_focused.load(std::memory_order_relaxed);
  }
}  // namespace text_input
