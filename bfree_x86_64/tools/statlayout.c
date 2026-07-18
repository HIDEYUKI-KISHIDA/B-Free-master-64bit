#include <stdio.h>
#include <sys/stat.h>
#include <stddef.h>
int main(void) {
  printf("sizeof(struct stat)=%zu\n", sizeof(struct stat));
  printf("st_dev=%zu\n", offsetof(struct stat, st_dev));
  printf("st_ino=%zu\n", offsetof(struct stat, st_ino));
  printf("st_nlink=%zu\n", offsetof(struct stat, st_nlink));
  printf("st_mode=%zu\n", offsetof(struct stat, st_mode));
  printf("st_uid=%zu\n", offsetof(struct stat, st_uid));
  printf("st_gid=%zu\n", offsetof(struct stat, st_gid));
  printf("st_rdev=%zu\n", offsetof(struct stat, st_rdev));
  printf("st_size=%zu\n", offsetof(struct stat, st_size));
  printf("st_blksize=%zu\n", offsetof(struct stat, st_blksize));
  printf("st_blocks=%zu\n", offsetof(struct stat, st_blocks));
  return 0;
}
