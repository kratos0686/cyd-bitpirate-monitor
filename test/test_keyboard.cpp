#include "check.h"
#include "keyboard.h"

SUITE(keyboard_tests) {
  Keyboard kb(0, 104, 320, 136);
  CHECK_EQ(kb.page(), 0);
  Key k = kb.hit(16, 121);       CHECK(k.type == KeyType::Char);      CHECK_EQ(k.ch, 'q');
  k = kb.hit(20, 148 + 10);      CHECK(k.type == KeyType::Char);      CHECK_EQ(k.ch, 'a');   // row 1 starts half a key in
  k = kb.hit(5, 148 + 10);       CHECK(k.type == KeyType::None);
  k = kb.hit(2, 172 + 5);        CHECK(k.type == KeyType::Page);
  k = kb.hit(318, 172 + 5);      CHECK(k.type == KeyType::Backspace);
  k = kb.hit(5, 211);            CHECK(k.type == KeyType::CtrlC);
  k = kb.hit(50, 211);           CHECK(k.type == KeyType::Hide);
  k = kb.hit(160, 211);          CHECK(k.type == KeyType::Space);     CHECK_EQ(k.ch, ' ');
  k = kb.hit(310, 211);          CHECK(k.type == KeyType::Enter);
  k = kb.hit(160, 50);           CHECK(k.type == KeyType::None);      // above the keyboard

  kb.setPage(1);
  CHECK_EQ(kb.page(), 1);
  k = kb.hit(16, 121);           CHECK_EQ(k.ch, '1');
  k = kb.hit(2, 172 + 5);        CHECK(k.type == KeyType::Page);
  k = kb.hit(200, 211);          CHECK(k.type == KeyType::Char);      CHECK_EQ(k.ch, '#');   // x 192..223

  for (int page = 0; page < 2; page++) {
    kb.setPage(page);
    for (int i = 0; i < kb.keyCount(); i++) {
      const KeyRect& a = kb.key(i);
      CHECK(a.w > 0 && a.h > 0 && a.x >= 0 && a.y >= 104 && a.x + a.w <= 320 && a.y + a.h <= 240);
      for (int j = i + 1; j < kb.keyCount(); j++) {      // no overlaps
        const KeyRect& b = kb.key(j);
        bool apart = a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
        CHECK(apart);
      }
    }
  }
}
