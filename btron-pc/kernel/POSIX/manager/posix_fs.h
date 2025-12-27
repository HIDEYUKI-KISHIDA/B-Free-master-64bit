#ifndef MAX_MODULE_NAME
#define MAX_MODULE_NAME 64
#endif
/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/

/* posix_fs.h - POSIX �ե����륷���ƥ�
 *
 *
 * $Log: posix_fs.h,v $
 * Revision 1.1  2011/12/27 17:13:35  liu1
 * Initial Version.
 *
 * Revision 1.21  2000-06-23 09:18:12  naniwa
 * to support O_APPEND
 *
 * Revision 1.20  2000/06/01 08:46:49  naniwa
 * to implement getdents
 *
 * Revision 1.19  2000/05/06 03:52:25  naniwa
 * implement mkdir/rmdir, etc.
 *
 * Revision 1.18  1999/05/28 15:46:43  naniwa
 * sfs ver 1.1
 *
 * Revision 1.17  1999/03/24 04:52:08  monaka
 * Source file cleaning for avoid warnings.
 *
 * Revision 1.16  1997/10/24 13:58:39  night
 * �ؿ�������ɲá�
 *
 * Revision 1.15  1997/10/11 16:25:19  night
 * �ե������ write �����ν�����
 *
 * Revision 1.14  1997/09/09 13:50:32  night
 * POSIX �Υե�����ؤν񤭹��߽���(�����ƥॳ����ϡ�write) ���ɲá�
 * ����ޤǤϡ�EP_NOSUP ���֤��Ƥ�����
 *
 * Revision 1.13  1997/08/31 13:31:35  night
 * lseek �����ƥॳ�����Ѥ�������ɲá�
 *
 * Revision 1.12  1997/07/09 15:03:05  night
 * inode ��¤�Τ� i_dirty �Ȥ������Ǥ��ɲá�
 * �������Ǥϡ���¤�Τ��ѹ�����Ƥ��ꡢ���ĥǥ�������ȿ�Ǥ���Ƥ��ʤ�����
 * �򼨤���
 *
 * Revision 1.11  1997/07/04 15:07:39  night
 * �����ڥ����ե����� - �ǥХ����ɥ饤�Хݡ��Ȥ��б�ɽ�δ�Ϣ�������ɲá�
 * ���ե�������ɤ߹��߽����β�����
 *
 * Revision 1.10  1997/07/02 13:26:01  night
 * FS_MOUNTROOT �ޥ����ΰ���������
 *
 * Revision 1.9  1997/04/25 13:00:38  night
 * struct statfs ��¤�Τ�������ɲá�
 *
 * Revision 1.8  1997/03/25 13:34:53  night
 * ELF �����μ¹ԥե�����ؤ��б�
 *
 * Revision 1.7  1996/11/20  12:10:20  night
 * FILE_UNLINK �ޥ��� ���ɲá�
 *
 * Revision 1.6  1996/11/18  13:43:20  night
 * inode ������ coverfs �� coverfile ���ѹ���
 *
 * Revision 1.5  1996/11/14  13:16:54  night
 * �Ƽ�������ɲá�
 *
 * Revision 1.4  1996/11/12  11:31:42  night
 * �桼���ץ������λ��ˤϡ�POSIX �ޥ͡�����δؿ��� extern �����ͭ����
 * ���ʤ��褦�ˤ�����
 *
 * Revision 1.3  1996/11/08  11:04:01  night
 * O_RDONLY, FS_FMT_REG, FS_FMT_DIR, FS_FMT_DEV �ޥ������ɲá�
 *
 * Revision 1.2  1996/11/07  21:12:04  night
 * ʸ�������ɤ� EUC ���ѹ�������
 *
 * Revision 1.1  1996/11/07  12:49:19  night
 * �ǽ����Ͽ
 *
 *
 */

#ifndef __POSIX_FS_H__
#define __POSIX_FS_H__	1



#ifndef O_RDONLY
#define O_RDONLY	0x00
#define O_WRONLY	0x01
#define O_RDWR		0x02
#define O_CREAT		0x08
#define O_APPEND	0x10
#endif

#define FS_FMT_REG	(0010000)
#define FS_FMT_DIR	(0020000)
#define FS_FMT_DEV	(0040000)


#define SEEK_SET	0
#define SEEK_CUR	1
#define SEEK_END	2

#define X_BIT		1
#define W_BIT		2
#define R_BIT		4
#define SU_UID		0

/* file system types. */

#define FS_SFS		1

#ifdef KERNEL


#define FS_MOUNTROOT(fsp,device,rootfs,rootfile)	(fsp->fs_mountroot)(device, rootfs,rootfile)

#define FILE_OPEN

#define FILE_CLOSE(ip)	(ip->i_ops->i_close (ip))

#define FILE_SYNC(ip)	(ip->i_ops->i_sync (ip))

#define FILE_LOOKUP(ip,part,oflag,mode,acc,tmpip)	\
	(ip->i_ops->i_lookup)(ip,part,oflag,mode,acc,tmpip)

#define FILE_WRITE(ip,start,buf,length,rlength)	(ip->i_ops->i_write) (ip,start,buf,length,rlength)

#define FILE_READ(ip,start,buf,length,rlength)	(ip->i_ops->i_read) (ip,start,buf,length,rlength)

#define FILE_CREATE(ip,path,oflag,mode,acc,newip)	\
	(ip->i_ops->i_create)(ip,path,oflag,mode,acc,newip)

#define FILE_UNLINK(ip,path,acc)	\
	(ip->i_ops->i_unlink)(ip,path,acc)

#define DIR_CREATE(ip, path, mode, acc, newip) \
	(ip->i_ops->i_mkdir)(ip, path, mode, acc, newip)

#define DIR_UNLINK(ip,path,acc)\
	(ip->i_ops->i_rmdir)(ip, path, acc)

#define GET_DENTS(ip, caller, offset, buf, length, rsize, fsize) \
	(ip->i_ops->i_getdents)(ip, caller, offset, buf, length, rsize, fsize)

struct fsops
{
  W		(*fs_mount)();
  W		(*fs_mountroot)();
  W		(*fs_umount)();
  W		(*fs_statfs)();
  W		(*fs_syncfs)();
  W		(*fs_read_inode)();
  W		(*fs_write_inode)();
  W		(*fs_read_super)();
  W		(*fs_write_super)();
  W		(*fs_get_inode)();		/* open */
  W		(*fs_put_inode)();		/* close */
};


struct fs
{
  struct fs	*fs_prev;
  struct fs	*fs_next;
  W		fs_magicid;
  W		fs_typeid;
  W		fs_refcount;
  W		fs_rflag;
  struct fsops	*fs_ops;
  W		fs_lock;
  UW		fs_device;
  struct inode	*fs_ilist;		/* ������� inode �Υꥹ�� */
  W		fs_blksize;
  struct inode	*rootdir;

  W		fs_allblock;
  W		fs_freeblock;
  W		fs_usedblock;

  W	        fs_allinode;
  W		fs_freeinode;
  W		fs_usedinode;

  UW		fs_isearch;		/* �����ֹ�ʲ��� inode �ϻ����� */
  UW		fs_bsearch;		/* �����ֹ�ʲ��� block �ϻ����� */

  union
    {
      struct sfs_superblock	sfs_fs;
    } fs_private;
};



struct inode
{
  struct inode	*i_prev;
  struct inode	*i_next;
  struct fs	*i_fs;
  UW		i_device;
  UW		i_lock;
  struct iops	*i_ops;
  W		i_refcount;
  W		i_dirty;	/* ���� Inode ���ѹ�����Ƥ��ꡢ�ե��������ѹ���
				 * ȿ�Ǥ���Ƥ��ʤ�
				 */
  struct inode	*coverfile;	/* �⤷��*����* �� �ޥ���ȥݥ���Ȥλ��ˤϡ� */
				/* �������Ǥ�ºݤΥե�����Ȥ��ƽ������� */

  /* in disk */
  UW		i_mode;
  UW		i_link;
  UW		i_index;
  UW		i_uid;
  UW		i_gid;
  UW		i_dev;		/* if device file */
  UW		i_size;
  UW		i_atime;
  UW		i_ctime;
  UW		i_mtime;
  UW		i_size_blk;

  /* �����˳ƥե����륷���ƥ���ȼ��ξ������� (union ��... )*/
  union
    {
      struct sfs_inode	sfs_inode;
    } i_private;
};


struct iops
{
  W		(*i_lookup)();
  W		(*i_create)();
  W		(*i_close)();
  W		(*i_read)();
  W		(*i_write)();
  W		(*i_stat)();
  W		(*i_truncate)();
  W		(*i_link)();
  W		(*i_unlink)();
  W		(*i_symlink)();		/* not used */
  W		(*i_chmod)();
  W		(*i_chown)();
  W		(*i_chgrp)();
  W		(*i_rename)();
  W		(*i_sync)();
  W		(*i_mkdir)();
  W		(*i_rmdir)();
  W		(*i_getdents)();
};


struct access_info
{
  W		uid;
  W		gid;
};

#endif /* KERNEL */


/* stat �����ƥॳ������
 */
struct stat
{
  UW		st_dev;
  UW		st_ino;
  UW		st_mode;
  UW		st_nlink;
  UW		st_uid;
  UW		st_gid;
  UW		st_rdev;
  UW		st_size;
  UW		st_blksize;
  UW		st_blocks;
  UW		st_atime;
  UW		st_mtime;
  UW		st_ctime;
};


/* statfs �����ƥॳ������
 */
struct statfs
{
  W		f_type;
  W		f_bsize;
  W		f_blocks;
  W		f_bfree;
  W		f_bavail;
  W		f_files;
  W		f_free;
};


struct special_file
{
  UW			major_minor;		/* ���ڥ����ե�����Υ᥸�㡼/�ޥ��ʡ��ֹ� */
  						/* �Ǿ�̥ӥåȤ� 0 �ξ��ϥ���饯���� */
  						/* �Ǿ�̥ӥåȤ� 1 �ξ��ϥ֥��å��� */
  B			name[MAX_MODULE_NAME];	/* �ǥХ����ɥ饤�Ф���Ͽ̾ */
  ID			port;			/* �ǥХ����ɥ饤�ФؤΥݡ��� */
  UW			dd;			/* �ǥХ����ε����ֹ椪��ӥѡ��ƥ�������ֹ� */
  ER			(*handle)();		/* ���̤ν���(�͡���ɥѥ��פʤ�) �򤹤�Ȥ��Υϥ�ɥ� */
};

extern struct special_file	special_file_table[];



#ifdef KERNEL

/* filesystem.c */
extern W		init_fs (void);
extern W		get_device_info (UW major_minor, ID *port, UW *dd);
extern struct inode	*alloc_inode (void);
extern W		dealloc_inode (struct inode *);
extern struct fs	*alloc_fs (void);
extern void		dealloc_fs (struct fs *);

extern W		fs_open_file (B *path, W oflag, W mode, struct access_info *acc,
				      struct inode *startip, struct inode **newip);
extern W		fs_close_file (struct inode *ip);
extern W		fs_lookup (struct inode	*startip, char *path, W	oflag,
				   W mode, struct access_info *acc, struct inode **newip);
extern W		fs_create_file (struct inode *startip, char *path, W oflag,
					W mode, struct access_info *acc, struct inode **newip);
extern W		fs_sync_file (struct inode *ip);
extern W		fs_read_file (struct inode *ip, W start, B *buf, W length, W *rlength);
extern W		fs_write_file (struct inode *ip, W start, B *buf, W length, W *rlength);
extern W		fs_remove_file (struct inode *startip, B *path,	struct access_info *acc);
extern W		fs_remove_dir (struct inode *startip, B *path, struct access_info *acc);
extern W		fs_statfs (ID device, struct statfs *result);


extern W		mount_root (ID device, W fstype, W option);
extern W		mount_fs (struct inode *device, struct inode *mountpoint, W fstype, W option);


extern struct inode	*fs_check_inode (struct fs *fsp, W index);
extern W		fs_register_inode (struct inode *ip);
extern W		init_special_file (void);
extern W		fs_convert_path (struct inode *ip, B *buf, W length);

extern W		permit(struct inode *ip, struct access_info *acc, UW bits);


extern struct fs	fs_buf[], *free_fs, *rootfs;
extern struct inode	inode_buf[], *free_inode, *rootfile;
extern struct file	file_buf[], *free_file;


#endif

#endif /* __POSIX_FS_H__ */


