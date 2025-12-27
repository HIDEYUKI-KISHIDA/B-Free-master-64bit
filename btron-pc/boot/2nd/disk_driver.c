/* IDE LBA48 モード書き込み */
static int ide_write_lba48(int channel, int drive, UWORD64 lba, int count, const unsigned char *buffer)
{
    int port = (channel == 0) ? IDE_PRIMARY_CMD : IDE_SECONDARY_CMD;
    int i, j;
    const unsigned short *data = (const unsigned short *)buffer;
    if (count < 1 || count > 65536) return -1;
    // セクタカウント（高位）設定
    outb(port + IDE_REG_SECTOR_CNT, (unsigned char)((count >> 8) & 0xFF));
    // セクタカウント（低位）設定
    outb(port + IDE_REG_SECTOR_CNT, (unsigned char)(count & 0xFF));
    // LBAアドレス（48bit分割）
    outb(port + IDE_REG_LBA_LOW, (unsigned char)((lba >> 24) & 0xFF));
    outb(port + IDE_REG_LBA_LOW, (unsigned char)((lba >> 0) & 0xFF));
    outb(port + IDE_REG_LBA_MID, (unsigned char)((lba >> 32) & 0xFF));
    outb(port + IDE_REG_LBA_MID, (unsigned char)((lba >> 8) & 0xFF));
    outb(port + IDE_REG_LBA_HIGH, (unsigned char)((lba >> 40) & 0xFF));
    outb(port + IDE_REG_LBA_HIGH, (unsigned char)((lba >> 16) & 0xFF));
    // デバイス選択
    outb(port + IDE_REG_DEVICE, 0x40 | (drive << 4));
    // WRITE SECTORS EXT コマンド
    outb(port + IDE_REG_COMMAND, 0x34);
    if (ide_wait_status(channel, drive, IDE_STATUS_DRQ, 100000) != 0) return -1;
    for (j = 0; j < count; j++) {
        for (i = 0; i < SECTOR_SIZE / 2; i++) {
            outw(port + IDE_REG_DATA, *data++);
        }
    }
    return count;
}
/* IDE LBA28 モード書き込み */
static int ide_write_lba28(int channel, int drive, UWORD64 lba, int count, const unsigned char *buffer)
{
    int port = (channel == 0) ? IDE_PRIMARY_CMD : IDE_SECONDARY_CMD;
    int i, j;
    const unsigned short *data = (const unsigned short *)buffer;
    if (count < 1 || count > 256) return -1;
    outb(port + IDE_REG_SECTOR_CNT, (unsigned char)count);
    outb(port + IDE_REG_LBA_LOW, (unsigned char)(lba & 0xFF));
    outb(port + IDE_REG_LBA_MID, (unsigned char)((lba >> 8) & 0xFF));
    outb(port + IDE_REG_LBA_HIGH, (unsigned char)((lba >> 16) & 0xFF));
    outb(port + IDE_REG_DEVICE, 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    outb(port + IDE_REG_COMMAND, IDE_CMD_WRITE);
    if (ide_wait_status(channel, drive, IDE_STATUS_DRQ, 100000) != 0) return -1;
    for (j = 0; j < count; j++) {
        for (i = 0; i < SECTOR_SIZE / 2; i++) {
            outw(port + IDE_REG_DATA, *data++);
        }
    }
    return count;
}
#ifndef DUMMY_DEFS_ADDED
#define DUMMY_DEFS_ADDED
typedef int size_t; typedef int ssize_t; typedef int off_t; typedef int time_t; typedef int pid_t; typedef int uid_t; typedef int gid_t; typedef int dev_t; typedef int ino_t; typedef int mode_t; typedef int nlink_t; typedef int blksize_t; typedef int blkcnt_t; typedef int sigset_t; typedef int va_list; typedef int jmp_buf[1];
#define NULL ((void*)0)
#define __attribute__(x)
#define __asm__(x)
#define __volatile__
#define __restrict
#define __inline__
#define __extension__
#define __builtin_va_list int
#define __builtin_va_start(a,b)
#define __builtin_va_end(a)
#define __builtin_va_arg(a,b) (0)
#define __builtin_offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif
#include "disk_driver.h"
#include "lib.h"

/* グローバルディスクパラメータ */
struct disk_param disk_params[4];

/* IDE ステータス待機（タイムアウト付き） */
static int ide_wait_status(int channel, int drive, unsigned char expected_flag, int timeout)
{
    unsigned char status;
    int port = (channel == 0) ? IDE_PRIMARY_CMD : IDE_SECONDARY_CMD;
    
    while (timeout-- > 0) {
        status = inb(port + IDE_REG_STATUS);
        
        if (status & IDE_STATUS_ERR) {
            return -1;  /* エラー */
        }
        
        if ((status & expected_flag) == expected_flag) {
            return 0;   /* 完了 */
        }
    }
    
    return -2;  /* タイムアウト */
}

/* IDE LBA28 モード読み込み */
static int ide_read_lba28(int channel, int drive, UWORD64 lba, int count, unsigned char *buffer)
{
    int port = (channel == 0) ? IDE_PRIMARY_CMD : IDE_SECONDARY_CMD;
    int i, j;
    unsigned short *data = (unsigned short *)buffer;
    
    if (count < 1 || count > 256) {
        return -1;
    }
    
    /* セクタカウント設定 */
    outb(port + IDE_REG_SECTOR_CNT, (unsigned char)count);
    
    /* LBA アドレス設定 */
    outb(port + IDE_REG_LBA_LOW, (unsigned char)(lba & 0xFF));
    outb(port + IDE_REG_LBA_MID, (unsigned char)((lba >> 8) & 0xFF));
    outb(port + IDE_REG_LBA_HIGH, (unsigned char)((lba >> 16) & 0xFF));
    
    /* デバイス・LBA上位4ビット設定 */
    outb(port + IDE_REG_DEVICE, 
         0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    
    /* READ コマンド送信 */
    outb(port + IDE_REG_COMMAND, IDE_CMD_READ);
    
    /* DRQ（Data Request）フラグを待機 */
    if (ide_wait_status(channel, drive, IDE_STATUS_DRQ, 100000) != 0) {
        return -1;
    }
    
    /* セクタのデータを読む */
    for (j = 0; j < count; j++) {
        for (i = 0; i < SECTOR_SIZE / 2; i++) {
            *data++ = inw(port + IDE_REG_DATA);
        }
    }
    
    return count;
}

/* IDE LBA48 モード読み込み */
static int ide_read_lba48(int channel, int drive, UWORD64 lba, int count, unsigned char *buffer)
{
    int port = (channel == 0) ? IDE_PRIMARY_CMD : IDE_SECONDARY_CMD;
    int i, j;
    unsigned short *data = (unsigned short *)buffer;
    
    if (count < 1 || count > 65536) {
        return -1;
    }
    
    /* セクタカウント（高位）設定 */
    outb(port + IDE_REG_SECTOR_CNT, (unsigned char)((count >> 8) & 0xFF));
    
    /* LBA アドレス（高位）設定 */
    outb(port + IDE_REG_LBA_LOW, (unsigned char)((lba >> 24) & 0xFF));
    outb(port + IDE_REG_LBA_MID, (unsigned char)((lba >> 32) & 0xFF));
    outb(port + IDE_REG_LBA_HIGH, (unsigned char)((lba >> 40) & 0xFF));
    
    /* セクタカウント（低位）設定 */
    outb(port + IDE_REG_SECTOR_CNT, (unsigned char)(count & 0xFF));
    
    /* LBA アドレス（低位）設定 */
    outb(port + IDE_REG_LBA_LOW, (unsigned char)(lba & 0xFF));
    outb(port + IDE_REG_LBA_MID, (unsigned char)((lba >> 8) & 0xFF));
    outb(port + IDE_REG_LBA_HIGH, (unsigned char)((lba >> 16) & 0xFF));
    
    /* デバイス設定（LBAモード） */
    outb(port + IDE_REG_DEVICE, 0xE0 | (drive << 4));
    
    /* READ EXT コマンド送信 */
    outb(port + IDE_REG_COMMAND, 0x24);  /* Read Sectors Ext */
    
    /* DRQ フラグを待機 */
    if (ide_wait_status(channel, drive, IDE_STATUS_DRQ, 100000) != 0) {
        return -1;
    }
    
    /* セクタデータを読む */
    for (j = 0; j < count; j++) {
        for (i = 0; i < SECTOR_SIZE / 2; i++) {
            *data++ = inw(port + IDE_REG_DATA);
        }
    }
    
    return count;
}

/* ディスク初期化 */
int disk_init(void)
{
    int channel, drive;
    int detected = 0;
    
    /* 全ドライブをリセット */
    for (channel = 0; channel < 2; channel++) {
        for (drive = 0; drive < 2; drive++) {
            disk_params[channel * 2 + drive].channel = channel;
            disk_params[channel * 2 + drive].drive = drive;
            disk_params[channel * 2 + drive].ready = 0;
        }
    }
    
    /* ドライブ検出 */
    for (channel = 0; channel < 2; channel++) {
        for (drive = 0; drive < 2; drive++) {
            if (disk_detect(channel, drive) == 0) {
                detected++;
            }
        }
    }
    
    console_printf("Disk driver initialized: %d drive(s) detected\n", detected);
    
    return 0;
}

/* ドライブ検出 */
int disk_detect(int channel, int drive)
{
    int port = (channel == 0) ? IDE_PRIMARY_CMD : IDE_SECONDARY_CMD;
    unsigned char status;
    int idx = channel * 2 + drive;
    
    /* ステータス読み込み */
    status = inb(port + IDE_REG_STATUS);
    
    if (status == 0xFF || status == 0x00) {
        /* ドライブなし */
        disk_params[idx].ready = 0;
        return -1;
    }
    
    /* IDENTIFY コマンド実行 */
    if (disk_identify(channel, drive) == 0) {
        disk_params[idx].ready = 1;
        console_printf("Drive detected: Channel=%d, Drive=%d\n", channel, drive);
        return 0;
    }
    
    disk_params[idx].ready = 0;
    return -1;
}

/* ドライブ識別 */
int disk_identify(int channel, int drive)
{
    int port = (channel == 0) ? IDE_PRIMARY_CMD : IDE_SECONDARY_CMD;
    int i;
    unsigned short data[256];
    struct disk_info *info = &disk_params[channel * 2 + drive].info;
    
    /* デバイス選択 */
    outb(port + IDE_REG_DEVICE, (drive << 4) | 0xA0);
    
    /* IDENTIFY コマンド */
    outb(port + IDE_REG_COMMAND, IDE_CMD_IDENTIFY);
    
    /* DRQ フラグを待機 */
    if (ide_wait_status(channel, drive, IDE_STATUS_DRQ, 100000) != 0) {
        return -1;
    }
    
    /* データ読み込み */
    for (i = 0; i < 256; i++) {
        data[i] = inw(port + IDE_REG_DATA);
    }
    
    /* ドライブ情報解析 */
    info->cylinders = data[1];
    info->heads = data[3];
    info->sectors = data[6];
    info->total_sectors = (data[61] << 16) | data[60];
    info->lba_mode = (data[49] & 0x0200) ? 1 : 0;
    
    /* モデル番号を抽出 */
    for (i = 0; i < 20; i++) {
        unsigned short w = data[27 + i];
        info->model[i * 2] = (w >> 8) & 0xFF;
        info->model[i * 2 + 1] = w & 0xFF;
    }
    info->model[40] = 0;
    
    return 0;
}

/* 1セクタ読み込み */
int disk_read_sector(int drive, UWORD64 lba, unsigned char *buffer)
{
    int channel = drive / 2;
    int drive_num = drive % 2;
    
    if (drive < 0 || drive >= 4) {
        return -1;
    }
    
    if (!disk_params[drive].ready) {
        return -1;
    }
    
    return ide_read_lba28(channel, drive_num, lba, 1, buffer);
}

/* 複数セクタ読み込み */
int disk_read_sectors(int drive, UWORD64 lba, int count, unsigned char *buffer)
{
    int channel = drive / 2;
    int drive_num = drive % 2;
    int remaining = count;
    int read = 0;
    
    if (drive < 0 || drive >= 4 || count <= 0) {
        return -1;
    }
    
    if (!disk_params[drive].ready) {
        return -1;
    }
    
    while (remaining > 0) {
        int to_read = (remaining > 256) ? 256 : remaining;
        
        if (ide_read_lba28(channel, drive_num, lba, to_read, buffer) <= 0) {
            return -1;
        }
        
        read += to_read;
        remaining -= to_read;
        lba += to_read;
        buffer += to_read * SECTOR_SIZE;
    }
    
    return read;
}

/* 1セクタ書き込み */
int disk_write_sector(int drive, UWORD64 lba, unsigned char *buffer)
{
    int channel = drive / 2;
    int drive_num = drive % 2;
    if (drive < 0 || drive >= 4) return -1;
    if (!disk_params[drive].ready) return -1;
    // 28bit範囲外はLBA48コマンド
    if (lba > 0x0FFFFFFF) {
        return ide_write_lba48(channel, drive_num, lba, 1, buffer);
    }
    return ide_write_lba28(channel, drive_num, lba, 1, buffer);
}

/* 複数セクタ書き込み */
int disk_write_sectors(int drive, UWORD64 lba, int count, unsigned char *buffer)
{
    int channel = drive / 2;
    int drive_num = drive % 2;
    int remaining = count;
    int written = 0;
    if (drive < 0 || drive >= 4 || count <= 0) return -1;
    if (!disk_params[drive].ready) return -1;
    while (remaining > 0) {
        int to_write = (lba > 0x0FFFFFFF) ? ((remaining > 65536) ? 65536 : remaining) : ((remaining > 256) ? 256 : remaining);
        int ret;
        if (lba > 0x0FFFFFFF) {
            ret = ide_write_lba48(channel, drive_num, lba, to_write, buffer);
        } else {
            ret = ide_write_lba28(channel, drive_num, lba, to_write, buffer);
        }
        if (ret <= 0) return -1;
        written += ret;
        remaining -= to_write;
        lba += to_write;
        buffer += to_write * SECTOR_SIZE;
    }
    return written;
}

/* キャッシュフラッシュ */
int disk_flush_cache(int drive)
{
    int channel = drive / 2;
    int drive_num = drive % 2;
    int port = (channel == 0) ? IDE_PRIMARY_CMD : IDE_SECONDARY_CMD;
    
    /* FLUSH CACHE コマンド */
    outb(port + IDE_REG_DEVICE, (drive_num << 4) | 0xA0);
    outb(port + IDE_REG_COMMAND, IDE_CMD_FLUSH);
    
    /* 完了を待機 */
    return ide_wait_status(channel, drive_num, 0, 100000);
}

/* ディスク情報取得 */
struct disk_info *disk_get_info(int drive)
{
    if (drive < 0 || drive >= 4) {
        return NULL;
    }
    
    return &disk_params[drive].info;
}

/* ディスク状態取得 */
int disk_get_status(int drive)
{
    if (drive < 0 || drive >= 4) {
        return -1;
    }
    
    return disk_params[drive].ready ? 0 : -1;
}

/* デバッグ情報表示 */
void disk_debug_info(int drive)
{
    struct disk_info *info;
    
    if (drive < 0 || drive >= 4) {
        return;
    }
    
    info = &disk_params[drive].info;
    
    console_printf("=== Drive %d Debug Info ===\n", drive);
    console_printf("Cylinders:  %u\n", info->cylinders);
    console_printf("Heads:      %u\n", info->heads);
    console_printf("Sectors:    %u\n", info->sectors);
    console_printf("Total Sectors: %ld\n", info->total_sectors);
    console_printf("LBA Mode:   %s\n", info->lba_mode ? "Yes" : "No");
    console_printf("Model:      %s\n", info->model);
}
