#include "defines.h"
#include "kozos.h"
#include "intr.h"
#include "interrupt.h"
#include "syscall.h"
#include "lib.h"

#define THREAD_NUM 6
#define THREAD_NAME_SIZE 15

/* スレッドコンテキスト */
typedef struct _kz_context {
    uint32 sp; /* スタックポインタ */
} kz_context;

/* タスクコントロールブロック(TCB) */
typedef struct _kz_thread {
    struct _kz_thread *next;
    char name[THREAD_NAME_SIZE + 1];    /* スレッド名 */
    char *stack;                        /* スタック */

    /* スレッドのスタートアップ */
    struct {
        kz_func_t func; /* スレッドのメイン関数 */
        int argc;       /* スレッドのメイン関数に渡す argc */
        char **argv;    /* スレッドのメイン関数に渡す argv */
    } init;

    /* システムコール用バッファ */
    struct {
        kz_syscall_type_t type;
        kz_syscall_param_t *param;
    } syscall;

    kz_context context;
} kz_thread;

/* スレッドのレディー・キュー */
static struct {
    kz_thread *head;
    kz_thread *tail;
} readque;

static kz_thread *current;                       /* カレントスレッド */
static kz_thread threads[THREAD_NUM];           /* タスクコントロールブロック */
static kz_handler_t handlers[SOFTVEC_TYPE_NUM]; /* 割り込みハンドラ */

void dispatch(kz_context *context);

/* カレントスレッド */
static int getcurrent(void) {
    if(current == NULL) return -1;

    /* カレントスレッドは必ず先頭にあるとするため、先頭から抜き出す */
    readyque.head = current->next;
    if(readyque.head == NULL) readyque.tail = NULL;

    current->next = NULL;

    return 0;
}

/* カレントスレッドをレディーキューにつなげる */
static int putcurrent(void) {
    if(current == NULL) return -1;

    /* レディーキューの末尾に接続する */
    if(readyque.tail) {
        readyque.tail->next = current;
    }
    else {
        readyque.head = current;
    }
    readyque.tail = current;

    return 0;
}

/* スレッドの終了 */
static void thread_end(void) {
    kz_exit();
}

/* スレッドのスタートアップ */
static void thread_init(kz_thread *thp) {
    /* スレッドのメイン関数を呼び出す */
    thp->init.func(thp->init.argc, thp->init.argv);
    thread_end();
}

/* システムコールの処理(kz_run();の呼び出し) */
static kz_thread_id_t thread_run(kz_func_t func, char *name
    int stacksize, int argc, char *argv[ ]) {

    int i;
    kz_thread *thp;
    uint32 *sp;
    extern char userstack;  /*リンカスクリプトで定義されたスタック領域 */
    static char *thread_stack = &userstack;

    /* 空いているタスクコントロールブロックを検索 */
    for(i = 0; i < THREAD_NUM; i++) {
        thp = &thread[i];
        if(!thp->init.func)break;   /* 見つかった */
    }
    if(i == THREAD_NUM)return -1;   /* 見つからなかった */

    memset(thp, 0, sizeof(*thp));

    /* タスクコントロールブロック */
    strcpy(thp->name, name);
    thp->next = NULL;

    thp->init.func = func;
    thp->init.argc = argc;
    thp->init.argv = argv;

    /* スタック領域を獲得 */
    memset(thread_stack, 0, stacksize);
    thread_stack += stacksize;

    thp->stack = thread_stack;  /* スタックを設定 */

    /* スタックの初期化 */
    sp = (uint32 *)thp->stack;
    *(--sp) = (uint32)thread_end;

    /* プログラムカウンタの設定 */
    *(--sp) = (uint32)thread_init;

    *(--sp) = 0;    /* ER6 */
    *(--sp) = 0;    /* ER5 */
    *(--sp) = 0;    /* ER4 */
    *(--sp) = 0;    /* ER3 */
    *(--sp) = (uint32)argv;    /* ER2 */
    *(--sp) = (uint32)argc;    /* ER1 */

    /* スレッドのスタートアップ(thread_init())に渡す引数 */
    *(--sp) = (uint32)thp;  /* ER0 */

    /* スレッドのコンテキストを設定 */
    thp->context.sp = (uint32)sp;

    /* システムコールを呼び出したスレッドをレディーキューに戻す */
    putcurrent();

    /* 新規作成したスレッドをレディーキューに接続する */
    current = thp;
    putcurrent();

    return (kz_thread_id_t)current;
}

/* システムコールの処理(kz_exit():スレッドの終了) */
static int thread_exit(void) {
    puts(current->name);
    puts("EXIT.\n");
    memset(current, 0, sizeof(*current));

    return 0;
}

/* 割り込みハンドラの登録 */
static int setintr(softvec_type_t type, kz_handler_t handler) {
    static void thread_intr(softvec_type_t, unsigned long sp);

    /* 割り込みを受け付けるために、ソフトウェア割り込みベクタに
     * OSの割り込み処理の入り口となる関数を登録する
     */
    softvec_setintr(type, thread_intr);

    handlers[type] = handler;   /* OS側から呼び出す割り込みハンドラを登録 */

    return 0;
}

static void call_functions(kz_syscall_type_t type, kz_syscall_param_t *p) {
    /* システムコールの実行中にcurrentに書き換わるため注意 */
    switch(type) {
    case KZ_SYSCALL_TYPE_RUN:   /* kz_run() */
        p->un.run.ret = thread_run(p->un.run.func, p->un.run.name,
            p->un.run.stacksize,
            p->un.run.argc, p->un.run.argv);
        break;

    case KZ_SYSCALL_TYPE_EXIT:  /* kz_exit() */
        /* TCBが削除されるので、戻り値を書き込まない */
        thread_exit();
        break;

    default:
        break;
    }
}

/* システムコールの処理 */
static void syscall_proc(kz_syscall_type_t type, kz_syscall_param_t *p) {
    /*
     * システムコールを呼び出したスレッドをレディーキューから
     * 外した状態で処理関数を呼び出す.このためシステムコールを
     * 呼び出したスレッドをそのまま動作継続させたい場合は、
     * 処理関数の内部でputcurrent()を行う必要がある.
     */

    getcurrent();
    call_functions(type, p);
}

/* スレッドのスケジューリング */
static void schedule(void) {
    if(!readyque.head) kz_sysdown();    /* 見つからない */

    current = readyque.head;            /* カレントスレッドに設定 */
}

/* システムコールの呼び出し */
static void syscall_intr(void) {
    syscall_proc(current->syscall.type, current->syscall.param);
}

/* ソフトウェアエラーの発生 */
static void softerr_intr(void) {
    puts(current->name);
    puts("DOWN.\n");
    getcurrent();   /* レディーキューから外す */
    thread_exit();  /* スレッド終了 */
}

/* 割り込み処理の入口関数 */
static void thread_intr(softvec_type_t type, unsigned long sp) {
    /* カレントスレッドのコンテキストを保存する */
    current->context.sp = sp;

    /* 割り込みごとの処理を実行する.
     * SOFTVEC_TYPE_SYSCALL,SOFT_TYPE_SOFTERRの場合は
     * syscall_intr(),softerr_intr()がハンドラに登録されているので、それらが実行される
     */
    if(handlers[type]) handlers[type]();

    schedule();

    /* スレッドのディスパッチ
     * (dispathc()関数本体はstartup.sにあり、アセンブラで記述されている)
     */
    dispatch(&current->context);

    /* ここには帰って来ない */
}

/* 初期スレッドの起動 */
void kz_start(kz_func_t func, char *name, int stacksize, int argc, char *argv[ ]) {
    /* 以降呼び出すスレッド関連のライブラリ関数の内部で、currentを
     * 見ている場合があるので、currentをNULLに初期化しておく.
     */
    current = NULL;

    readyque.head = readyque.tail = NULL;
    memset(threads, 0, sizeof(threads));
    memset(handlers, 0, sizeof(handlers));

    /* 割り込みハンドラの登録 */
    setintr(SOFTVEC_TYPE_SYSCALL, syscall_intr);    /* システムコール */
    setintr(SOFTVEC_TYPE_SOFTERR, softerr_intr);    /* ダウン要因発生 */

    /* システムコール発行不可のため直接関数を呼び出してスレッド作成する */
    current = (kz_thread *)thread_run(func, name, stacksize, argc, argv);

    /* 最初のスレッドを起動 */
    dispatch(&current->context);

    /* ここには帰って来ない */
}

/* 致命的エラーが発生した場合に、この関数を呼ぶ */
void kz_sysdown(void) {
    puts("system error.\n");
    while(1);
}

/* システムコール呼び出し用ライブラリ関数 */
void kz_syscall(kz_syscall_type_t, kz_syscall_param_t *param) {
    current->syscall.type = type;
    current->syscall.param = param;

    asm volatile("trapa #0");   /* トラップ命令により割り込みを発生させる */
}