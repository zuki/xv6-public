// ELF実行ファイルのフォーマット

#define ELF_MAGIC 0x464C457FU  // リトルエンディアンで"\x7FELF"

// ファイルヘッダ
struct elfhdr {
  uint magic;  // ELF_MAGICに等しくなければならない
  uchar elf[12];
  ushort type;
  ushort machine;
  uint version;
  uint entry;
  uint phoff;
  uint shoff;
  uint flags;
  ushort ehsize;
  ushort phentsize;
  ushort phnum;
  ushort shentsize;
  ushort shnum;
  ushort shstrndx;
};

// プログラムセクションヘッダ
struct proghdr {
  uint type;
  uint off;
  uint vaddr;
  uint paddr;
  uint filesz;
  uint memsz;
  uint flags;
  uint align;
};

// プログラムヘッダのtype値
#define ELF_PROG_LOAD           1

// プログラムヘッダのflags値のフラグビット
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
