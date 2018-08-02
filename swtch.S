# コンテキストスイッチ
#   void swtch(struct context **old, struct context *new);
#
# 現在のレジスタをスタックに保存し、struct contextを
# 作成してそのアドレスを*oldに保存する。スタックをnewに
# 切り替え、以前に保存していたレジスタをポップする。

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
