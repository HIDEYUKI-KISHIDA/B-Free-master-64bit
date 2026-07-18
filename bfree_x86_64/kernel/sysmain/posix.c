// posix.c - POSIX互換層本実装案（仕様書v2.4準拠）
//
// * PoCのラッパAPI・エラー通知枠組みを流用
// * システムコールラッパ本体・ABI/アラインメント・ENOSYS返却を明示



#define EP_NOSYS (-38) // Function not implemented

// selectラッパ本体
int posix_select(int nfds, void* readfds, void* writefds, void* exceptfds, void* timeout) {
    (void)nfds;
    (void)readfds;
    (void)writefds;
    (void)exceptfds;
    (void)timeout;
    // printf("[POSIX] select: nfds=%d\n", nfds); // カーネルでは標準Cライブラリ不可
    // TODO: カーネル内部でselect実装 or システムコール経由
    return EP_NOSYS;
}

// pollラッパ本体
int posix_poll(void* fds, unsigned long nfds, int timeout) {
    (void)fds;
    (void)nfds;
    (void)timeout;
    // printf("[POSIX] poll: nfds=%lu timeout=%d\n", nfds, timeout); // カーネルでは標準Cライブラリ不可
    // TODO: カーネル内部でpoll実装 or システムコール経由
    return EP_NOSYS;
}

// 未実装APIのENOSYS返却例
typedef int (*posix_stub_t)(void);
int posix_not_impl_stub(void) {
    return EP_NOSYS;
}
// 必要に応じて他APIも同様にラッパ・ENOSYS返却を追加
