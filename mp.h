// マルチプロセッサ仕様書第1巻を参照 [14]

struct mp {             // 浮動ポインタ
  uchar signature[4];           // "_MP_"
  void *physaddr;               // MP構成テーブルの物理アドレス
  uchar length;                 // 1
  uchar specrev;                // [14]
  uchar checksum;               // すべてのバイトの合計は0でなければならない
  uchar type;                   // MPシステム構成種別
  uchar imcrp;
  uchar reserved[3];
};

struct mpconf {         // 構成テーブルヘッダ
  uchar signature[4];           // "PCMP"
  ushort length;                // 総テーブル長
  uchar version;                // [14]
  uchar checksum;               // すべてのバイトの合計は0でなければならない
  uchar product[20];            // 製品ID
  uint *oemtable;               // OEM テーブルポインタ
  ushort oemlength;             // OEM テーブル長
  ushort entry;                 // エントリカウント
  uint *lapicaddr;              // ローカルAPICのアドレス
  ushort xlength;               // 拡張テーブル長
  uchar xchecksum;              // 拡張テーブルチェックサム
  uchar reserved;
};

struct mpproc {         // プロセッサテーブルエントリ
  uchar type;                   // エントリ種別 (0)
  uchar apicid;                 // ローカルAPIC ID
  uchar version;                // ローカルAPIC バージョン
  uchar flags;                  // CPU フラグ
    #define MPBOOT 0x02           // このプロセッサはブートストラッププロセッサ
  uchar signature[4];           // CPU シグネチャ
  uint feature;                 // CPUID命令のfeatureフラグ
  uchar reserved[8];
};

struct mpioapic {       // I/O APIC テーブルエントリ
  uchar type;                   // エントリ種別 (2)
  uchar apicno;                 // I/O APIC ID
  uchar version;                // I/O APIC バージョン
  uchar flags;                  // I/O APIC フラグ
  uint *addr;                  // I/O APIC アドレス
};

// テーブルエントリ種別
#define MPPROC    0x00  // プロセッサごとに1つ
#define MPBUS     0x01  // バスごとに1つ
#define MPIOAPIC  0x02  // I/O APICごとに1つ
#define MPIOINTR  0x03  // バス割り込みソースごとに1つ
#define MPLINTR   0x04  // システム割り込みソースごとに1つ

//PAGEBREAK!
// Blank page.
