#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* 0 until the guest event loop. QQmlThread::isThisThread() stays stock. */
void bfree_guest_set_typeloader_main_ok(int on);
int bfree_guest_typeloader_main_ok(void);

#ifdef __cplusplus
}
#endif
