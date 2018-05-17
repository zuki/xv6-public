#include "mmu.h"

  # vectors.S はすべてのトラップをここに送信する。
.globl alltraps
alltraps:
  # トラップフレームを構築する。
  pushl %ds
  pushl %es
  pushl %fs
  pushl %gs
  pushal

  # データセグメントを設定する。
  movw $(SEG_KDATA<<3), %ax
  movw %ax, %ds
  movw %ax, %es

  # trap(tf)を呼び出す。ここで、tf=%esp
  pushl %esp
  call trap
  addl $4, %esp

  # 復帰はtrapretへ向けられる...
.globl trapret
trapret:
  popal
  popl %gs
  popl %fs
  popl %es
  popl %ds
  addl $0x8, %esp  # トラップ番号とエラーコード
  iret
