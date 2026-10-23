# Mac Bridge branch (`mac-bridge`)

This branch is the firmware of **[Jax StopWatch](https://github.com/BTCUP99/jax-stopwatch)**: push-to-talk
from an M5Stack StopWatch to Codex and webhook agents through a Mac. It is based on upstream
[Corvelis/stackchan-pet-fw](https://github.com/Corvelis/stackchan-pet-fw) v0.4.1 (commit `7ea267d`) and
targets the `m5stack-stopwatch-release` environment.

| Change | Files |
| --- | --- |
| USB / Wi-Fi link to the Mac: saved networks, LAN probe with a pinned certificate, WSS through Tailscale Funnel (ISRG roots), mbedTLS in PSRAM | `src/NetLink.*`, `src/NetLinkRootCa.h` |
| IMA ADPCM audio across the internet path | `src/Adpcm.h` |
| Avatar sets uploaded over USB to `/av/<id>/`, switching and a happy face | `src/AvatarStore.*`, `src/FaceController.*` |
| Home screen: clock on top, assistant / link / battery status row, X notice badges | `src/FaceController.*` |
| Mac Bridge commands, button mapping (hold A to talk, B to switch / stop), upstream interactions off | `src/main.cpp`, `src/config.h` |

Credentials and avatar writes are accepted over USB only. The upstream unauthenticated Wi-Fi
servers and setup access point stay off. Commands and protocol: see
[docs/architecture.md](https://github.com/BTCUP99/jax-stopwatch/blob/main/docs/architecture.md).

```sh
pio run -e m5stack-stopwatch-release
```

Upstream's MIT license and third-party notices apply unchanged.
