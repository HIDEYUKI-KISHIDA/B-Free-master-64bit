// --- TPM暗号化・認証API雛形 ---
int tpm_get_random(void *dev, void *buf, size_t len) {
    // TODO: TPMから安全な乱数を取得
    (void)dev; (void)buf; (void)len;
    return 0;
}

int tpm_generate_key(void *dev, int key_type, void *out, size_t outlen) {
    // TODO: TPMで鍵ペア生成
    (void)dev; (void)key_type; (void)out; (void)outlen;
    return 0;
}

int tpm_sign(void *dev, const void *data, size_t datalen, void *sig, size_t *siglen) {
    // TODO: TPMで署名生成
    (void)dev; (void)data; (void)datalen; (void)sig; (void)siglen;
    return 0;
}

int tpm_verify(void *dev, const void *data, size_t datalen, const void *sig, size_t siglen) {
    // TODO: TPMで署名検証
    (void)dev; (void)data; (void)datalen; (void)sig; (void)siglen;
    return 0;
}
#include <stdint.h>
#include "device.h"

// TPM（Trusted Platform Module）セキュリティデバイス雛形
// PCIスキャンでclass_code==0x0C, subclass==0x05, prog_if==0x20（TPM）やACPIテーブル探索

struct tpm_info {
    volatile void *mmio_base;
    int version; // 1:TPM1.2, 2:TPM2.0
};


// --- TPM read/write/ioctl 雛形 ---
static int tpm_open(void *dev, int mode) {
    // 必要なら初期化
    return 0;
}
static int tpm_close(void *dev) {
    return 0;
}
static ssize_t tpm_read(void *dev, void *buf, size_t len) {
    // TODO: TPMからデータ取得（例: ランダム値、証明書）
    (void)dev; (void)buf; (void)len;
    return 0;
}
static ssize_t tpm_write(void *dev, const void *buf, size_t len) {
    // TODO: TPMへコマンド送信（例: 鍵登録、認証）
    (void)dev; (void)buf; (void)len;
    return 0;
}
static int tpm_ioctl(void *dev, int cmd, void *arg) {
    // 例: TPMバージョン取得/状態取得/初期化等
    (void)dev; (void)cmd; (void)arg;
    return 0;
}

void register_tpm_device(volatile void *mmio_base, int version) {
    static struct tpm_info priv;
    priv.mmio_base = mmio_base;
    priv.version = version;
    static struct device_ops ops = {
        .open = tpm_open,
        .close = tpm_close,
        .read = tpm_read,
        .write = tpm_write,
        .ioctl = tpm_ioctl
    };
    static device_t tpm_dev = {
        .name = "tpm0",
        .type = DEV_TYPE_OTHER,
        .ops = &ops,
        .priv = &priv,
        .next = NULL
    };
    register_device(&tpm_dev);
}
