#pragma once
#include <stdint.h>

enum class KeyType : uint8_t { None, Char, Backspace, Enter, Space, CtrlC, Page, Hide };
struct Key { KeyType type; char ch; };
struct KeyRect { int x, y, w, h; Key key; char label[5]; };

class Keyboard {
public:
  Keyboard(int x, int y, int w, int h);
  void setPage(int p);                       // 0 letters, 1 common symbols, 2 more symbols
  int page() const { return page_; }
  void setCaps(bool caps);
  void setCaseToggle(bool enabled);
  int keyCount() const { return count_; }
  const KeyRect& key(int i) const { return keys_[i]; }
  Key hit(int px, int py) const;             // {None,0} when no key is under the point
private:
  static constexpr int MAX_KEYS = 48;
  void build();
  int x_, y_, w_, h_, page_ = 0, count_ = 0;
  bool caps_ = false;
  bool caseToggle_ = false;
  KeyRect keys_[MAX_KEYS];
};
