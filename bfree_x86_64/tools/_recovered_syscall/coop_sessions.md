# coop sessions — NOT recovered

`syscall.o` defines dual-live helpers such as:

- `bfree_coop_sessions_init`
- `bfree_coop_session_alloc` / `_free`
- `bfree_coop_session_find_pid` / `_focus_pid`
- `bfree_coop_session_load_globals` / `_park_globals`

Agent transcripts (97982f27) discuss H02 dual-live and contain **call sites**
(`g_coop_session`, `bfree_coop_session_park_globals`, init in boot), but **no complete
`static` function bodies** for `bfree_coop_session_alloc` (etc.) were found in
StrReplace/Write payloads.

Likely lived only in direct editor state that was wiped with `syscall.c`.
Reconstruct from `process.h` / design checklists in subagent `8019d23b` / `fa1e612c`,
or reverse from `syscall.o` if needed.
