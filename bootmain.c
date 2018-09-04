// ブートローダ。
//
// bootmain()を呼び出すbootasm.Sと共にブートブロックを形成する。
// bootasm.Sは、プロセッサを32ビットプロテクトモードにした。
// bootmain()は、ディスクのセクタ1からELFカーネルイメージをロードして、
// カーネルのエントリルーチンにジャンプする。

#include "types.h"
#include "elf.h"
#include "x86.h"
#include "memlayout.h"

#define SECTSIZE  512

void readseg(uchar*, uint, uint);

void
bootmain(void)
{
  struct elfhdr *elf;
  struct proghdr *ph, *eph;
  void (*entry)(void);
  uchar* pa;

  elf = (struct elfhdr*)0x10000;  // 開始位置を設定

  // ディスクから最初のページを読み込む
  readseg((uchar*)elf, 4096, 0);

  // ELF実行ファイルか?
  if(elf->magic != ELF_MAGIC)
    return;  // bootasm.Sにエラー処理を任せる

  // 各プログラムセグメントをロードする（phフラグは無視する）
  ph = (struct proghdr*)((uchar*)elf + elf->phoff);
  eph = ph + elf->phnum;
  for(; ph < eph; ph++){
    pa = (uchar*)ph->paddr;
    readseg(pa, ph->filesz, ph->off);
    if(ph->memsz > ph->filesz)
      stosb(pa + ph->filesz, 0, ph->memsz - ph->filesz);
  }

  // ELFヘッダからエントリポイントを呼び出す。
  // 復帰しない!
  entry = (void(*)(void))(elf->entry);
  entry();
}

void
waitdisk(void)
{
  // ディスクの用意ができるのを待機する。
  while((inb(0x1F7) & 0xC0) != 0x40)
    ;
}

// offsetからセクタを1つdstに読み込む。
void
readsect(void *dst, uint offset)
{
  // コマンドを発行する。
  waitdisk();
  outb(0x1F2, 1);   // count = 1
  outb(0x1F3, offset);
  outb(0x1F4, offset >> 8);
  outb(0x1F5, offset >> 16);
  outb(0x1F6, (offset >> 24) | 0xE0);
  outb(0x1F7, 0x20);  // cmd 0x20 - セクタの読み込み

  // データを読み込む。
  waitdisk();
  insl(0x1F0, dst, SECTSIZE/4);
}

// カーネルの'offset'から'count'バイトを物理アドレス'pa'に読み込む。
// 要求以上にコピーする場合がある。
void
readseg(uchar* pa, uint count, uint offset)
{
  uchar* epa;

  epa = pa + count;

  // セクタ境界に切り捨てる。
  pa -= offset % SECTSIZE;

  // バイトをセクタに変換する; カーネルはセクタ1から始まる。
  offset = (offset / SECTSIZE) + 1;

  // これが遅すぎる場合は、一度にもっと多くのセクタを読み込めるかもしれない。
  // 要求以上にメモリに書き込む場合があるが、昇順でロードするので、
  // 問題はない。
  for(; pa < epa; pa += SECTSIZE, offset++)
    readsect(pa, offset);
}
