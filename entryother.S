#include "asm.h"
#include "memlayout.h"
#include "mmu.h"

# 非ブートCPU（"AP"）はブートCPUのSTARTUP IPIに反応して開始される。
# "Multi-Processor Specification"のB.4.2節によると、
# APは、CS:IPにXY00:0000をセットしてリアルモードで開始される。
# ここで、XYはSTARTUPで送信される8ビットの値である。
# したがって、このコードは4096バイト境界から開始しなければならない。
#
# このコードはDSに0をセットするので、
# 低位2^16(64K)バイトのアドレスにいなければならない。
#
# (main.cの）startothersは一度に1つSTARTUPを送信する。
# startothersはこのコード(start)を0x7000にコピーし、
# 新規にコアごとに割り当てられるスタックのアドレスを(start-4)に、
# ジャンプ先のアドレス(mpenter)を(start-8)に、
# entrypgdirの物理アドレスを(start-12)に置く。
#
# このコードはbootasm.Sとentry.Sの要素を結合する

.code16
.globl start
start:
  cli

  # データセグメントレジスタDS, ES, SSを0クリアする。
  xorw    %ax,%ax
  movw    %ax,%ds
  movw    %ax,%es
  movw    %ax,%ss

  # リアルモードからプロテクトモードに切り替える。切り替えの際に
  # 実効メモリマップが変わらないように、仮想アドレスを
  # 直接物理アドレスにマップするブートストラップGDTを使用する。e
  lgdt    gdtdesc
  movl    %cr0, %eax
  orl     $CR0_PE, %eax
  movl    %eax, %cr0

  # ロングジャンプして%csと%eipを再ロードし、32ビットプロテクト
  # モードへの移行を完了させる。セグメントディスクリプタは移行しないので
  # マッピングは依然として恒等マッピングである。
  ljmpl    $(SEG_KCODE<<3), $(start32)

//PAGEBREAK!
.code32  # ここからは32ビットコードを生成するようアセンブラに伝える
start32:
  # プロテクトモードのデータセグメントレジスタを設定する
  movw    $(SEG_KDATA<<3), %ax    # データセグメントセレクタ(0x10)
  movw    %ax, %ds                # -> DS: データセグメント
  movw    %ax, %es                # -> ES: エクストラセグメント
  movw    %ax, %ss                # -> SS: スタックセグメント
  movw    $0, %ax                 # 未使用のセグメントには0をセット
  movw    %ax, %fs                # -> FS
  movw    %ax, %gs                # -> GS

  # ページ単位が4MBになるようにページサイズ拡張をセットする
  movl    %cr4, %eax
  orl     $(CR4_PSE), %eax
  movl    %eax, %cr4
  # 最初のページテーブルとしてentrypgdirを使用する
  movl    (start-12), %eax
  movl    %eax, %cr3
  # ページングを有効にする
  movl    %cr0, %eax
  orl     $(CR0_PE|CR0_PG|CR0_WP), %eax
  movl    %eax, %cr0

  # startothers()により割り当てられたスタックに切り替える
  movl    (start-4), %esp
  # mpenter()を呼び出す
  call	 *(start-8)

  movw    $0x8a00, %ax
  movw    %ax, %dx
  outw    %ax, %dx
  movw    $0x8ae0, %ax
  outw    %ax, %dx
spin:
  jmp     spin

.p2align 2
gdt:
  SEG_NULLASM
  SEG_ASM(STA_X|STA_R, 0, 0xffffffff)
  SEG_ASM(STA_W, 0, 0xffffffff)


gdtdesc:
  .word   (gdtdesc - gdt - 1)
  .long   gdt
