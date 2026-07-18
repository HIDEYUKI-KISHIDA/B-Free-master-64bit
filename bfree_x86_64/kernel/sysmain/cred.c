#include "cred.h"

#include <errno.h>

static unsigned int bfree_uid = 0;
static unsigned int bfree_gid = 0;
static unsigned int bfree_euid = 0;
static unsigned int bfree_egid = 0;

unsigned int bfree_getuid(void) { return bfree_uid; }
unsigned int bfree_geteuid(void) { return bfree_euid; }
unsigned int bfree_getgid(void) { return bfree_gid; }
unsigned int bfree_getegid(void) { return bfree_egid; }

int bfree_setuid(unsigned int uid)
{
    if (bfree_euid != 0)
        return -EPERM;
    bfree_uid = bfree_euid = uid;
    return 0;
}

int bfree_setgid(unsigned int gid)
{
    if (bfree_euid != 0)
        return -EPERM;
    bfree_gid = bfree_egid = gid;
    return 0;
}

int bfree_setreuid(unsigned int ruid, unsigned int euid)
{
    if (bfree_euid != 0)
        return -EPERM;
    if (ruid != (unsigned int)-1)
        bfree_uid = ruid;
    if (euid != (unsigned int)-1)
        bfree_euid = euid;
    return 0;
}

int bfree_setregid(unsigned int rgid, unsigned int egid)
{
    if (bfree_euid != 0)
        return -EPERM;
    if (rgid != (unsigned int)-1)
        bfree_gid = rgid;
    if (egid != (unsigned int)-1)
        bfree_egid = egid;
    return 0;
}
