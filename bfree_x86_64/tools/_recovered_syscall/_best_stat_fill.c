static long bfree_linux_stat_fill(long statbuf, uint32_t mode, int64_t size)
{
    bfree_linux_stat_t *st;

    if (statbuf == 0 || !bfree_user_buf_mapped((uint64_t)(uintptr_t)statbuf, sizeof(bfree_linux_stat_t))) {
        return -14;
    }
    st = (bfree_linux_stat_t *)(uintptr_t)statbuf;
    memset(st, 0, 144U);
    st->st_dev = 1ULL;
    st->st_ino = 1ULL;
    st->st_mode = mode;
    st->st_nlink = 1U;
    st->st_uid = 0U;
    st->st_gid = 0U;
    st->st_blksize = 4096;
    st->st_size = size;
    st->st_blocks = (size + 511) / 512;
    return 0;
}
