/* JOSカーネルのための簡単なリンカスクリプト。
   文法に関してはGNU ldの'info'マニュアル ("info ld") を参照のこと。 */

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
	/* このアドレスにカーネルをリンクする: "." は関連とアドレスを意味する */
        /* KERNLINKと等しくなければならない */
	. = 0x80100000;

	.text : AT(0x100000) {
		*(.text .stub .text.* .gnu.linkonce.t.*)
	}

	PROVIDE(etext = .);	/* 'etext'シンボルをこの値であると定義する */

	.rodata : {
		*(.rodata .rodata.* .gnu.linkonce.r.*)
	}

	/* カーネルメモリにデバッグ情報を含める */
	.stab : {
		PROVIDE(__STAB_BEGIN__ = .);
		*(.stab);
		PROVIDE(__STAB_END__ = .);
		BYTE(0)		/* このセクションにスペースを割り当てるよう
				   リンカに強制する */
	}

	.stabstr : {
		PROVIDE(__STABSTR_BEGIN__ = .);
		*(.stabstr);
		PROVIDE(__STABSTR_END__ = .);
		BYTE(0)		/* このセクションにスペースを割り当てるよう
				   リンカに強制する */
	}

	/* データセグメントが次のページにくるようにアドレスを調整する */
	. = ALIGN(0x1000);

	/* 規約上、Unixのリンカはtext, data, bssの終端アドレスに疑似
   * シンボルetext, edata, endを提供している。
	 * カーネルマッピングにおいて、データセクションの先頭アドレスが
	 * 必要であるが、規約にはそれにあたるシンボルが存在しない。
	 * なぜなら、規約はtextとdataの間に読み込み専用のrodataセクションが
	 * 設けられる前にできたものだからである。 */
	PROVIDE(data = .);

	/* データセグメント */
	.data : {
		*(.data)
	}

	PROVIDE(edata = .);

	.bss : {
		*(.bss)
	}

	PROVIDE(end = .);

	/DISCARD/ : {
		*(.eh_frame .note.GNU-stack)
	}
}
