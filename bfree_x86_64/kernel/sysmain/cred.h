#ifndef BFREE_CRED_H
#define BFREE_CRED_H

unsigned int bfree_getuid(void);
unsigned int bfree_geteuid(void);
unsigned int bfree_getgid(void);
unsigned int bfree_getegid(void);
int bfree_setuid(unsigned int uid);
int bfree_setgid(unsigned int gid);
int bfree_setreuid(unsigned int ruid, unsigned int euid);
int bfree_setregid(unsigned int rgid, unsigned int egid);
int bfree_setresuid(unsigned int ruid, unsigned int euid, unsigned int suid);
int bfree_getresuid(unsigned int *ruid, unsigned int *euid, unsigned int *suid);
int bfree_setresgid(unsigned int rgid, unsigned int egid, unsigned int sgid);
int bfree_getresgid(unsigned int *rgid, unsigned int *egid, unsigned int *sgid);
int bfree_getgroups(int size, unsigned int *list);
int bfree_setgroups(int size, const unsigned int *list);
int bfree_capget(void *hdrp, void *datap);
int bfree_capset(void *hdrp, const void *datap);

#endif
