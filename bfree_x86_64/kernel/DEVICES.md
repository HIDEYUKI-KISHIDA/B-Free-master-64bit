# B-Free x86_64 カーネルで使用可能な主なデバイス一覧

| デバイス種別 | ノード例 (/dev/...) | 備考 |
|:---|:---|:---|
| シリアル | serial0, serial1 | COMポート等 |
| PCI | pci0, pci1 | 自動列挙 |
| AHCI/SATA | ahci0, sata0 | HDD/SSD等 |
| USB | usb0, usb1 | XHCI/EHCI/UHCI等 |
| ネットワーク | net0, eth0, wifi0 | Ethernet/Wi-Fi |
| GPU | gpu0 | フレームバッファ/描画API |
| オーディオ | audio0 | HD Audio/AC97/USB |
| ACPI | acpi | 電源管理/イベント |
| TPM | tpm0 | セキュアエレメント |
| RTC | rtc0 | リアルタイムクロック |
| センサ | sensor0 | 温度/加速度等 |
| 仮想デバイス | null, zero, random | /dev/null等 |

---

# /dev, /sys, /proc での主な公開例

- /dev/serial0 など: デバイスI/O
- /sys/gpu0/ など: 属性・状態
- /proc/1234 など: プロセス情報
- /proc/cpuinfo, /proc/meminfo: システム情報

---

# 今後の拡張
- ホットプラグ対応デバイスの自動追加/削除
- デバイス依存関係・優先度・エラー状態の管理
- 権限・セキュリティ属性の付与
