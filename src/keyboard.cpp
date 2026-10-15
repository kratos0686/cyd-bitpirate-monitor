#include "keyboard.h"
#include <string.h>

Keyboard::Keyboard(int x, int y, int w, int h) : x_(x), y_(y), w_(w), h_(h) { build(); }

void Keyboard::setPage(int p) { page_ = p < 0 ? 0 : (p > 2 ? 2 : p); build(); }
void Keyboard::setCaps(bool caps) { caps_ = caps; build(); }
void Keyboard::setCaseToggle(bool enabled) { caseToggle_ = enabled; build(); }

Key Keyboard::hit(int px, int py) const {
  for (int i = 0; i < count_; i++) {
    const KeyRect& k = keys_[i];
    if (px >= k.x && px < k.x + k.w && py >= k.y && py < k.y + k.h) return k.key;
  }
  return Key{KeyType::None, 0};
}

// Rows are 20 half-units wide. put() takes a width in half-units.
void Keyboard::build() {
  count_ = 0;
  int rowH = h_ / 4;
  for (int r = 0; r < 4; r++) {
    int acc = 0;
    int top = y_ + r * rowH;
    auto put = [&](int hu, KeyType t, char ch, const char* label) {
      if (count_ >= MAX_KEYS) return;
      KeyRect& k = keys_[count_++];
      k.x = x_ + acc * w_ / 20;
      k.w = x_ + (acc + hu) * w_ / 20 - k.x;
      k.y = top; k.h = rowH;
      k.key = Key{t, ch};
      strncpy(k.label, label, sizeof k.label - 1);
      k.label[sizeof k.label - 1] = 0;
      acc += hu;
    };
    auto chars = [&](const char* s) {
      for (; *s; s++) {
        char ch = *s;
        if (caps_ && ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        char l[2] = {ch, 0};
        put(2, KeyType::Char, ch, l);
      }
    };
    if (page_ == 0) {
      if (r == 0) chars("qwertyuiop");
      else if (r == 1) { acc = 1; chars("asdfghjkl"); }
      else if (r == 2) { put(3, KeyType::Page, 0, "123"); chars("zxcvbnm"); put(3, KeyType::Backspace, 0, "<-"); }
      else { put(3, KeyType::CtrlC, 0, caseToggle_ ? (caps_ ? "ABC" : "Aa") : "^C"); put(3, KeyType::Hide, 0, "KB"); put(10, KeyType::Space, ' ', "spc"); put(4, KeyType::Enter, 0, "Ent"); }
    } else if (page_ == 1) {
      if (r == 0) chars("1234567890");
      else if (r == 1) chars("-_/\\.,:;=+");
      else if (r == 2) { put(3, KeyType::Page, 0, "More"); chars("[]<>|&\""); put(3, KeyType::Backspace, 0, "<-"); }
      else { put(3, KeyType::CtrlC, 0, "^C"); put(3, KeyType::Hide, 0, "KB"); put(6, KeyType::Space, ' ', "spc");
             put(2, KeyType::Char, '#', "#"); put(2, KeyType::Char, '@', "@"); put(4, KeyType::Enter, 0, "Ent"); }
    } else {
      if (r == 0) chars("!$%'()*?^`");
      else if (r == 1) chars("{}~");
      else if (r == 2) { put(3, KeyType::Page, 0, "abc"); chars("!$%^*()"); put(3, KeyType::Backspace, 0, "<-"); }
      else { put(3, KeyType::CtrlC, 0, "^C"); put(3, KeyType::Hide, 0, "KB"); put(6, KeyType::Space, ' ', "spc");
             put(2, KeyType::Char, '#', "#"); put(2, KeyType::Char, '@', "@"); put(4, KeyType::Enter, 0, "Ent"); }
    }
  }
}
