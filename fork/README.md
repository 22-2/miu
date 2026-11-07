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
- `Source.cpp` に入れる変更は、次のフックだけにします。
  - `#include "fork/EditorFork.inl"`（`openFileFromPath` の宣言の直前）
  - `WM_SYSKEYDOWN` と `WM_KEYDOWN` の先頭での `g_editor.forkHandleKey(...)` の呼び出し
  - upstream の Ctrl キー処理の条件に足した `!g_editor.forkSkipCtrlShortcuts`
  - メッセージループでの `g_editor.forkPreTranslateKey(...)` の呼び出し（F11 用）
  - F1 ヘルプの枠を文字量に合わせる処理
- upstream のキーを無効にしたいときは、upstream のコードを消さずに `forkHandleKey` で先に受け取って捨てます。
- ヘルプの文言（`miu.rc` の `IDS_HELP_TEXT`）は、日本語と英語だけ直しています。

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
