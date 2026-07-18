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

#endif
