# ネットワークAPIテスト用ビルド手順（MSYS2/MinGW環境例）

## 1. 必要パッケージ
- gcc（MinGW）
- make（任意）

## 2. ビルドコマンド例

```
cd /c/Users/h_kis/Desktop/B-Free-master/Program
mkdir -p build_armtest
cd build_armtest

# ソースを上位ディレクトリから参照
# テスト用バイナリ生成
gcc -I../kernel_arm -I../userland_arm -o net_api_testcases ../userland_arm/net_api_testcases.c ../kernel_arm/net_arm.c

# 実行
./net_api_testcases
```

## 3. 注意点
- includeパスや依存ファイルは適宜調整
- 他のテストも同様にbuild_armtest/配下でビルド・実行可能
- Makefile化も推奨

---

## 4. Makefileサンプル（build_armtest/用）

```
CC=gcc
CFLAGS=-I../kernel_arm -I../userland_arm

all: net_api_testcases

net_api_testcases: ../userland_arm/net_api_testcases.c ../kernel_arm/net_arm.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f net_api_testcases
```

---

## 5. 問題が出た場合
- ファイルパスや依存関係を再確認
- 権限やパーミッションも確認
