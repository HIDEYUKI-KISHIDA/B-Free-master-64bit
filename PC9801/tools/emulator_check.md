# PC-9801ブート・エミュレータ検証手順

## 検証対象
- DOSBox-X（推奨）
- QEMU（オプション）
- Neko Project II（オプション）
- MAME（オプション）

## 検証手順
1. `run_pc98_dosboxx.ps1` を実行し、`pc98_boot.img` で起動する。
2. 起動ログ・画面キャプチャを `captures/` ディレクトリに保存。
3. OSの起動・BIOSメッセージ・B-Freeロゴ等が表示されるか確認。
4. キーボード入力・ディスクアクセス・DMA・VFS・プロセス管理・syscall等の基本機能が動作するか確認。
5. ISO/USBブート（El Torito）イメージで起動し、同様に動作確認。
6. QEMU/Neko Project II/MAMEでも同様の手順で起動検証。

## トラブルシュート
- 起動しない場合は `dosbox-x.conf` の設定やイメージファイルのパスを再確認。
- BIOSエラーや未対応機能は `ChangeLog` や `README` を参照。
- エミュレータごとの制限事項は各公式ドキュメントを参照。

## 備考
- DOSBox-XはPC-9801互換性が高く、開発・検証に推奨。
- `captures/` ディレクトリに自動保存されるログ・スクリーンショットを活用。
