# NXStream

![Platform](https://img.shields.io/badge/platform-Nintendo%20Switch-e60012)
![Language](https://img.shields.io/badge/language-C-blue)
![License](https://img.shields.io/badge/license-GPL--3.0-green)
![Status](https://img.shields.io/badge/status-early%20development-orange)

**Xbox Remote Play client for Nintendo Switch (homebrew).** Stream your own Xbox Series/One console to your Switch over your local network.

> ⚠️ Early development. The current build signs in to your Microsoft account and lists your consoles — streaming is not implemented yet.

## Goals

- Reliable home streaming from your own Xbox on the same network
- Automatic reconnection after the Switch goes to sleep or the network drops
- Robust input handling (no disconnects under heavy input)

## Roadmap

| Phase | Scope | Status |
|-------|-------|--------|
| 0 | Build pipeline + diagnostics app (title mode, network, sleep/resume events) | ✅ Done |
| 1 | Microsoft account sign-in + console discovery | ✅ Done |
| 2 | Streaming session + video on screen | ⏳ Next |
| 3 | Controller input + audio | — |
| 4 | Reconnection (sleep/resume, network loss, session expiry) | — |
| 5 | Extras (rumble, settings, polish) | — |

## Documentation (Portuguese)

- [Project plan](docs/PLANO.md): goals, decisions, phases, risks
- [Architecture](docs/ARQUITETURA.md): modules, threads, session state machine, libraries
- [Protocol](docs/PROTOCOLO.md): how Xbox Remote Play works (auth, API, WebRTC)
- [Development](docs/DESENVOLVIMENTO.md): building, testing on the Switch, logs, conventions

## Download

Every push is built automatically by GitHub Actions. Open the latest run under the **Actions** tab and download the `nxstream-nro` artifact.

## Usage

1. Copy `nxstream.nro` to `/switch/nxstream/` on your SD card.
2. Launch it in **title mode** (hold **R** while launching a game). Applet mode (album) is not supported.

## Building locally

Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with `switch-dev` installed.

```sh
make
```

## License

[GPL-3.0](LICENSE). Derivative works must remain free and open source.

## Disclaimer

NXStream is unofficial homebrew software and is **not affiliated with, endorsed by, or supported by Microsoft Corporation or Nintendo Co., Ltd.** Xbox is a trademark of Microsoft Corporation. Nintendo Switch is a trademark of Nintendo Co., Ltd. These names are used for identification purposes only.

This software is provided "as is", without warranty of any kind. Use at your own risk.
