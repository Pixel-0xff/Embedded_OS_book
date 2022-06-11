#include "defines.h"
#include "serial.h"
#include "xmodem.h"
#include "elf.h"
#include "lib.h"

static int init(void) {
	/* リンカスクリプトで定義したシンボル */
	extern int erodata, data_start, edata, bss_start, ebss;

	/* データ領域とBSS領域を初期化 */
	memcpy(&data_start, &erodata, (long)&edata - (long)&data_start);
	memset(&bss_start, 0, (long)&ebss - (long)&bss_start);

	/* ソフトウェア・割り込みベクタを初期化する */
	softvec_init();

	/* シリアルの初期化 */
	serial_init(SERIAL_DEFAULT_DEVICE);

	return 0;
}

/* メモリの16進数ダンプ出力 */
static int dump(char *buf, long size) {
	long i;

	if(size < 0) {
		puts("no data.\n");
		return -1;
	}

	for(i = 0; i < size; i++) {
		putxval(buf[i], 2);

		if((i & 0xf) == 15) {
			puts("\n");
		}
		else {
			if((i & 0xf) == 7) puts(" ");
			puts(" ");
		}
	}
	puts("\n");

	return 0;
}

static void wait() {
	volatile long i;
	for(i = 0; i < 300000; i++);
}

int main(void) {

	static char buf[16];
	static long size = -1;
	static unsigned char *loadbuf = NULL;
	extern int buffer_start;		/* リンカスクリプトで定義されたバッファ */
	char *entry_point;
	void (*f)(void);

	init();

	puts("kzload (kozos boot loader) started.\n");

	while(1) {
		puts("kzload> ");	/* プロンプト文字の表示 */
		gets(buf);			/* シリアルからのコマンドを受信 */

		if(!strcmp(buf, "load")) {		/* XMODEMでのファイルのダウンロード */
			loadbuf = (char *)(&buffer_start);
			size = xmodem_recv(loadbuf);
			wait();
			if(size < 0) {
				puts("\nXMODEM receive error!.\n");
			}
			else {
				puts("\nXMODEM receive succeeded.\n");
				continue;
			}
		}
		else if(!strcmp(buf, "dump")) {	/* メモリの16進ダンプ出力 */
			puts("size: ");
			putxval(size, 0);
			puts("\n");
			dump(loadbuf, size);
		}
		else if(!strcmp(buf, "run")) {			/* ELF形式ファイルの実行 */
			entry_point = elf_load(loadbuf);	/* メモリ上にロード */
			if(!entry_point) {
				puts("run error!\n");
			}
			else {
				puts("starting from entry point: ");
				putxval((unsigned long)entry_point, 0);
				puts("\n");
				f = (void(*)(void))entry_point;
				f();	/* ロードしたプログラムに処理を渡す */
			}
		}
		else {
			puts("unknown.\n");
		}

		return 0;
	}
}