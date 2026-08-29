# Windows CI issue

## ベースライン

前回の対象は [失敗した Windows CI job](https://github.com/dskkato/memfd_buffer_backend/actions/runs/33018850980/job/98343883669)、今回の再現結果は [修正後の Windows CI job](https://github.com/dskkato/memfd_buffer_backend/actions/runs/33019827196/job/98347167663) です。前回のベースラインは commit `cd685dd` (`update ci setting`) で、Windows job は次の2段階だけに整理されています。

1. `ros-tooling/setup-ros@0.7.18` で Rolling のバイナリ環境を用意する
2. `ros-tooling/action-ros-ci@0.4.8` で4パッケージをビルドする

今回の修正では、この構成を保ったまま `setup-ros` の Python subprocess に互換 shim を先に読み込ませます。

## 失敗箇所と原因

ログ上、Windows runner (`windows-2022`) と Python 3.12.10 の起動までは成功しています。失敗したのはパッケージの configure/build ではなく、`setup-ros` が内部で実行する `rosdep init` です。

```text
File ".../rosdistro/vcs.py", line 39, in <module>
    from distutils.version import LooseVersion
ModuleNotFoundError: No module named 'distutils'

The process '.../Scripts/rosdep.exe' failed with exit code 1
```

Python 3.12 では標準ライブラリから `distutils` が削除されています（[PEP 632](https://peps.python.org/pep-0632/)）。一方、今回のログでは `setuptools` が 59.8.0 のままで、`rosdistro` 1.0.1 が使用する `distutils.version` を互換提供できませんでした。`setup-ros` の後に置いた手動修正は、この初期化が失敗した時点では実行されないため、今回の原因には届きません。

`setuptools` 60 以降は vendored distutils を含みます（[setuptools の互換説明](https://setuptools.pypa.io/en/stable/deprecated/distutils-legacy.html)）。しかし、[setup-ros 0.7.18 の Windows 用 pip パッケージ定義](https://github.com/ros-tooling/setup-ros/blob/0.7.18/src/package_manager/pip.ts) は `setuptools<60.0` を含むパッケージ群を後からインストールします。実際に修正後のログでも、事前に入れた 81.0.0 が 59.8.0 へダウングレードされていました。よって、単純な事前 upgrade では解決しません。

今回の対策は、runner の一時ディレクトリへ `setuptools>=66.1,<82` を `--target` で配置し、そこに `sitecustomize.py` を作ることです。Python 起動時に `SETUPTOOLS_USE_DISTUTILS=local` と `_distutils_hack.do_override()` を実行します。pip がこの互換用コピーを後から管理・削除しないよう、`setuptools-*.dist-info` だけは除去します。これにより、setup-ros がグローバル環境へ 59.8.0 をインストールしても、`rosdep init` のプロセス開始時には互換用コピーの vendored `distutils` が有効になります。

なお、ROS 2 の [rosdep チュートリアル](https://github.com/ros2/ros2_documentation/blob/rolling/source/Tutorials/Intermediate/Rosdep.rst) は rosdep の Windows 対応を制限事項として記載しています。`action-ros-ci` 自体は Windows では通常の rosdep install をスキップしますが、`setup-ros` の環境初期化に含まれる `rosdep init` が先に走るため、そこだけは回避できません。

## これまで試したこと

Git の履歴と各試行の差分を確認した結果は次の通りです。

| commit / 試行 | 内容 | 結果・判断 |
| --- | --- | --- |
| `47babe6` | Ubuntu の Rolling CI のみ | Linux のベースライン。Windows の検証は未実施。 |
| `efb1ba0` に集約された初期試行 | Windows job、source build、`vcs-repo-file-url`、手動の `setup.bat` 呼び出しなどを追加 | 構成が複雑になり、setup phase の原因切り分けが困難になった。 |
| `79f748d` | source build から Windows の ROS 2 バイナリ配布へ変更 | Windows では公式バイナリを使う方針に整理。 |
| `560fb11`, `3a4cd38` | `empy==3.3.4` を手動インストール | setup 後の Python runtime 対策。今回の `rosdep init` より後なので、今回の失敗は直せない。 |
| `1088163` | `setuptools>=66.1,<82` を手動インストール | 方向性は正しいが、`setup-ros` の後に置かれていたため、setup phase の失敗前に実行されない。 |
| `34ee00e` | `setuptools==66.1.1` にさらに固定 | 同じく setup 後であり、今回の失敗箇所には間に合わない。固定値は依存更新を抑える利点がある一方、まずは互換範囲を検証する方針にする。 |
| `7d7c200` | `extra-cmake-args` から `colcon-defaults` JSON へ変更 | CMake 引数の渡し方としては整理されたが、今回の setup failure とは無関係。 |
| `cd685dd` | 手動 `setup.bat`、`empy`、`setuptools`、Windows 用 colcon defaults を削除し、action の公式設定に簡素化 | 現在のベースライン。ただし Python 3.12 と `rosdistro` の互換問題が残った。 |
| run `33019827196` | setup-ros 前に `setuptools>=66.1,<82` を導入 | 事前ステップは成功したが、setup-ros が後から `setuptools<60.0` を導入して 59.8.0 に戻したため、`rosdep init` が同じ import error で失敗。 |
| run `33021062185` | `PYTHONPATH` の `sitecustomize.py` から `_distutils_hack` を有効化 | runner 初期環境には `_distutils_hack` が無く、準備ステップ自身が `ModuleNotFoundError` で失敗。互換用 setuptools の配置が必要と判明。 |

## 実施した修正

`.github/workflows/ros2-rolling.yml` の Windows job で、`setup-ros@0.7.18` の直前に Python の startup hook を追加しました。

```yaml
- name: Prepare Python compatibility
  shell: pwsh
  run: |
    $compatDir = Join-Path $env:RUNNER_TEMP "python-compat"
    New-Item -ItemType Directory -Path $compatDir -Force | Out-Null
    python -m pip install --target $compatDir --no-deps "setuptools>=66.1,<82"
    Get-ChildItem -Path $compatDir -Directory -Filter "setuptools-*.dist-info" |
      Remove-Item -Recurse -Force
    $sitecustomize = @(
      "import os"
      "os.environ['SETUPTOOLS_USE_DISTUTILS'] = 'local'"
      "import _distutils_hack"
      "_distutils_hack.do_override()"
    )
    $sitecustomize | Set-Content -Path (Join-Path $compatDir "sitecustomize.py") -Encoding utf8
    $pythonPath = $compatDir
    if ($env:PYTHONPATH) {
      $pythonPath = "$compatDir;$env:PYTHONPATH"
    }
    $env:PYTHONPATH = $pythonPath
    "PYTHONPATH=$pythonPath" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
    python -c "import distutils.version; print(distutils.version.__file__)"
```

最後の行は互換性の確認とログの可視化です。ここで失敗すれば ROS 2 のセットアップに進む前に原因が分かり、成功すれば `setup-ros` 内部の `rosdep init` が `distutils.version` を import できる状態になります。

## 次の確認手順

1. この変更を push して、Windows job が `Set up ROS 2` を通過することを確認する。
2. setup を通過した後に、`action-ros-ci` の Windows build が成功するか確認する。
3. ビルド失敗が残る場合だけ、失敗した最初のパッケージとコマンドに絞って追加修正する。
4. `action-ros-ci` の現行実装では Windows の `colcon test` が一時的に無効化されているため、job 名の “build-and-test” に反して現状は主に build の確認になる。この点は setup/build が安定してから、別途 `colcon test` の実行方法を検討する。

Windows の対象 distro は `lyrical` に切り替え、Linux 側の `rolling` は維持する。Lyrical は公式 Windows バイナリが提供される一方、Rolling の Windows バイナリは提供されないためである。`required-ros-distributions` と `target-ros2-distro` はともに `lyrical` とする。Python 3.12 の互換問題に対する startup hook は Lyrical でも必要である。
