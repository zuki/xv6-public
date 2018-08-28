	.text

/* Switch from current_thread to next_thread. Make next_thread
 * the current_thread, and set next_thread to 0.
 * Use eax as a temporary register; it is caller saved.
 */
	.globl thread_switch
thread_switch:
	/* current_threadが指し示す構造体に現在のスレッド状態を保存 */
	/* next_threadの状態を復元 */
	/* next_threadが指し示していたアドレスをcurrent_threadが指すようにする*/
	pushal
	movl current_thread, %eax
	movl %esp, (%eax)
	movl next_thread, %eax
	movl (%eax), %esp
	popal
	movl next_thread, %eax
	movl %eax, current_thread
	movl $0x0, next_thread
	ret				/* pop return address from stack */
