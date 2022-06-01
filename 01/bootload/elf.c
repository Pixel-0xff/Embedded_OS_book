#include "defines.h"
#include "elf.h"
#include "lib.h"

struct elf_header {
    struct {    /* 16バイトの識別情報 */
        unsigned char magic[4];     /* マジックナンバー */
        unsigned char class;        /* 32/64ビットの区別 */
        unsigned char format;       /* エンディアン情報 */
        unsigned char version;      /* ELFフォーマットのバージョン */
        unsigned char abi;          /* OSの種別 */
        unsigned char abi_version;  /* OSのバージョン */
        unsigned char reserve[7];   /* 予約領域(未使用) */
    }id;

    short type;                     /* ファイルの種別 */
    short arch;                     /* CPUの種類 */
    long version;                   /* ELF形式のバージョン */
    long entry_point;               /* 実行開始アドレス */
    long program_header_offset;     /* プログラム・ヘッダ・テーブルの位置 */
    long section_header_offset;     /* セクション・ヘッダ・テーブルの位置 */
    long flags;                     /* 各種フラグ */
    short header_size;              /* ELFヘッダのサイズ */
    short program_header_size;      /* プログラム・ヘッダのサイズ */
    short program_header_num;       /* プログラム・ヘッダの個数 */
    short section_header_size;      /* セクション・ヘッダのサイズ */
    short section_header_num;       /* セクション・ヘッダの個数 */
    short section_name_index;       /* セクション名を格納するセクション */
};

struct elf_program_header {
    long type;          /* セグメントの種別 */
    long offset;        /* ファイル中の位置 */
    long virtual_addr;  /* 論理アドレス(VA) */
    long physical_addr; /* 物理アドレス(PA) */
    long file_size;     /* ファイル中のサイズ */
    long memory_size;   /* メモリ上でのサイズ */
    long flags;         /* 各種フラグ */
    long align;         /* アライメント */
};

/* ELFヘッダのチェック */
static int elf_check(struct elf_header *header) {
    /* マジックナンバーのチェック */
    if(memcmp(header->id.magic, "\x7f" "ELF", 4)) return -1;

    /* 各種パラメータのチェック */
    if(header->id.class != 1) return -1;
    if(header->id.format != 2) return -1;
    if(header->id.version != 1) return -1;
    if(header->type != 2) return -1;
    if(header->version != 1) return -1;

    /* アーキテクチャがH8であることのチェック */
    if((header->arch != 46) && (header->arch != 47)) return -1;

    return 0;
}

/* セグメント単位でロード */
static int elf_load_program(struct elf_header *header) {
    int i;
    struct elf_program_header *phdr;

    for(i = 0; i < header->program_header_num; i++) {  /* セグメント単位でループする */
        /* プログラム・ヘッダの取得 */
        phdr = (struct elf_program_header *)
            ((char *)header + header->program_header_offset +
                header->program_header_size * i);

        if(phdr->type != 1)continue; /* ロード可能なセグメントか? */

        /* 実験用に、実際にロードせずにセグメント情報を表示する */
        putxval(phdr->offset, 6); puts(" ");
        putxval(phdr->virtual_addr, 8); puts(" ");
        putxval(phdr->physical_addr, 8); puts(" ");
        putxval(phdr->file_size, 5); puts(" ");
        putxval(phdr->memory_size, 5); puts(" ");
        putxval(phdr->flags, 2); puts(" ");
        putxval(phdr->align, 2); puts(" ");
    }

    return 0;
}

/* ELF形式を解析 */
int elf_load(char *buf) {
    struct elf_header *header = (struct elf_header *)buf;

    /* ELFヘッダのチェック */
    if(elf_check(header) < 0) return -1;

    /* セグメント単位でのロード */
    if(elf_load_program(header) < 0)return -1;

    return 0;
}