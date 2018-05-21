#include "asm.h"
#include "memlayout.h"
#include "mmu.h"

# 最初のCPUを起動する: 32ビットプロテクトモードに切り替えて
# Cの関数にジャンプする。
# BIOSはこのコードをHDの第1セクタから物理アドレス 0x7c00のメモリに
# ロードして、%cs=0, %ip=7c00 として、リアルモードで実行を開始する。

.code16                       # 16ビットモードでアセンブルする
.globl start
start:
  cli                         # BIOSが有効にした割り込みを禁止する。

  # データセグメントレジスタ DS, ES, SS を0クリア。
  xorw    %ax,%ax             # %ax を0にセット
  movw    %ax,%ds             # -> データセグメント
  movw    %ax,%es             # -> 拡張セグメント
  movw    %ax,%ss             # -> スタックセグメント

  # 物理アドレスラインA20は0にセットされており、はじめて2MBのメモリを持つ
  # PCで1MBを想定したソフトウェアを実行できるようした。これを元に戻す。
seta20.1:
  inb     $0x64,%al               # busy状態が解消されるまで待機
  testb   $0x2,%al
  jnz     seta20.1

  movb    $0xd1,%al               # 0xd1 -> port 0x64
  outb    %al,$0x64

seta20.2:
  inb     $0x64,%al               # busy状態が解消されるまで待機
  testb   $0x2,%al
  jnz     seta20.2

  movb    $0xdf,%al               # 0xdf -> port 0x60
  outb    %al,$0x60

  # リアルモードからプロテクトモードに切り替える。仮想アドレスをそのまま
  # 物理アドレスにマップするブートストラップGDTを使うことにより、
  # 実効メモリマップが移行期間中変更されない。
  lgdt    gdtdesc
  movl    %cr0, %eax
  orl     $CR0_PE, %eax
  movl    %eax, %cr0

//PAGEBREAK!
  # ロングジャンプを使用することにより、%csと%eipを再ロードして、32ビット
  # プロテクトモードへの移行を完了させる。セグメントディスクリプタは
  # 変更せずに設定されるので、マッピングは恒等マッピングのままである。
  ljmp    $(SEG_KCODE<<3), $start32

.code32  # アセンブラにここからは32ビットコードを生成するように伝える。
start32:
  # プロテクトモードのデータセグメントレジスタを設定する。
  movw    $(SEG_KDATA<<3), %ax    # 使用するデータセグメントセレクタ
  movw    %ax, %ds                # -> DS: データセグメント
  movw    %ax, %es                # -> ES: 拡張セグメント
  movw    %ax, %ss                # -> SS: スタックセグメント
  movw    $0, %ax                 # 未使用のセグメントは0クリア
  movw    %ax, %fs                # -> FS
  movw    %ax, %gs                # -> GS

  # スタックポインタをセットして、Cno関数を呼び出す。
  movl    $start, %esp
  call    bootmain

  # bootmainが復帰したら（しないはず）、Bochs下で実行している場合は
  # Bochsのブレークポイントを呼び出して、ループする。
  movw    $0x8a00, %ax            # 0x8a00 -> port 0x8a00
  movw    %ax, %dx
  outw    %ax, %dx
  movw    $0x8ae0, %ax            # 0x8ae0 -> port 0x8a00
  outw    %ax, %dx
spin:
  jmp     spin

# ブートストラップGDT
.p2align 2                                # 4バイト境界に揃える
gdt:
  SEG_NULLASM                             # ヌルセグメント
  SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff)   # コードセグメント
  SEG_ASM(STA_W, 0x0, 0xffffffff)         # データセグメント

gdtdesc:
  .word   (gdtdesc - gdt - 1)             # sizeof(gdt) - 1
  .long   gdt                             # gdtアドレス
