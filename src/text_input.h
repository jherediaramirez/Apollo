/**
 * @file src/text_input.h
 * @brief Tells whether a text box on the host currently has keyboard focus.
 *
 * Used to ask the client to show its on-screen keyboard automatically
 * (Apollo protocol extension 0x3003).
 */
#pragma once

namespace text_input {
#ifdef _WIN32
  /**
   * @brief Whether an editable text field has keyboard focus right now.
   * @details Starts a background watcher on first call. Cheap to call often.
   */
  bool is_focused();
#else
  inline bool is_focused() {
    return false;
  }
#endif
}  // namespace text_input
