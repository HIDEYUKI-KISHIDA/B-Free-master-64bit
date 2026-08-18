/* C helper linked into qt_wl_hello.elf. Same vfile tiles + canned xdg wire
 * as the C p8test client. Not SCM_RIGHTS / not upstream qtwayland. */
#ifndef WL_STUB_CLIENT_H
#define WL_STUB_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

long wl_stub_flush_app(const unsigned char *app, unsigned w, unsigned h);

#ifdef __cplusplus
}
#endif

#endif
