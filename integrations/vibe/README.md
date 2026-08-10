# Vibeモード連携

[日本語](README.md) | [English](README.en.md)

このdirectoryは、Stack-chan Multi-Device ControllerへVibe操作モードを追加するための
追跡対象アダプターです。外部firmwareのコピーは保持せず、次のGit submoduleを固定commitで
参照します。

- StopWatch: GOROmanさんの[VibeWatch](https://github.com/GOROman/vibewatch)
- CoreS3 + ｽﾀｯｸﾁｬﾝ: [VibeStackChan](https://github.com/Corvelis/VibeStackChan)

固定versionは[`upstreams.json`](upstreams.json)とGit submoduleのgitlinkを一致させます。
ビルド時には`prepare_dependencies.py`がsubmoduleのcommitと無変更状態を検証し、親firmwareへ
組み込むために必要なBLE共有・一時停止／再開APIだけを`generated/`へ生成します。
外部submoduleへ直接変更を加えてはいけません。

## 初期化とビルド

```sh
git submodule update --init --recursive
pio run -c integrations/vibe/platformio.ini -e m5stack-cores3-vibewatch
pio run -c integrations/vibe/platformio.ini -e m5stack-stopwatch-vibewatch
```

CoreS3版のVibeモードはVibeStackChanと同じ`FAST`、`FORK`、`SEND`、Agent／Action切り替え、
背面タッチ操作を使用します。背面はタップで録音開始、再タップで録音終了、その後の長押しで
送信します。StopWatch版はVibeWatchの画面・物理ボタン操作を使用します。

## 更新確認

```sh
python3 scripts/check_vibe_upstreams.py
python3 scripts/check_vibe_upstreams.py --remote
```

1つ目はcheckout、gitlink、`upstreams.json`の一致を検証します。2つ目は各upstreamの`main`も
確認します。GitHub Actionsは週1回この確認を行い、更新を検出するとissueを作成します。

依存を更新する場合は、submoduleと`upstreams.json`を同じcommitへ更新し、CoreS3版と
StopWatch版を両方buildしてください。自動的に最新`main`へ追従させないことで、配布buildの
再現性と実機動作を維持します。

## 謝辞とライセンス

BLE HIDによるCodex操作、操作体系、UIおよびビジュアルデザインの基になったVibeWatchを
公開してくださったGOROmanさんに感謝します。VibeWatchとVibeStackChanはいずれもMIT
Licenseです。詳細は各submoduleの`LICENSE`と、このrepositoryの
[`THIRD_PARTY_NOTICES.md`](../../THIRD_PARTY_NOTICES.md)を参照してください。
