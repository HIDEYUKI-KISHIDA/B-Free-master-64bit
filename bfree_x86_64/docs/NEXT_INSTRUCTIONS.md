# 次の指示（スマホ編集用）

このファイルは **GitHub モバイルから編集して指示を残す**ためのものです。
Cursor エージェントはセッション開始時にここを読みます。

## スマホでの出し方

**出先は `PHONE.md` だけ編集する**（この長いファイルは触らなくてよい）。

1. GitHub アプリで下の直リンクを開く（ブランチ `cursor/d2c-vfile-slots-9760`）
2. ✏️ → 末尾の指示（いまは「次へ」）だけを書き換える → **Commit**
3. PC のこのチャットで「次」と言う。エージェントは `git fetch` して `PHONE.md` を読む

直リンク（アプリで開く）:
https://github.com/HIDEYUKI-KISHIDA/B-Free-master-64bit/blob/cursor/d2c-vfile-slots-9760/bfree_x86_64/docs/PHONE.md

編集画面（鉛筆を探さなくてよい）:
https://github.com/HIDEYUKI-KISHIDA/B-Free-master-64bit/edit/cursor/d2c-vfile-slots-9760/bfree_x86_64/docs/PHONE.md

---

## スマホ指示（ここだけ編集）

<!-- PHONE_START: この行と PHONE_END の間に書いて Commit -->

次へ

<!-- PHONE_END -->


書き方の例:
- `次へ`
- `止まって。スモークだけ回せ`
- `ISO を作り直して`

---

## 常設方針（エージェント用・スマホでは触らなくてよい）

**正本:** `C:\Users\h_kis\Desktop\B-Free-master`（詳細: `docs/CANONICAL.md`）。`J:\B-Free-master` は古いコピー・使わない。
本線: `work/posix-holes-redo`。

**優先: ホスト DesktopShell 必須の 3 穴。** 薄い塊寄せは据え置き。

| # | 穴 | 状態（強化プローブ 2026-08-08） |
|---|-----|------|
| H1 | Item↔Item `setParentItem` | **緑**（QML↔QML / bare Item / nested）— 製品ツリー解禁候補 |
| H2 | SG flush | 単純 `UpdateRequest` **緑**；**H2b 緑**（プロトコル＋ Quick nullkids） |
| H3 | kde/wabi 子 IR | **緑**（ClockApplet create+parent；下記） |

**着手順（残り）:** 必須 8 + 拡張 4（ホスト権威 1〜12）は 2026-08-08 本線スモーク緑。相対 CU `addImport` は **H3 URL bypass**（soft-rel はハングで据え置き）。

**H2b:** unexpose → contentItem 兄弟を hidden で attach → re-expose → leaf update → `requestUpdate`（hidden）→ show → `requestUpdate`。

**H3 根因:** 古い `guest_clock_applet_qmlcache` が HIT 直後 `QString::replaceArgEscapes` で PF@CR2=0x29000000。URL 自体は無害（Child ユニットを Clock URL に載せると緑）。修正: `ClockApplet.qml` を `import QtQuick`（**バージョン無し**；`2.15` 付き極小ユニットは赤）で再 qmlcachegen + `install_guest_qmlcache_unit.py --ref guest_product_child_qmlcache.cpp`（`tools/_tmp_rebuild_clock_qmlcache.sh`）。Timer / Date / formatDateTime 付き本番相当で緑。

## 現在の状態（エージェントが更新）

- **Updated:** 2026-08-29（**QGUI C flush PASS**。`wl_stub_flush_app` 直呼び。原因は W9 缶詰 wire が `msg[768]` を超えるスタック破壊（`wl_client_build` ≈860、CR2=`0xB00080006`）。`WL_CLIENT_MSG_MAX=1024`。`createUnixEventDispatcher` / `processEvents` 未使用。日次未タッチ）
- **TypeLoader:** 直せない。`loadUrl` / Qml 再ビルド / always-true from boot はしない。
- **結果:** stamp `d2b-v4-g1sg-qpa-v5` + `d2b-v4-g1real-v2`。holder `0x525eba0`。hook / pulse / winok / parentok / stomp / vis / cmp / peek / mu / idx / bsp / bso / 2h / stb / stv / rw（#GP）/ rwa（ハング）/ ir / po / sy / rg / psr / sbr（ハング）/ pd / cpd（PF）/ sc / qh / qd / qf / fm / pe / ev / eh / dp / dv / nb / rb / r2 / sp / s2 / s3 / e2 / sh / h2 / hb / hx / bg / **px**（`g1sg-px`）退避。**grab / instance / show / handleUpdateRequest 直呼び / QBackingStore ctor / `renderWindow` / stub `setBackingStore`+`render` / `setCurrentPaintDevice`+`render` / `render()` 単体 再試行 禁止。** `QPainter` ctor は bg で戻り。desk は **px**。compositor resident reget。日次 ISO / g1-desk 未タッチ。
  - `QObject::metaObject()` 直呼びは d_ptr+0x38=0 で **QObject::staticMetaObject** を返すので `inherits(QQuickWindow)` は偽。このバイナリの vptr+0 が metaObject（+0x10 は GP）。製品 root 実測 vptr **`0x402d2b8` = QQuickWindow**（QmlImpl ではない）
  - 薄い単位 + 窓 + **`G1 SG present UR ok`**
  - 製品 fill n=0x20 miss=0 stand-in=8。**`D2 product beginCreate ok`**
  - **`G1 product parent ok`:** 製品 `QQuickWindow::contentItem` を薄い窓の `contentItem` へ `setParentItem`（H1、call/ret、`g_prod_sg_win` は薄い SHM 窓のまま）→ 2回目 UR ok。続けて `setSize(1024×768)` も UR 緑だがスクショ不変
- **SG present 試行（これ以上同じ手は禁止）:**
  - guest_main に present を足す: v1/v2/v3 で QtQuick register / ctor PF
  - tu-v3 新 `--wrap` argv 末尾: 不発。tu-v4 wrap-first: holder `0x525fba0` ctor PF
  - **tu-v5 PASS** hook-only（`QObject::setParent` wrap）。別 TU stamp のみ
  - **tu-v6** 末尾 TU に QQuickWindow/QWindowPrivate: holder `0x525fba0`、**G1 types fill 前に CR2=`0x103BFFC1F`**。戻した
  - **tu-v7** guest_main 末尾に `guest_prod_sg_try_pulse`（既存 `pulse_flush` を呼ぶだけ）: **同じ CR2=`0x103BFFC1F`**。戻した
  - `.text` / holder を 4KiB ずらすと `qml_register_types_QtQuick` が死ぬ。新 TU の Qt include も guest_main 増分もだめ
  - parent thunk を **jmp** で入れる: #GP vector `0xD`（SSE アライン）。**call で入れ直すこと**
  - **`QQuickWindow::grabWindow`:** シリアル `G1 SG present UR enter` のまま戻らず。**再試行禁止。**
  - **cmp store vs d3:** bits と `g_d3_fb_bits` は同一 `0x160157a8`。flush は D2 タイル。`QT_QUICK_BACKEND=software` は getenv 済み。
  - **peek +0x238:** priv `0x165c4228`、loop `0x165c5b58`（非 null）、renderControl `0`、loop vptr **`0x4052848` = QSGSoftwareRenderLoop**。窓に software loop は既にある。`instance()` は不要。ハングは `show()` 側。
  - **maybeUpdate (`0x2bb4550`):** シリアル緑。hex は priv `0x165c4228` + loop `0x165c5b58` + **hash `0`**。空ハッシュは即 `ret`（`je +0x100`）。`parent_ok=1`、2× UR ok、resident reget。画面は D2c のまま。
  - **WindowData `operator[]` (`0x2bb40a0`):** シリアル緑。1回目 hash before `0` → WindowData `0x165c9020` → hash after **`0x165c8f48`**。2回目は already-filled（before=after=`0x165c8f48`、同じ WindowData）。hash insert はハングしない。`show()` のハングはこのあと（`QBackingStore` ctor または vcall `+0x98`）。
  - **bsp peek:** シリアル緑。hex は bs hash `*(loop+0x20)` / WindowData w0 / WindowData&。1回目 **`0` / `0` / `0x165c9020`**。2回目 **`0` / `1` / `0x165c9020`**（w0 bit0 = updatePending。BackingStore hash は空のまま）。
  - **BS `operator[]` (`0x2bb4b20`):** シリアル緑。1回目 hash before `0` → after **`0x165c8f48`**、***slot=`0`**、slot addr `0x165c9020`。2回目 already-filled（before=after=`0x165c8f48`、*slot は 0 のまま）。ctor なしでスロットだけ作れる。`g_bfree_wl_store` は `QBfreeWlBackingStore` であり `QBackingStore*` ではないのでスロットへ直書きしない。
  - **2h (WD+BS 同時):** シリアル緑。hex は *WD / *BS / WD hash / BS hash。1回目 **`0` / `0` / `0x165c8f48` / `0x165c9328`**（hash は別物）。2回目 *WD=`1`、*BS=`0`、hash 同じ。両方のスロットが同一 thunk で存在する。
  - **stb poke:** シリアル緑。*slot **`0x524aa80`** = BSS stub、d+8 **`0x16015678`** = QPA `QBfreeWlBackingStore`（cmp の store と同じ）。`sendPosted` してもハングせず UR ok。
  - **stv (stub+vis):** シリアル緑。visible/exposed を poke したあと *slot **`0x524aa80`**。2× UR ok、resident reget。sendPosted はハングしない。画面は stb と同じ D2c。
  - **renderWindow (`0x2bb53c0`):** `parent_ok=0`。`G1 SG present UR enter` のあと **#GP vector `0xD`**。RIP=`0x3846183` = `QString::vasprintf+0x73`。`bytes@RIP=0f 29 …`（`movaps`）。thunk は push rbp/r14/rbx のあと `sub $8` で call 直前 rsp≡8（SSE 非アライン）。**同じ thunk の再試行禁止。** rw 退避は残す。
  - **rwa (aligned renderWindow):** 3 push のあと `sub $0x10`（call 直前 rsp≡0）。stamp `g1sg-rwa`。`parent_ok=0`。シリアルは `G1 SG present UR enter` で **PANIC なし・戻らず**（~100s 待ち）。#GP は直った。中でハング（hash 探索 / `isRenderable` / `polishItems` / sync / render）。**`renderWindow` 自体の再試行禁止。** rwa 退避は残す。
  - **ir (isRenderable):** シリアル緑。hex **`0x1`** が UR enter の直後に 2 回（vis poke 後は描画可能）。`parent_ok=1`、2× UR ok、resident reget。renderWindow ハングは `isRenderable` のあと（polish / sync / render）。
  - **po (polishItems):** シリアル緑。`polishItems`（`0x28d7930`）は戻り、2× UR ok。`parent_ok=1`、resident reget。polish 自体はハングしない。画面は D2c のまま（画素は変わらず）。
  - **sy (syncSceneGraph):** シリアル緑。`syncSceneGraph`（`0x28d82b0`）は戻り、2× UR ok。`parent_ok=1`、resident reget。sync 自体はハングしない。renderWindow ハングは sync のあと（`renderSceneGraph` / blit）。
  - **rg (renderSceneGraph 単体):** シリアル緑。`renderSceneGraph`（`0x28d4890`）は戻る。`parent_ok=1`、2× UR ok。画面は D2c 完全一致（白カード 378123 + 壁紙 284832 + EX/VW/TE 10404 + シアン 4096）。sync 無しなので renderer null で即 return。画素は出ていない。
  - **psr (polish+sync+renderSceneGraph):** シリアル緑。3つとも戻り、2× UR ok。`parent_ok=1`、resident reget。`sendPosted` / `requestUpdate` は枠のため省略。画面は D2c **完全一致**（白カード 378123 + 壁紙 284832 + EX/VW/TE 10404 + シアン 4096）。rasterize は走ったが **WL SHM / `g_d3_fb_bits` には乗っていない**（paint device / backing store 未設定）。
  - **sbr (setBackingStore+render):** renderer は priv+**`0x218`** = **`0x165c90b8`**（非 null）。そのあと `[qt] QPA beginPaint skip` が 2 回出て **ハング**。`parent_ok=0`、UR ok なし。PANIC なし。stub `QBackingStore` 経由の `render()` は beginPaint に入る。**同じ setBackingStore+render は再試行禁止。** sbr 退避は残す。
  - **pd (paintDevice vcall):** シリアル緑。`*g_bfree_wl_store` vcall +0x10 → hex **`0x16015688`**（2回）。store 本体は cmp の `0x16015678` なので **+0x10 の埋め込み QImage**（`QBfreeWlBackingStore::m_image`）。`parent_ok=1`、2× UR ok、resident reget。beginPaint には入らない。
  - **cpd (setCurrentPaintDevice+render):** paintDevice hex **`0x16015688`** のあと setCurrent は戻り、renderer hex **`0x165c90b8`**。その直後 `QSGSoftwareRenderer::render`（`0x28f2920`）が **PF RIP=`0` CR2=`0`**（`[SURVIVE]`）。`parent_ok=0`、UR ok なし。PANIC / #GP なし。beginPaint skip は main entry 直後の 1 回だけで render 経路ではない。GPR に幅 `0x400` と bits **`0x160157a8`**（`g_d3_fb_bits` と同じ）があるので raster は SHM に入ったあと null 関数ポインタへ jump。**`setCurrentPaintDevice`+`render` の再試行禁止。** `render()` 単体の再試行も禁止。cpd 退避は残す。
  - **sc (setCurrentPaintDevice だけ):** シリアル緑。hex **`0x16015688` / `0`**（renderer+`0x200` = pd、+`0x208` = backing store は 0）。2× UR ok。`parent_ok=1`、resident reget。setCurrent はスロットに乗る。`render` は呼ばない。
  - **qh (QImage 先頭 4 qword):** シリアル緑。hex **`0x4059fe8` / `0` / `0x160156f8` / `0x48`**。vptr `0x4059fe8` = `vtable for QImage`（`0x4059fd8`+0x10）。+8 reserved=0。+16 = **QImageData\*** `0x160156f8`。+24=`0x48`（オブジェクト外の可能性）。2× UR ok。`parent_ok=1`、resident reget。本物 QImage。
  - **qd (QImageData 先頭 4 qword):** シリアル緑。hex **`0x40000000001` / `0x2000000300` / `0x300000` / `0x3ff0000000000000`**。ref=1、width=1024、height=768、depth=32、nbytes=3MiB（`1024×768×4`）、dpr=1.0。format / `data*` はこの 4 qword の外（+0x20 以降）。2× UR ok。`parent_ok=1`、resident reget。QImage は 1024×768 RGB 相当で生きている。
  - **qf (QImageData +0x20..+0x38):** シリアル緑。hex **`0` / `0` / `0` / `0x160157a8`**。+0x20/+0x28 は cleanup 関数/info（null）。+0x30=0（format ではない）。+0x38 = **`data*` `0x160157a8`** = `g_d3_fb_bits`。2× UR ok。`parent_ok=1`、resident reget。画素バッファは SHM と同一。
  - **fm (QImageData +0x40..+0x58):** シリアル緑。hex **`4` / `0x1000` / `0x100000001` / `0x40ad870e1c3870e2`**。**format=`4` = `QImage::Format_RGB32`**、**bytes_per_line=4096**（1024×4）。cpd の RIP=0 は Invalid format ではない。+0x50 は ser/detach 系の 1,1。+0x58 は未解釈。2× UR ok。`parent_ok=1`、resident reget。QImage は 1024×768 RGB32 で完備。
  - **pe (paintEngine vcall):** シリアル緑。QImage vptr+**`0x18`** = `QImage::paintEngine`（`0x2c609b0`）→ hex **`0x165cca68`**（非 null、heap。fm の +0x58 ゴミではない）。2× UR ok。`parent_ok=1`、resident reget。初回 vcall は戻り、`QPainter` / `render` は未使用。
  - **ev (engine vptr):** シリアル緑。hex **`0x405e838`** が 2 回。`vtable for QRasterPaintEngine` は **`0x405e828`**、オブジェクト vptr = +`0x10` で一致。2× UR ok。`parent_ok=1`、resident reget。engine 欠落ではない。`render()` の RIP=0 は metric（vptr+`0x20`、幅 `0x400`）のあと、`QPainter::QPainter(QPaintDevice*)`（`0x2e2bd30`）〜 raster compose の null 関数ポインタ。
  - **eh (engine 先頭 4 qword):** シリアル緑。hex **`0x405e838` / `0` / `0x4fffffeff` / `0x165cca98`**。`QPaintEngine` ctor 実測: +0 vptr、**+8 は常に 0**（d_ptr ではない）、+0x10 features=`0xfffffeff`、+0x14 type/bitfields=`4`、**+0x18 = d_ptr `0x165cca98`（非 null）**。engine 本体 `0x165cca68` から +`0x30`。2× UR ok。`parent_ok=1`、resident reget。RIP=0 は d_ptr 欠落ではない。
  - **dp (d_ptr 先頭 4 qword):** シリアル緑。hex **`0x405ea30` / `0` / `0x165cca68` / `0x405ef80`**。vptr = `QRasterPaintEnginePrivate`+`0x10`（一致）。+8=0。+0x10 = engine q_ptr（eh の本体）。+0x18 = **`QRegion::shared_empty`**（最初の QRegion）。2× UR ok。`parent_ok=1`、resident reget。private は ctor 済み。
  - **dv (d_ptr+0x220 device):** シリアル緑。hex **`0x16015688`** が 2 回。pd / sc / qh の埋め込み QImage と同一。ctor の `QPaintDevice*` スロットは生きている。`QRasterPaintEngine::begin` は vptr+`0x10` = **`0x2e10fc0`**（非 null）、`end` は +`0x18` = `0x2dfbc10`。2× UR ok。`parent_ok=1`、resident reget。RIP=0 は device 欠落でも begin スロット欠落でもない。
  - **nb (d_ptr+0x228/230/238):** シリアル緑。hex **`0x165cd328` / `0x165cd2d8` / `0`**。`init()` 実測: +0x228 = `new(0x158)`（span/state）、+0x230 = `new(0x40)`（**QRasterBuffer**、+0x18 に `QColorSpace`）。+0x238 は init が触らず 0（`QPainter*` 候補、begin 前は正常）。2× UR ok。`parent_ok=1`、resident reget。rasterBuffer 欠落ではない。
  - **rb (QRasterBuffer 先頭 4 qword):** シリアル緑。hex **`0` / `0` / `4` / `0`**。Qt 6.8 実測: +0/+8 は mono/destColor/compositionMode=0（SourceOver）。+0x10 = **format=`4` RGB32**。+0x18 = QColorSpace d=0。2× UR ok。`parent_ok=1`、resident reget。
  - **r2 (QRasterBuffer +0x20..+0x38):** シリアル緑。hex **`0x30000000400` / `0x1000` / `4` / `0x160157a8`**。+0x20 = **m_width=1024, m_height=768**。+0x28 = **bytes_per_line=4096**。+0x30 = **bytes_per_pixel=4**。+0x38 = **`m_buffer=0x160157a8`** = `g_d3_fb_bits`。0x40 オブジェクトは prepare 済み。RIP=0 は scanline 欠落ではない。2× UR ok。`parent_ok=1`、resident reget。
  - **sp (QSpanData 先頭 4 qword):** シリアル緑。hex **`0` / `0` / `0` / `0`**。+0 `rasterBuffer*`=0（隣の prepare 済み QRasterBuffer は未結び）。+8 **`blend`=0**（ProcessSpans。cpd の RIP=0 と一致）。+0x10 `unclipped_blend`=0。+0x18 `bitmapBlit`=0。`new(0x158)` のあと `QSpanData::init` / `setup` / `adjustSpanMethods` は未実行。2× UR ok。`parent_ok=1`、resident reget。
  - **s2 (QSpanData +0x20..+0x38):** シリアル緑。hex **`0` / `0` / `0` / `0`**。+0x20 `alphamapBlit`=0。+0x28 `alphaRGBBlit`=0。+0x30 `fillRect`=0。+0x38 `m11`=0。blit メソッド表は空。2× UR ok。`parent_ok=1`、resident reget。
  - **s3 (QSpanData +0x80..+0x98):** シリアル緑。hex **`0xffffffffffffffff` / `0` / `0` / `0`**。+0x80 `clip*`=**-1**（`QRegion::shared_empty` `0x405ef80` ではない。未初期化／番兵）。+0x88 type/txop=0（`None`）。+0x90 `tempImage*`=0（ctor どおり）。+0x98 solidColor 先頭=0。QSpanData は `new`+ctor のみ。thunk は jz_priv 省略（priv 既証）で 217+stamp=224。2× UR ok。`parent_ok=1`、resident reget。
  - **e2 (d_ptr+0x200..+0x218):** シリアル緑。hex **`0` / `0x165cd178` / `0` / `0xffffffffffffffff`**。device は +0x220 なのでこの 4 qword は **QPaintEngineExPrivate 末尾**（`QStroker` / `StrokeHandler*` / `QPen` / `QRect exDeviceRect`）。+0x208 は生きた heap。+0x218=`-1` は QRect 番兵または未初期化。2× UR ok。`parent_ok=1`、resident reget。
  - **sh (*(d_ptr+0x208) 先頭 4 qword):** シリアル緑。hex **`2` / `0x3ff0000000000000` / `0x165cd108` / `0x1000000000`**。+0=`2`、+8=**IEEE 1.0**、+0x10=heap（オブジェクトより 0x70 前）、+0x18 上位 dword=`16`。vptr なし。Qt 6.8 `QDataBuffer` は capacity/siz/buffer 順なので siz 位置の 1.0 は不一致。`StrokeHandler` 未確定。RIP=0 の blend 欠落とは別物。2× UR ok。`parent_ok=1`、resident reget。
  - **h2 (同じオブジェクト +0x20..+0x38):** シリアル緑。hex **`0x40` / `0` / `0` / `0`**。+0x20=`64`。+0x28 以降は 0。2× UR ok。`parent_ok=1`、resident reget。
  - **hb (`*(d_ptr+0x208)+0x10` 先頭 4 qword):** シリアル緑。hex **`0x100000001` / `0xffff00000001` / `0` / `0x3ff0000000000000`**。sh/h2 と合わせて `*(d_ptr+0x208)` は **`QPenPrivate`**（`strokerPen(Qt::NoPen)`）: ref=2、width=1.0、`QBrush*`、style=`NoPen`、cap=`SquareCap(0x10)`、join=`BevelJoin(0x40)`、dash 空、dashOffset=0。+0x10 は **`QBrushData`**: ref=1、style=`SolidPattern(1)`、`QColor` Rgb 不透明黒（alpha=`0xffff`）、`QTransform` m11=1.0。ExPrivate 末尾であり **blend 欠落とは無関係**。thunk 208+stamp=215。2× UR ok。`parent_ok=1`、resident reget。
  - **hx (同じ QBrushData +0x20..+0x38):** シリアル緑。hex **`0` / `0` / `0` / `0x3ff0000000000000`**。m12=0、m13=0、m21=0、m22=1.0。**identity `QTransform`**。stroker `QPen`/`QBrush` は ctor 済み。blend は埋まらない。thunk 209+stamp=216。2× UR ok。`parent_ok=1`、resident reget。
  - **bg (QPainter ctor = begin 相当):** シリアル緑。`QPainter::QPainter(QImage*)`（`0x2e2bd30`）は戻る。hex **`0x165cd630` / `0`**。engine+8 **state が非 null**（eh では 0 だった。`setState` 済）。`d_ptr+0x228` の heap `QSpanData.blend` はまだ 0。`render()` は未使用。thunk 206+stamp=213。2× UR ok。`parent_ok=1`、resident reget。
  - **px (ctor + markDirty + buildRenderList + renderNodes):** シリアル緑。`QSGSoftwareRenderer::render()` は呼ばない（cpd の入れ子 ctor / RIP=0 を避ける）。`renderNodes` は sret `QRegion`（rdi=dest, rsi=renderer, rdx=painter）。2× UR ok。`parent_ok=1`、resident reget。thunk 193+stamp=200。
- **画面:** compositor が SHM blit のあと机の顔 + 52px バー + **Start パネル**を重ねる。シリアル `[wl] desk face` / `[wl] host bar` / **`[wl] Start open`**。`g_start_open=1`（boot でパネル表示。`desk_click` / `hit_start` でトグル。QMP マウスは使わない）。**D3 はクライアント SHM を FB に blit しない**（白いカード + EX/VW/TE のちらつき / 残像の原因だった）。reget は `/tmp/wlXX` を吸うだけ。カーソルは poll のときだけ動かす。PPM: wall **394111** / panel **146031** / bar **28672** / **start_on 2180** / start(閉)=0。`parent_ok=1`、resident reget、EXCEPTION なし。スクショ `d3-sg-start.png` / `d3-sg-parent.png`。製品 QML stand-in は 8 のまま。**やらない:** `paint_windows` を reget に戻す（PF CR2=`0x40`）。**やらない:** reget から `desk_click` ワンショット（子 `main entry` 直後 PF CR2=`0x1588530` RIP=`0x2825628`）。
- **W9a PASS:** compositor が `get_registry` に `wl_seat` を載せる。缶詰クライアントが name=4 を bind し `get_pointer` / `get_keyboard`。シリアル `[wl] seat advertise` / `[wl] bind seat` / `[wl] seat caps` / `[wl] seat get_pointer` / `[wl] seat get_keyboard`。上流 qtwayland ではない。確認: `bash tools/_tmp_w9a_seat_smoke.sh`。
- **W9b PASS:** `get_pointer` のあと `wl_pointer.enter` + `motion` + `frame`（デスク surface id=5）。カーソルループの実マウスは button（BTN_LEFT）も送る。シリアル `[wl] pointer enter` / `[wl] pointer motion`。`[wl] pointer button` は実クリック時のみ（ヘッドレスでは出ない）。クリックの hit-test / `desk_click` はまだ compositor 側。`SCM_RIGHTS` ではない。確認: `bash tools/_tmp_w9b_pointer_smoke.sh`。
- **W9c ソース PASS / 日次 ISO は戻した:** スモークでは `[SCM] rights send` → `[SCM] rights recv` → `[wl] scm rights ok`。そのカーネルを日次に載せると机が止まる（壁紙のみ、`[wl] pointer enter` まで来ない）。起動カーネルは `kernel/kernel.elf.d2c-d3-wayland`（`bfree-desk-wayland.iso` から抽出）。W9c バイナリ退避は `kernel/kernel.elf.w9c-scm`。`wl_scm_probe()` は起動パスから外した。確認: `bash tools/_tmp_restore_d2c_kernel.sh`。`kernel.elf.g1-desk` は上書きしていない。
- **W9d PASS:** `get_keyboard` のあと `wl_keyboard.enter` + `modifiers`。keymap fd は送らない。ライブ `keyboard key` はカーソルループから外した（クリックが重い原因）。Terminal 入力は compositor `term_key`。確認: `bash tools/_tmp_w9d_keyboard_smoke.sh`（persist.img はロックするのでヘッドレスは付けない）。
- **クリック:** アイコン / ピンは Start パネルより先に `win_open`。押下は `desk_click` のみ（ソケットへ motion/button を書かない）。クリックした周は resident reget を飛ばす。
- **W9e PASS:** compositor が `get_registry` に `wl_output` を載せ、bind のあと geometry / mode / scale / done。シリアル `[wl] output advertise` / `[wl] bind output` / `[wl] output geometry` / `[wl] output mode` / `[wl] output done`。上流 qtwayland ではない。SCM ではない。確認: `bash tools/_tmp_w9e_output_smoke.sh`（persist.img はロックするのでヘッドレスは付けない）。
- **W9f PASS:** `xdg_wm_base` bind のあと ping、缶詰クライアントが pong。シリアル `[wl] bind xdg_wm_base` / `[wl] xdg ping` / `[wl] xdg pong`。上流 qtwayland ではない。SCM ではない。確認: `bash tools/_tmp_w9f_xdg_ping_smoke.sh`（persist.img はロックするのでヘッドレスは付けない）。
- **W9g PASS:** `wl_shm` bind（name=2）のあと `format` を XRGB8888 と ARGB8888 で送る。シリアル `[wl] bind shm` / `[wl] shm format`。缶詰クライアントはタイル SHM のまま。上流 qtwayland ではない。SCM ではない。確認: `bash tools/_tmp_w9g_shm_format_smoke.sh`（persist.img はロックするのでヘッドレスは付けない）。
- **W9h PASS:** `get_toplevel` のあと `xdg_toplevel.configure`（1024×768、states 空）と `xdg_surface.configure`（serial=1、缶詰 ack と一致）。シリアル `[wl] xdg configure` / `[wl] xdg ack_configure`。上流 qtwayland ではない。SCM ではない。確認: `bash tools/_tmp_w9h_xdg_configure_smoke.sh`（persist.img はロックするのでヘッドレスは付けない）。
- **W9i PASS:** `wl_display.sync` に `wl_callback.done`。シリアル `[wl] display sync` / `[wl] callback done`。上流 qtwayland ではない。SCM ではない。確認: `bash tools/_tmp_w9i_display_sync_smoke.sh`（persist.img はロックするのでヘッドレスは付けない）。
- **W9j PASS:** `wl_surface.frame` を commit で `callback.done`。シリアル `[wl] surface frame` / `[wl] callback done`。上流 qtwayland ではない。SCM ではない。確認: `bash tools/_tmp_w9j_surface_frame_smoke.sh`（persist.img はロックするのでヘッドレスは付けない）。
- **W9k PASS:** commit のあと `wl_buffer.release`。シリアル `[wl] buffer release`。上流 qtwayland ではない。SCM ではない。確認: `bash tools/_tmp_w9k_buffer_release_smoke.sh`（persist.img はロックするのでヘッドレスは付けない）。
- **W9o PASS (throwaway):** compositor `bind` name=1 を記録。keymap fd（format=0）。ISO `bfree-w9o-try.iso`。シリアル `[wl] bind compositor` / `[wl] keyboard keymap` / `desk face`。日次未注入。確認: `bash tools/_tmp_w9o_smoke.sh`。
- **W9p PASS (throwaway):** keymap のあと `wl_keyboard.repeat_info`（rate=25, delay=600）。`create_region` / region op / surface `damage` / opaque / input は記録して無視。ISO `bfree-w9p-try.iso`。シリアル `[wl] keyboard repeat` / `desk face`。缶詰は当時 `create_region` を送らない。日次未注入。確認: `bash tools/_tmp_w9p_smoke.sh`。
- **W9q PASS (throwaway):** 缶詰が region id=21 を `create_region` してすぐ `destroy`。親が `wl_display.delete_id` を返す。ISO `bfree-w9q-try.iso`。シリアル `[wl] create_region` / `[wl] destroy` / `[wl] delete_id` / `desk face`。日次未注入。確認: `bash tools/_tmp_w9q_smoke.sh`。
- **W9r PASS (throwaway):** `set_buffer_scale` / `set_window_geometry` / `pool.resize` / `set_cursor`。transform / damage_buffer も no-op。ISO `bfree-w9r-try.iso`、カーネル `kernel.elf.w9m-parent`。シリアル `[wl] surface scale` / `[wl] xdg geometry` / `[wl] pool resize` / `[wl] set_cursor` / `desk face` / `host bar`。日次未注入。確認: `bash tools/_tmp_w9r_smoke.sh`。
- **W9s PASS (throwaway):** `xdg_toplevel.set_app_id` / `set_min_size` / `set_max_size`。set_parent / move / resize / max / fullscreen / popup / positioner も no-op。ISO `bfree-w9s-try.iso`。シリアル `[wl] xdg app_id` / `[wl] xdg min_size` / `[wl] xdg max_size` / `desk face` / `host bar`。日次未注入。確認: `bash tools/_tmp_w9s_smoke.sh`。
- **W9t PASS (throwaway):** `wl_subcompositor` を name=6 で advertise/bind。`get_subsurface` / seat `get_touch` / `release` / surface `offset` も no-op。ISO `bfree-w9t-try.iso`。シリアル `[wl] subcompositor advertise` / `[wl] bind subcompositor` / `desk face` / `host bar`。日次未注入。確認: `bash tools/_tmp_w9t_smoke.sh`。
- **W9u PASS (throwaway):** `zxdg_decoration_manager_v1` を name=7 で advertise/bind。`get_toplevel_decoration` のあと `configure` mode=1（server_side）。ISO `bfree-w9u-try.iso`。シリアル `[wl] decoration advertise` / `[wl] bind decoration` / `[wl] get_decoration` / `[wl] deco configure` / `desk face` / `host bar`。日次未注入。確認: `bash tools/_tmp_w9u_smoke.sh`。
- **W9v PASS (throwaway):** `wp_viewporter` を name=8 で advertise/bind。`get_viewport` / `set_destination`（`set_source` も no-op）。ISO `bfree-w9v-try.iso`。シリアル `[wl] viewporter advertise` / `[wl] bind viewporter` / `[wl] get_viewport` / `[wl] viewport dest` / `desk face` / `host bar`。日次未注入。確認: `bash tools/_tmp_w9v_smoke.sh`。
- **qt_wl_real tiny PASS:** Qt 無し 1080B `_start`（`0x400000`、`-mno-sse`）。シリアル `[wl] execve p8test.elf` → `[qt] real wayland LINK_OK` → `[wl] vfork parent`。38MB/44MB の PF は exec/vfork ではない。
- **qt_wl_real Gui-only PASS:** `qt_wl_real_gui.elf` 36.2MB。desktop.ld + Gui/Core。**no** WaylandClient / **no** `libqwayland-generic.a`。compat は `BFREE_THROWAY_SKIP_DESKTOP_VA`（desktop BSS `0x5262f80` / holder gap を書かない）。`main` のあと `exit_group`（231）。ISO `bfree-qt-wl-real-try.iso`。確認: `bash tools/_tmp_qt_wl_real_smoke.sh`。`qt_wl_hello.elf` / 日次 / g1-desk 未タッチ。38MB フル ELF は再実行しない。
- **qt_wl_real Gui init_array PASS:** 同じ Gui ELF で `BFREE_SKIP_GUEST_INIT_ARRAY` を外した。シリアル `init_array: C runner begin` → ctor `[0]`…`[0x1b]`（28 個）→ `C runner end` → `LINK_OK` → `[wl] vfork parent`。holder gap zero / `pin_global_guard` は throwaway では skip。プラグイン / WaylandClient はまだ無し。
- **qt_wl_real plugin-only link FAIL:** `libqwayland-generic.a` を `--whole-archive`（`BFREE_QT_WL_REAL_PLUGIN=1`）。`libQt6WaylandClient.a` / wayland-client / ffi は無し。undef: `QtWaylandClient::QWaylandIntegration::QWaylandIntegration()` と `init()`。
- **qt_wl_real plugin+WaylandClient PASS:** 同じ throwaway に `libQt6WaylandClient.a`（whole しない）+ `libwayland-client.a` / cursor / ffi。ELF 38.2MB。シリアル ctor `[0]`…`[0x1c]`（29 個、Gui 時より +1）→ `C runner end` → `LINK_OK` → `[wl] vfork parent`。desktop TLS VA skip。`QGuiApplication` はまだしない。確認: `bash tools/_tmp_qt_wl_real_smoke.sh`。日次 / g1-desk / hello / desktop.elf 未タッチ。古い 38MB PF は TLS VA であり、プラグイン自体ではなかった。
- **qt_wl_real QGui ctor PASS:** hybrid スタック + `bfree_wl_create_platform_integration`（stub QPA）。シリアル `QPA dispatcher ok` → `QGuiApplication ctor ok` → `real qgui fill` → `flush skip` → `exit_group` → `[wl] vfork parent`。Unix dispatcher / `processEvents` / qApp parent なし。当時 `store.flush` は shm/wire のあと PF CR2=`0xB00080006`（768 バイト不足。下の flush PASS で解消）。
- **qt_wl_real QGUI C flush PASS:** `store.flush` ではなく `wl_stub_flush_app` 直呼び。シリアル `flush enter` → shm → wire → `flush ret` → `flush` → `exit_group` → `[wl] vfork parent`。wire バッファを `WL_CLIENT_MSG_MAX` 1024 に拡大（W9q–W9v で缶詰が ≈860）。ISO `bfree-qt-wl-real-try.iso`。確認: `bash tools/_tmp_qt_wl_real_smoke.sh`。`qt_wl_hello.elf` / 日次 / g1-desk / desktop.elf 未タッチ。
- **Next:** `QBackingStore::flush`（hello と同じ Qt ラッパ）。`createUnixEventDispatcher` / `processEvents` / 本物 `QWaylandIntegration` はまだしない。desktop リンクは「リンクして」。日次注入は「日次に載せて」。
- **日次 gtk SCM PASS:** ユーザーが `/tmp/bfree_wayland_desk.log` で `[wl] client accepted` / 2× `[wl] pointer enter` / `[wl] desk face` / `[wl] host bar` を確認（2026-08-29）。`create_pool fd parent` は probe=0 なので出ない。カーネルは `kernel.elf.w9m-parent`。退避 `bfree.iso.pre-w9m-scm` / `kernel.elf.d2c-d3-wayland`。`kernel.elf.g1-desk` は未タッチ。
- **W9m first hop PASS (throwaway):** vfork 後の親 socketpair `recvmsg n=0` は、pipe write/read が as-copy 以外でも子へ coop yield し、子の nr 1002 が親 rax を 0 にしていた。`g_guest_fork_was_as_copy` のときだけ yield。
- **D2c+SCM / W9l:** gtk で日次と捨て ISO が両方動く。`create_pool fd` は vfork **前**。親 socketpair と live accept は vfork **後**も捨て ISO で通った。日次は `kernel.elf.d2c-d3-wayland`（SCM なし）。
- **やらない:** N2 退避 ISO の削除、上流 qtwayland リンク、`SCM_RIGHTS` を日次 g1-desk へ、GPU / G1–G3 を W9 より先、fork(57)、`kernel.elf.g1-desk` 上書き。Qml always-true from boot。guest_main を再太らせて UR。末尾 TU に Qt を入れて UR。wrap-first。
- **やらない:** 日次 ISO 全上書き（compositor.elf 注入は可）、上流 qtwayland、GPU、fork(57)、`kernel.elf.g1-desk` 上書き。Qml always-true from boot。QDebug in-place。QGui heap dummy を BSS にしない。mutex ごとの kick を戻さない。worker ppoll 直前 yield を戻さない。V4 ctor の C1+C2 同時 wrap を戻さない。ExecutableAllocator C2 wrap を戻さない。engineSerial が 1 のとき flip しない。Qml 再ビルドで import / getenv patch しない。aligned-new wrap を「先頭」として再試行しない。C1 中に 0x18/0x8 を box しない（newsz 前）。QJSEngine C1 wrap を成功条件にしない。osalloc real / RWX mmap を呼ばない。`registerHelper` / `normalizedType` を skip しない。同 TU の QHash / hasconv を再 wrap しない。QReadWriteLock unlock / `__cxa_guard_acquire` を depth 無しで全域ログしない。converter 登録を skip しない。seqconv を depth 無しで全域ログしない。`registerConverterFunction` / `qHash` を skip しない。malloc を depth 無しで全域ログしない。realloc を `return 0` のまま放置しない（fallback 取り直しは可）。InternalClass::init を skip しない。changeVTableImpl を skip しない。addMemberImpl を skip しない。`asPropertyKeyImpl` を skip しない。addMember を skip しない。addEntry を skip しない。PropertyHashData を skip しない。`patch_qv4internalclass_addmember_guest.sh` / `patch_qv4propertyhash_guest.sh` 等の Qml ソースパッチを当てない。

---
---

