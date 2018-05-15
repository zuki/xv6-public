//
// x86セグメントを作成するためのアセンブラマクロ
// （訳注: セグメントディスクリプタ(32 bit)を作成する）

#define SEG_NULLASM                                             \
        .word 0, 0;                                             \
        .byte 0, 0, 0, 0

// 0xC0はlimitが4096バイト単位であり、 （実行可能セグメントは）
// 32ビットモードであることを意味する(G & D bitをオン )
#define SEG_ASM(type,base,lim)                                  \
        .word (((lim) >> 12) & 0xffff), ((base) & 0xffff);      \
        .byte (((base) >> 16) & 0xff), (0x90 | (type)),         \
                (0xC0 | (((lim) >> 28) & 0xf)), (((base) >> 24) & 0xff)

#define STA_X     0x8       // 実行可能セグメント
#define STA_E     0x4       // 拡大縮小（非実行可能セグメント）
#define STA_C     0x4       // コンフォーミングコードセグメント（実行可能のみ）
#define STA_W     0x2       // 書き込み可能（非実行可能セグメント）
#define STA_R     0x2       // 読み取り可能（実行可能セグメント）
#define STA_A     0x1       // アクセス
