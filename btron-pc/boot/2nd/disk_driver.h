#ifndef __DISK_DRIVER_H__
#define __DISK_DRIVER_H__

#include "types.h"

/* IDE/ATA ポート定義 */
#define IDE_PRIMARY_CMD     0x1F0
#define IDE_PRIMARY_CTL     0x3F6
#define IDE_PRIMARY_IRQ     14

#define IDE_SECONDARY_CMD   0x170
#define IDE_SECONDARY_CTL   0x376
#define IDE_SECONDARY_IRQ   15

/* IDEレジスタオフセット */
#define IDE_REG_DATA        0
#define IDE_REG_ERROR       1
#define IDE_REG_FEATURE     1
#define IDE_REG_SECTOR_CNT  2
#define IDE_REG_LBA_LOW     3
#define IDE_REG_LBA_MID     4
#define IDE_REG_LBA_HIGH    5
#define IDE_REG_DEVICE      6
#define IDE_REG_STATUS      7
#define IDE_REG_COMMAND     7

/* IDE コマンド */
#define IDE_CMD_READ        0x20    /* Read Sectors with Retry */
#define IDE_CMD_WRITE       0x30    /* Write Sectors with Retry */
#define IDE_CMD_IDENTIFY    0xEC    /* Identify Device */
#define IDE_CMD_FLUSH       0xE7    /* Flush Cache */

/* IDE ステータスフラグ */
#define IDE_STATUS_ERR      0x01    /* Error */
#define IDE_STATUS_IDX      0x02    /* Index */
#define IDE_STATUS_CORR     0x04    /* Corrected Data */
#define IDE_STATUS_DRQ      0x08    /* Data Request */
#define IDE_STATUS_DSC      0x10    /* Device Seek Complete */
#define IDE_STATUS_DF       0x20    /* Device Fault */
#define IDE_STATUS_DRDY     0x40    /* Device Ready */
#define IDE_STATUS_BSY      0x80    /* Busy */

/* セクタサイズ */
#define SECTOR_SIZE         512

/* ディスク情報 */
struct disk_info {
    int drive_type;             /* 0=ATA, 1=ATAPI */
    unsigned int cylinders;
    unsigned int heads;
    unsigned int sectors;
    UWORD64 total_sectors;
    int lba_mode;               /* LBAサポート */
    char model[256];            /* ドライブモデル */
};

/* ディスクパラメータ */
struct disk_param {
    int channel;                /* 0=Primary, 1=Secondary */
    int drive;                  /* 0=Master, 1=Slave */
    struct disk_info info;
    int ready;                  /* ドライブ準備完了 */
};

/* グローバル変数 */
extern struct disk_param disk_params[4];  /* 4ドライブ（Primary Master/Slave, Secondary Master/Slave） */

/* ディスク初期化 */
int disk_init(void);
int disk_detect(int channel, int drive);
int disk_identify(int channel, int drive);

/* ディスク操作 */
int disk_read_sector(int drive, UWORD64 lba, unsigned char *buffer);
int disk_read_sectors(int drive, UWORD64 lba, int count, unsigned char *buffer);
int disk_write_sector(int drive, UWORD64 lba, unsigned char *buffer);
int disk_write_sectors(int drive, UWORD64 lba, int count, unsigned char *buffer);

/* ディスク制御 */
int disk_flush_cache(int drive);

/* ディスク情報取得 */
struct disk_info *disk_get_info(int drive);
int disk_get_status(int drive);

/* デバッグ */
void disk_debug_info(int drive);

#endif  /* __DISK_DRIVER_H__ */
