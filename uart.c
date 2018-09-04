// Intel 8250シリアルポート(UART)。

#include <sys/types.h>
#include "defs.h"
#include <xv6/param.h>
#include "traps.h"
#include "spinlock.h"
#include <xv6/fs.h>
#include "file.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

#define COM1    0x3f8

static int uart;    // uartがあるか?

void
uartinit(void)
{
  char *p;

  // FIFOを止める
  outb(COM1+2, 0);

  // 9600 ボー、8 データビット、1 ストップビット、パリティオフ。
  outb(COM1+3, 0x80);    // divisorのロックを外す
  outb(COM1+0, 115200/9600);
  outb(COM1+1, 0);
  outb(COM1+3, 0x03);    // divsorをロックする、8 データビット。
  outb(COM1+4, 0);
  outb(COM1+1, 0x01);    // 受信割り込みを有効にする。

  // ステータスが0xFFの場合、シリアルポートがない。
  if(inb(COM1+5) == 0xFF)
    return;
  uart = 1;

  // 事前の割り込み条件を確認する。
  // 割り込みを有効にする。
  inb(COM1+2);
  inb(COM1+0);
  ioapicenable(IRQ_COM1, 0);

  // ここにいることを通知する。
  for(p="xv6...\n"; *p; p++)
    uartputc(*p);
}

void
uartputc(int c)
{
  int i;

  if(!uart)
    return;
  for(i = 0; i < 128 && !(inb(COM1+5) & 0x20); i++)
    microdelay(10);
  outb(COM1+0, c);
}

static int
uartgetc(void)
{
  if(!uart)
    return -1;
  if(!(inb(COM1+5) & 0x01))
    return -1;
  return inb(COM1+0);
}

void
uartintr(void)
{
  consoleintr(uartgetc);
}
