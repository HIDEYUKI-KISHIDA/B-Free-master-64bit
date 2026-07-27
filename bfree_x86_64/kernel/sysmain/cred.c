#include "cred.h"

#include <errno.h>
#include <string.h>

static unsigned int bfree_uid = 0;
static unsigned int bfree_gid = 0;
static unsigned int bfree_euid = 0;
static unsigned int bfree_egid = 0;
static unsigned int bfree_suid = 0;
static unsigned int bfree_sgid = 0;
static unsigned int bfree_groups[32];
static int bfree_ngroups;
static unsigned int bfree_caps[2];

unsigned int bfree_getuid(void) { return bfree_uid; }
unsigned int bfree_geteuid(void) { return bfree_euid; }
unsigned int bfree_getgid(void) { return bfree_gid; }
unsigned int bfree_getegid(void) { return bfree_egid; }

int bfree_setuid(unsigned int uid)
{
    if (bfree_euid != 0)
        return -EPERM;
    bfree_uid = bfree_euid = bfree_suid = uid;
    return 0;
}

int bfree_setgid(unsigned int gid)
{
    if (bfree_euid != 0)
        return -EPERM;
    bfree_gid = bfree_egid = bfree_sgid = gid;
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

int bfree_setresuid(unsigned int ruid, unsigned int euid, unsigned int suid)
{
    if (bfree_euid != 0)
        return -EPERM;
    if (ruid != (unsigned int)-1)
        bfree_uid = ruid;
    if (euid != (unsigned int)-1)
        bfree_euid = euid;
    if (suid != (unsigned int)-1)
        bfree_suid = suid;
    return 0;
}

int bfree_getresuid(unsigned int *ruid, unsigned int *euid, unsigned int *suid)
{
    if (ruid)
        *ruid = bfree_uid;
    if (euid)
        *euid = bfree_euid;
    if (suid)
        *suid = bfree_suid;
    return 0;
}

int bfree_setresgid(unsigned int rgid, unsigned int egid, unsigned int sgid)
{
    if (bfree_euid != 0)
        return -EPERM;
    if (rgid != (unsigned int)-1)
        bfree_gid = rgid;
    if (egid != (unsigned int)-1)
        bfree_egid = egid;
    if (sgid != (unsigned int)-1)
        bfree_sgid = sgid;
    return 0;
}

int bfree_getresgid(unsigned int *rgid, unsigned int *egid, unsigned int *sgid)
{
    if (rgid)
        *rgid = bfree_gid;
    if (egid)
        *egid = bfree_egid;
    if (sgid)
        *sgid = bfree_sgid;
    return 0;
}

int bfree_getgroups(int size, unsigned int *list)
{
    int i;

    if (size < 0)
        return -EINVAL;
    if (size == 0)
        return bfree_ngroups;
    if (size < bfree_ngroups)
        return -EINVAL;
    for (i = 0; i < bfree_ngroups; i++)
        list[i] = bfree_groups[i];
    return bfree_ngroups;
}

int bfree_setgroups(int size, const unsigned int *list)
{
    int i;

    if (bfree_euid != 0)
        return -EPERM;
    if (size < 0 || size > 32)
        return -EINVAL;
    bfree_ngroups = size;
    for (i = 0; i < size; i++)
        bfree_groups[i] = list[i];
    return 0;
}

int bfree_capget(void *hdrp, void *datap)
{
    unsigned int *data = datap;

    (void)hdrp;
    if (data != NULL) {
        data[0] = bfree_caps[0];
        data[1] = bfree_caps[1];
    }
    return 0;
}

int bfree_capset(void *hdrp, const void *datap)
{
    const unsigned int *data = datap;

    (void)hdrp;
    if (bfree_euid != 0)
        return -EPERM;
    if (data != NULL) {
        bfree_caps[0] = data[0];
        bfree_caps[1] = data[1];
    }
    return 0;
}
