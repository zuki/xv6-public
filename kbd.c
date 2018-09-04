#include <sys/types.h>
#include "x86.h"
#include "defs.h"
#include "kbd.h"

int
kbdgetc(void)
{
  static uint shift;
  static uchar *charcode[4] = {
    normalmap, shiftmap, ctlmap, ctlmap
  };
  uint st, data, c;

  st = inb(KBSTATP);
  if((st & KBS_DIB) == 0)  // キーデータなし
    return -1;
  data = inb(KBDATAP);     // キーデータを読み取る

  if(data == 0xE0){
    shift |= E0ESC;
    return 0;
  } else if(data & 0x80){
    // キーが離された
    data = (shift & E0ESC ? data : data & 0x7F);  // ブレイクコードをメイクコードに変換
    shift &= ~(shiftcode[data] | E0ESC);          // シフトコード解除
    return 0;
  } else if(shift & E0ESC){
    // 直前の文字が E0 escapeだった; 0x80と共に押された
    data |= 0x80;                                // ブレイクコードに変換
    shift &= ~E0ESC;                             // シフトコード解除
  }

  shift |= shiftcode[data];
  shift ^= togglecode[data];
  c = charcode[shift & (CTL | SHIFT)][data];
  if(shift & CAPSLOCK){
    if('a' <= c && c <= 'z')
      c += 'A' - 'a';
    else if('A' <= c && c <= 'Z')
      c += 'a' - 'A';
  }
  return c;
}

void
kbdintr(void)
{
  consoleintr(kbdgetc);
}
