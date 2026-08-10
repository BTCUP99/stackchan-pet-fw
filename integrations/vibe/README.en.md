# Vibe Mode Integration

[日本語](README.md) | [English](README.en.md)

This directory contains the tracked adapter that adds Vibe control modes to the
Stack-chan Multi-Device Controller. It does not keep copied external firmware;
instead, it pins these Git submodules to exact commits:

- StopWatch: GOROman's [VibeWatch](https://github.com/GOROman/vibewatch)
- CoreS3 + Stack-chan: [VibeStackChan](https://github.com/Corvelis/VibeStackChan)

[`upstreams.json`](upstreams.json) and the Git submodule gitlinks must identify
the same commits. During a build, `prepare_dependencies.py` verifies the commit
and clean state of each submodule, then generates only the BLE-sharing and
suspend/resume adaptation required by the parent firmware under `generated/`.
Do not modify either external submodule directly.

## Initialize and Build

```sh
git submodule update --init --recursive
pio run -c integrations/vibe/platformio.ini -e m5stack-cores3-vibestackchan
pio run -c integrations/vibe/platformio.ini -e m5stack-stopwatch-vibewatch
```

The CoreS3 Vibe mode uses the same `FAST`, `FORK`, `SEND`, Agent/Action layer,
and rear-touch behavior as VibeStackChan. Tap the rear sensor to start
recording, tap it again to stop, then hold it to send. The StopWatch build uses
VibeWatch's touchscreen and physical-button controls.

## Check for Updates

```sh
python3 scripts/check_vibe_upstreams.py
python3 scripts/check_vibe_upstreams.py --remote
```

The first command validates the checkout, gitlink, and `upstreams.json`. The
second also checks each upstream `main` branch. A weekly GitHub Actions workflow
opens an issue when it detects an update.

To update a dependency, move its submodule and `upstreams.json` to the same
commit, then build both the CoreS3 and StopWatch environments. Pins do not
automatically follow `main`, preserving reproducible releases and tested device
behavior.

## Acknowledgements and License

The BLE HID controls for Codex, interaction model, UI, and visual design are
based on GOROman's VibeWatch. Thank you to GOROman for making VibeWatch
available. VibeWatch and VibeStackChan both use the MIT License. See each
submodule's `LICENSE` and this repository's
[`THIRD_PARTY_NOTICES.md`](../../THIRD_PARTY_NOTICES.md) for details.
