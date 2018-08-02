# xv6のカーネルはこのファイルから実行を開始する。このファイルはカーネルの
# Cコードにリンクされているので、main()などのカーネルシンボルを参照できる。
# ブートブロック(bootasm.Sとbootmain.c)はこのファイルのentryにジャンプする。

# マルチブートヘッダ。GUN Grubなどのマルチブート・ブートローダ用。
# http://www.gnu.org/software/grub/manual/multiboot/multiboot.html
#
# GRUB 2を使って、Linuxファイルシステムに格納したファイルからxv6を
# ブートできる。kernelまたはkernelmemfsを/bootにコピーし、
# 以下をメニューエントリに追加する。
#
# menuentry "xv6" {
# 	insmod ext2
# 	set root='(hd0,msdos1)'
# 	set kernel='/boot/kernel'
# 	echo "Loading ${kernel}..."
# 	multiboot ${kernel} ${kernel}
# 	boot
# }

#include "asm.h"
#include "memlayout.h"
#include "mmu.h"
#include "param.h"

# マルチブートヘッダ。マルチブートローダへ向けるためのデータ。
.p2align 2
.text
.globl multiboot_header
multiboot_header:
  #define magic 0x1badb002
  #define flags 0
  .long magic
  .long flags
  .long (-magic-flags)

# 規約により、_startシンボルにはELFエントリポイントを指定する。
# まだ仮想メモリを設定していないので、エントリポイントは
# 'entry'の物理アドレスである。
.globl _start
_start = V2P_WO(entry)

# ブートプロセッサのxv6に入る。ページングは無効。
.globl entry
entry:
  # ページ単位が4MBになるようにページサイズ拡張をセットする
  movl    %cr4, %eax
  orl     $(CR4_PSE), %eax
  movl    %eax, %cr4
  # ページディレクトリをセットする
  movl    $(V2P_WO(entrypgdir)), %eax
  movl    %eax, %cr3
  # ページングを有効にする
  movl    %cr0, %eax
  orl     $(CR0_PG|CR0_WP), %eax
  movl    %eax, %cr0

  # スタックポインタを設定する
  movl $(stack + KSTACKSIZE), %esp

  # main()にジャンプして、高位アドレスの実行コードにスイッチする。
  # ここでは間接呼び出しが必要である。
  # なぜなら、直接ジャンブにすると
  # アセンブラがPC相対の命令を生成するからである。
  mov $main, %eax
  jmp *%eax

.comm stack, KSTACKSIZE
