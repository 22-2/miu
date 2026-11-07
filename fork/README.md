# フォークの運用

upstream（[kenjinote/miu](https://github.com/kenjinote/miu)）に追従しながら、独自の変更を足すための決まりと手順です。

## ブランチ

| ブランチ | 役割 |
|---|---|
| `master` | upstream と同じ状態にしておく。独自のコミットは入れない |
| `custom` | 独自の変更を入れた本命。upstream は **merge** で取り込む（rebase しない） |

rebase は履歴を書き換えるので、push のたびに force push が必要になります。merge ならその必要がなく、タグや Releases もそのまま使えます。

## 独自のコードの置き場所

- 独自のコードは `fork/` に置きます。upstream はこのフォルダーを触らないので、衝突しません。
- `fork/EditorFork.inl` には、`struct Editor` に足すメソッドとキー割り当て（`forkHandleKey`）があります。`Source.cpp` からクラスの中で include しています。ASCII だけで書きます（MSVC が `/utf-8` なしで読むため）。
- `fork/ListOps.h` には、Markdown のリスト編集の処理があります。Win32 に依存しないので、`fork/tests/` の単体テストで確かめられます。テストは CI の `test` ジョブで、ビルドの前に動きます。
- `Source.cpp` に入れる変更は、次のフックだけにします。
  - `#include "fork/ListOps.h"`（先頭の include の並びの最後）
  - `#include "fork/EditorFork.inl"`（`openFileFromPath` の宣言の直前）
  - `WM_CHAR` の先頭での `g_editor.forkListEnter()` の呼び出し（Enter 用）
  - `WM_SYSKEYDOWN` と `WM_KEYDOWN` の先頭での `g_editor.forkHandleKey(...)` の呼び出し
  - upstream の Ctrl キー処理の条件に足した `!g_editor.forkSkipCtrlShortcuts`
  - メッセージループでの `g_editor.forkPreTranslateKey(...)` の呼び出し（F11 用）
  - F1 ヘルプの枠を文字量に合わせる処理
- upstream のキーを無効にしたいときは、upstream のコードを消さずに `forkHandleKey` で先に受け取って捨てます。
- ヘルプの文言（`miu.rc` の `IDS_HELP_TEXT`）は、日本語と英語だけ直しています。

## リスト編集

拡張子が `md`・`markdown`・`mdx`・`txt` のファイルと、新規作成した無題のファイルで有効です。コードの中の `- ` や `1.` には反応しません。カーソルが1つのときだけ動きます。

| キー | 動作 |
|---|---|
| Enter | 項目を続ける（`- `、`1. `、`- [ ] `）。カーソルより後ろの文字は新しい項目に移る。空の項目での動きは設定 `blankItemEnter` で選べ、既定では同じ階層にもう1つ空の項目を作る |
| Shift+Enter | 普通の改行 |
| Tab / Shift+Tab | 項目を子ごとインデント / 戻す。インデントの単位は、そのリストで使っているもの（タブか、いちばん浅いスペースの幅）。なければタブ |
| Alt+Shift+↑/↓ | 項目を子ごと、前後の兄弟の項目と入れ替える。リストの外では普通の行移動 |
| Ctrl+Shift+↑/↓ | リストの中でも普通の行移動（選択した行だけを動かす） |
| Ctrl+Shift+Space | 項目を子ごと選択。押すたびに親の項目、最後にリスト全体へ広がる |

- 番号付きリストは、編集のたびに番号を振り直します。ただし `1. 1. 1.` のように全部同じ番号のリストは、そのままにします。
- 子リストとして新しくインデントした番号付きの項目は、1 から始まります。

## 設定

設定は `%APPDATA%\miu\fork.ini` に書きます。Ctrl+, で開けます（ファイルがなければ、全項目と既定値の入ったものを作ってから開きます）。保存するとすぐに反映され、miu の再起動は要りません。ファイルがない間は既定値で動きます。

| セクション / キー | 値 | 既定 | 意味 |
|---|---|---|---|
| `[list]` `blankItemEnter` | `continue` / `outdent` / `remove` | `continue` | 空の項目で Enter を押したとき。`continue` は同じ階層に空の項目を足す。`outdent` は1段戻す（最上位では記号を消す）。`remove` は記号を消す |

- 値の大文字・小文字は区別しません。知らない値や書き間違いは既定値として扱います。
- 設定を足すときは、`fork/EditorFork.inl` の `ForkSettings` と `forkSettings()`、`forkOpenSettings()` のひな形、この表を合わせて直します。
- upstream の miu には設定画面がなく、記録しているのは最近使ったファイル（レジストリ）くらいです。そのため、項目が増えるまではファイルで十分という判断です。

## 最初の準備（clone ごとに1回）

PowerShell でリポジトリの中から実行します。

```powershell
pwsh -File fork/git/setup.ps1
```

次の設定がされます。

- `upstream` リモートの追加
- `Source.cpp` と `miu.rc` 用のマージドライバー（`fork/git/utf16-merge.sh`）
- `git diff` で UTF-8 として表示する設定

この2つのファイルは UTF-16 LE なので、Git はバイナリとして扱い、そのままではマージしません。マージドライバーは、3つの版を UTF-8 に変換して `git merge-file` で合わせ、UTF-16 LE（BOM 付き）に戻します。Git for Windows に入っている `sh` と `iconv` だけを使います。

## upstream の更新を取り込む

```powershell
git fetch upstream
git switch custom
git merge upstream/master
```

- 衝突しなければ、そのままマージコミットができます。
- 衝突した場合は、普通のテキストと同じく `<<<<<<<` から `>>>>>>>` の衝突マーカーが入ります（ファイルは UTF-16 のまま）。直してから `git add` して `git commit` します。
- 衝突しやすいのは、フックを入れた行のまわりだけです。

取り込んだら push し、`master` も upstream にそろえます。

```powershell
git push
git push origin upstream/master:master
```

push すると GitHub Actions の `Build (Windows)` が動き、成果物 `miu-win-x64` に `miu.exe` が入ります。

## リリース

`custom` で `v` から始まるタグを push すると、GitHub Releases に `miu.exe` が載ります。

upstream も `v1.0.48` のようなタグを使っているので、名前が重ならないように「元にした upstream の版 + `-custom.連番`」にします。

```powershell
git tag v1.0.48-custom.1
git push origin v1.0.48-custom.1
```
