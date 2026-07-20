# AF_UNIX + early coop base

Canonical early layer already on disk:

- `tools/_unix_coop_block.c` — AF_UNIX sockets + early `bfree_coop_yield_*` (no CR3 flip, no as_copy heap park)
- Applied by `tools/_patch_unix_coop.py`

Recovered `as_copy.c` / `inet.c` supersede pieces of that block (yields gain `bfree_coop_as_switch_to` + heap park; socket() gains AF_INET).
