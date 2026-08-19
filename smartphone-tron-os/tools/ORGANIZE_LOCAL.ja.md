# J:\SMARTPHONE TRON OS ローカル整理手順

Git 正本: `B-Free-master/Program/smartphone-tron-os/`（またはリポジトリルート `smartphone-tron-os/`）

---

## 1. 現状把握（PowerShell）

```powershell
$src = "J:\SMARTPHONE TRON OS"
$old = "C:\Users\h_kis\Desktop\SMARTPHONE TRON OS"

Get-ChildItem $src -Recurse -File | Select-Object FullName, Length, LastWriteTime | Format-Table -AutoSize
if (Test-Path $old) {
  Write-Host "--- Desktop 旧フォルダ ---"
  Get-ChildItem $old -Recurse -File | Select-Object FullName, Length, LastWriteTime | Format-Table -AutoSize
}
```

---

## 2. 推奨配置（整理後）

```
J:\SMARTPHONE TRON OS\
├── docs\
│   ├── spec\
│   │   └── SMARTPHONE_TRON_OS_仕様書_v0.5.md      ← 正本
│   ├── archive\
│   │   └── SMARTPHONE_TRON_OS_仕様書_v0.4.md
│   ├── hardware\                                  ← Xperia / DT / PMIC メモ
│   ├── software\                                  ← Qt guest / POSIX メモ
│   └── reviews\                                   ← レビュー反映メモ
├── bsp\
├── guest\
└── README.md                                      ← Git からコピー
```

---

## 3. 整理スクリプト（手動確認後に実行）

```powershell
$root = "J:\SMARTPHONE TRON OS"
$dirs = @(
  "docs\spec", "docs\archive", "docs\hardware", "docs\software", "docs\reviews",
  "bsp", "guest", "tools"
)
foreach ($d in $dirs) { New-Item -ItemType Directory -Force -Path (Join-Path $root $d) | Out-Null }

# 仕様書を版ごとに移動（存在する場合）
$v05 = Get-ChildItem $root -Filter "*仕様書*v0.5*" -Recurse -File -ErrorAction SilentlyContinue
$v04 = Get-ChildItem $root -Filter "*仕様書*v0.4*" -Recurse -File -ErrorAction SilentlyContinue
if ($v05) { Move-Item $v05.FullName (Join-Path $root "docs\spec\") -Force -ErrorAction SilentlyContinue }
if ($v04) { Move-Item $v04.FullName (Join-Path $root "docs\archive\") -Force -ErrorAction SilentlyContinue }

# Desktop 旧フォルダを統合（重複は手動マージ）
$old = "C:\Users\h_kis\Desktop\SMARTPHONE TRON OS"
if (Test-Path $old) {
  Copy-Item "$old\*" $root -Recurse -Force -ErrorAction SilentlyContinue
  Write-Host "Desktop からコピー済み。確認後 Desktop 側は archive へリネーム推奨"
}
```

---

## 4. Git に載せる（WSL）

```bash
# 例: リポジトリ内 smartphone-tron-os/ に同期
REPO="/mnt/c/Users/h_kis/Desktop/B-Free-master/Program"
cp -a "/mnt/j/SMARTPHONE TRON OS/docs/spec/"* "$REPO/smartphone-tron-os/docs/spec/" 2>/dev/null || true
cp -a "/mnt/j/SMARTPHONE TRON OS/docs/archive/"* "$REPO/smartphone-tron-os/docs/archive/" 2>/dev/null || true

cd "$REPO"
git add smartphone-tron-os/
git status
git commit -m "docs(smartphone-tron-os): sync spec from J: drive"
```

---

## 5. 版のルール

| 操作 | ルール |
|------|--------|
| 編集中 | `docs/spec/` の **v0.5 のみ** 更新 |
| 大改訂前 | 現行を `docs/archive/` にコピーしてから v0.6 へ |
| ハードメモ | `docs/hardware/`（JH7110 とは別フォルダ） |
| B-Free 実装 | `bfree_x86_64/` / `bfree_aarch64/` にコード。本フォルダは **仕様・BSP メモのみ** |

---

## 6. 仕様書 v0.5 の主要章（整理時の目次チェック）

整理時に欠けていないか確認:

- §10 … BSP フェーズ（Phase 1〜5）
- §17 … ソフトウェア路線（Qt ゲスト・4 層互換・§17.7 研究レーン）
- §18 … 改訂履歴

v0.4 から追加された §17.6〜17.8（4 層互換・syscall ブリッジ・読み方）が v0.5 に含まれること。
