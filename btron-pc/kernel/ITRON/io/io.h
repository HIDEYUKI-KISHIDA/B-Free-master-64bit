/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* io.h --- �ǥХ����ɥ饤�ФΤ�������
 *
 *
 */

#ifndef __IO_IO_H__
#define __IO_IO_H__	1

/* �ǥХ����ơ��֥��������� */
#define UNDEF_DEVICE	-1	/* ���Υ���ȥ�ϡ��Ȥ��Ƥ��ʤ� */
#define ANY_DEVICE	-2	/* Ǥ�դΥǥХ��� */

/* Common device names (ASCII only) */
#define DEVNAME_ECHO		"echo"
#define DEVNAME_RS232C	"rs232c"
#define DEVNAME_PD7220	"pd7220"

/* �ǥХ����ɥ饤�Фμ���
 *
 * B-Free OS �ǤΥǥХ����ˤϡ��֥��å����ΥǥХ����ȥ���饯������
 * �ǥХ�����2���ब���롣
 *
 */
typedef enum io_type_t
{
  BLOCK,			/* �֥��å����ǥХ��� */
  CHAR				/* ����饯�����ǥХ��� */
} IO_TYPE;


/* �ǥХ����ɥ饤�ФΥ��顼�ֹ����� 
 */
typedef enum
{
  IO_OK = 	0,		/* ���ｪλ		*/
  IO_TIMEOUT =  1,		/* �����ॢ����		*/
  IO_PERM =     2		/* �ѡ��ߥå���󥨥顼 */
} IO_ERR;


/* I/O ���ޥ�� --- �ɥ饤�Ф����뤿��Υ��ޥ��
 */
typedef enum io_command_t
{
  IO_NULL =	0x0000,		/* ���⤷�ʤ����ޥ�� */
  IO_OPEN =	0x0001,		/* �ɥ饤�ФΥ����ץ�  */
				/* ���٤ˤҤȤĤΥ��������������ѤǤ���ɥ饤�Ф� */
				/* ���˻��Ѥ��� */
  IO_CLOSE =	0x0002,		/* �ɥ饤�ФΥ�������: ������� */
  IO_READ =	0x0003,		/* �ɥ饤�Ф���Υǡ������ɤ߼�� */
  IO_WRITE =	0x0004,		/* �ɥ饤�ФؤΥǡ����ν񤭹��� */
  IO_STAT = 	0x0005,		/* �ǥХ������֤��ɤ߼�� */
  IO_CONTROL =  0x0006		/* �ǥХ����Υ���ȥ����� */
} T_IO_COMMAND;


/* IO_CONTROL���ޥ�ɤ���ǻ��Ѥ��륵�֥��ޥ��(����)
 *
 * ������¾�˥ǥХ�����ͭ�Υ��֥��ޥ�ɤ�������뤳�Ȥ�Ǥ��롣
 * ��ͭ�Υ��֥��ޥ�ɤˤĤ��Ƥϡ�����鶦�̥��֥��ޥ�ɤȽŤʤ�ʤ��褦 
 * ��Ƭ�ΥӥåȤ�1�ˤ��뤳��(0x8000 �Τ褦��)��
 */
#define IO_SYNC			0x0001
#define IO_ASYNC		0x0002


/* ��I/O���ޥ�ɤλ��ѻ��˥ɥ饤�Ф��Ϥ��ѥ��å� */

 /* �����ץ���˻��Ѥ���ѥ��å� */
struct io_open_packet
{
  UINT			device;	/* �ǥХ����ֹ� */
  W			mode;	/* �����ץ�⡼�� */
};

struct io_close_packet
{
  UINT			device;	/* �ǥХ����ֹ� */
};

struct io_read_packet
{
  W			offset;	/* read���ϰ���  */
				/* �ǥХ����ˤ�äƤϻ��Ѥ��ʤ� */
  W			size;	/* �ɤ߹��ॵ���� */
  VP			bufp;	/* �ǡ������ɤ߹���Хåե��ؤΥݥ��� */
};

struct io_write_packet
{
  W			offset;	/* write���ϰ��� */
  W			size;	/* �񤭹��ॵ���� */
  VP			bufp;	/* �ǡ�����񤭹���Хåե��ؤΥݥ��� */
};

struct io_stat_packet
{
  W			statid;
};

struct io_control_packet
{
  W			control;
  VP			argp;
};

typedef struct io_request_t
{
  T_IO_COMMAND	command;
  ID		taskid;		/* �׵ḵ�Υ������ɣ� */
  ID		resport;	/* �쥹�ݥ��ѤΥݡ��� */

  union
    {
      struct io_open_packet	open_pack;
      struct io_close_packet	close_pack;
      struct io_read_packet	read_pack;
      struct io_write_packet	write_pack;
      struct io_stat_packet	stat_pack;
      struct io_control_packet	control_pack;
    } s;
} T_IO_REQUEST;


/* �ꥯ�����Ȥ��Ф���������
 *
 * ���Υѥ��åȤϡ�ñ�˥��ޥ�ɤμ¹Ԥ��������������֤�������
 *
 */
typedef struct io_response_t
{
  IO_ERR		stat;
  
} T_IO_RESPONSE;


/* I/O device management interfaces */
ER	init_io (void);
ER	def_dev (B *name, IO_TYPE type, ID id, ID *rid);
ER	ref_dev (B *name, IO_TYPE type, ID *rid);
ER	get_ioreq (ID device, T_IO_REQUEST *req);
ER	put_res (ID device, T_IO_REQUEST *req, T_IO_RESPONSE *res);
ER	io_request (ID device, T_IO_REQUEST *req, T_IO_RESPONSE *res, W mode);
ER	io_response (ID device, T_IO_REQUEST *req, T_IO_RESPONSE *res);


#endif	/* __IO_IO_H__ */
