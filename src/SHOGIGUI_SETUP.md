# ShogiGUI でこのUSIエンジンを動かす手順（Windows）

このプロジェクトの USI エンジンは [src/usi.exe](usi.exe) です。
ただし、MSYS2(MINGW64) で動くのに PowerShell で落ちる場合、ShogiGUI から起動しても同じ理由で落ちます。

---

## 0. 先に結論（いまの状況だと）

- PowerShell で `usi.exe` が `Exit Code: 1` になる → **ShogiGUI からも起動失敗しやすい**
- 対策はどちらか
  - A) 必要DLLを [src](.) にコピーして `usi.exe` と同じフォルダに置く（推奨・確実）
  - B) ShogiGUI を起動する前に PATH に `C:\msys64\mingw64\bin` を通す（環境依存でおすすめしにくい）

---

## 1. なんで「DLLコピー」が必要？（1の操作の意味）

- `usi.exe` は MinGW(g++) でビルドされた実行ファイルです。
- そのため、実行時に MinGW のランタイム DLL（例: `libstdc++-6.dll` など）が必要になることがあります。
- MSYS2 の MINGW64 シェルでは最初から `C:\msys64\mingw64\bin` が PATH に入っていることが多く、DLLが見つかって動きます。
- PowerShell / ShogiGUI は普通の Windows 環境で起動されるため、DLLが見つからず落ちることがあります。

つまり、あなたの理解どおり「ShogiGUI をローカルWindowsで動かす＝PowerShellと同じ系の環境で起動される」ので、DLL問題が再発しやすいです。

---

## 2. DLLコピー手順（推奨）

1. エクスプローラーで `C:\msys64\mingw64\bin` を開く
2. 次のファイルを探してコピー
   - `libstdc++-6.dll`
   - `libgcc_s_seh-1.dll`
   - `libwinpthread-1.dll`
3. [src](.) に貼り付け（[src/usi.exe](usi.exe) と同じ場所）

これで ShogiGUI から起動できる確率が一気に上がります。

---

## 3. [src/usi_wrapper.bat](usi_wrapper.bat) は何？ なぜ直接 [src/usi.exe](usi.exe) を指定しない？

ShogiGUI の「エンジン登録」で実行ファイルに [src/usi_wrapper.bat](usi_wrapper.bat) を指定するためのラッパーです。

目的は2つです。

### (1) ログを残す
- USIは **stdout がプロトコル本体**です。
- うっかりデバッグ文字列を stdout に出すと、GUIが壊れます。
- ラッパーは **stderr を [src/usi_stderr.log](usi_stderr.log) に保存**します。
  - なので、GUIと壊さずに原因調査ができます。

### (2) 作業ディレクトリを固定する
- GUI から起動すると「カレントディレクトリ」が別の場所になることがあります。
- ラッパーは実行前に必ず [src](.) に移動してから [src/usi.exe](usi.exe) を起動します。
  - 将来、重みファイルなどを相対パスで読むようになっても事故りにくいです。

※直接 [src/usi.exe](usi.exe) を指定しても動くことはあります。
ただ、トラブル時にログが取れず詰みやすいので、最初はラッパー推奨です。

---

## 4. ShogiGUI のエンジン登録（クリック順の目安）

ShogiGUI はバージョンで表記が多少違いますが、だいたい次の流れです。

1. ShogiGUI を起動
2. 「エンジン管理」(または「エンジン設定」) を開く
3. 「追加」
4. 実行ファイルに [src/usi_wrapper.bat](usi_wrapper.bat) を指定
5. エンジン種別は USI（選べる場合）
6. 登録してテスト/対局開始

---

## 5. うまくいかない時の見方（最短で原因に辿る）

### 5.1 まず [src/usi_stderr.log](usi_stderr.log) を開く
- ログが増えていない → 起動できていない（DLL不足/実行ファイルパス/権限）
- ログが増えている → 起動はしている（USI応答・フリーズ・例外などを疑う）

### 5.2 よくある症状
- GUIが「応答なし」「起動失敗」
  - まず DLL を [src](.) にコピーしたか確認
  - それでもダメなら [src/usi_stderr.log](usi_stderr.log) を見てメッセージを貼ってください

---

## 6. 次にやると良いこと（任意）

- PowerShell でも [src/usi_wrapper.bat](usi_wrapper.bat) を叩いてみる
  - `cd src` のあと `./usi_wrapper.bat`
  - これで落ちるなら、ログが [src/usi_stderr.log](usi_stderr.log) に出ます
