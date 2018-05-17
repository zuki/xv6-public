# コンテキストスイッチ
#
#   void swtch(struct context **old, struct context *new);
#
# カレントレジスタをスタック上に保存し、context構造体を作成して、
# そのアドレスを*oldに保存する。
# スタックをnewに切り替え、前回保存したレジスタをポップする。

.globl swtch
swtch:
  movl 4(%esp), %eax       # old
  movl 8(%esp), %edx       # new

  # 旧callee-saveレジスタを保存
  pushl %ebp
  pushl %ebx
  pushl %esi
  pushl %edi

  # スタックを切り替える
  movl %esp, (%eax)
  movl %edx, %esp

  # 新callee-saveレジスタをロード
  popl %edi
  popl %esi
  popl %ebx
  popl %ebp
  ret
