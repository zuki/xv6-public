# コンテキストスイッチ
#
#   void swtch(struct context **old, struct context *new);
#
# カレントレジスタコンテキストをoldに保存し、
# fromからレジスタコンテキストをロードする。

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
