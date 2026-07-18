// --- サウンド再生・録音API雛形 ---
int audio_play_pcm(void *dev, const void *pcm, size_t len, int rate, int ch, int bits) {
    // TODO: PCMデータをrate, ch, bitsで再生
    (void)dev; (void)pcm; (void)len; (void)rate; (void)ch; (void)bits;
    return 0;
}

int audio_record_pcm(void *dev, void *pcm, size_t maxlen, int rate, int ch, int bits) {
    // TODO: PCMデータをrate, ch, bitsで録音
    (void)dev; (void)pcm; (void)maxlen; (void)rate; (void)ch; (void)bits;
    return 0;
}
#include <stdint.h>
#include "device.h"

// サウンドコントローラ（HD Audio, AC'97, USB Audio等）雛形
// PCIスキャンでclass_code==0x04

struct audio_info {
    volatile void *mmio_base;
    uint16_t vendor_id, device_id;
    int type; // 0:HD Audio, 1:AC97, 2:USB Audio, 3:Other
};


// --- サウンド read/write/ioctl 雛形 ---
static int audio_open(void *dev, int mode) {
    // 必要なら初期化
    return 0;
}
static int audio_close(void *dev) {
    return 0;
}
static ssize_t audio_read(void *dev, void *buf, size_t len) {
    // TODO: 録音データをbufに格納
    (void)dev; (void)buf; (void)len;
    return 0;
}
static ssize_t audio_write(void *dev, const void *buf, size_t len) {
    // TODO: bufの内容を再生
    (void)dev; (void)buf; (void)len;
    return 0;
}
static int audio_ioctl(void *dev, int cmd, void *arg) {
    // 例: ボリューム/サンプリングレート/入出力切替など
    (void)dev; (void)cmd; (void)arg;
    return 0;
}

void register_audio_device(volatile void *mmio_base, uint16_t vendor_id, uint16_t device_id, int type) {
    static struct audio_info priv;
    priv.mmio_base = mmio_base;
    priv.vendor_id = vendor_id;
    priv.device_id = device_id;
    priv.type = type;
    static struct device_ops ops = {
        .open = audio_open,
        .close = audio_close,
        .read = audio_read,
        .write = audio_write,
        .ioctl = audio_ioctl
    };
    static device_t audio_dev = {
        .name = "audio0",
        .type = DEV_TYPE_OTHER,
        .ops = &ops,
        .priv = &priv,
        .next = NULL
    };
    register_device(&audio_dev);
}
