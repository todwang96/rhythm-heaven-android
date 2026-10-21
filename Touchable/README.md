# Touchable

**English** | [简体中文](README.zh-CN.md)

Touchable brings ported games a **native iOS experience**, with better touch controls, virtual buttons, graphics, and performance. **Bring your own Switch game files**, and build native iPhone and iPad games on an Apple Silicon Mac.

## General Improvements

Every supported game gets these improvements:

- 🫧 **Liquid Glass controls**: on-screen buttons use the iOS Liquid Glass style.
- 📳 **HD rumble**: precise vibration through the Taptic Engine or a connected controller.
- 🎮 **Game controllers**: play with a physical controller.
- 📱 **iPhone and iPad**: one app runs full screen on both.
- 🚀 **Game Mode**: native iOS and iPadOS Game Mode gives the game priority for performance.
- 🌐 **Language setting**: choose the game language per app in iOS Settings.

## Exclusive Improvements

| Game | Game file version | Improvements |
| -------- | ------------------------ | ------------------------------------------------------------ |
| Rhythm Heaven | v0 (nsp) | Touch controls adapted to each level; fewer on-screen buttons that cover less of the picture; more intuitive play and feedback; ambient border glow |
| Silksong | v0 (nsp) + v655360 (nsp) | Adaptive screen size; full resolution; 120 Hz on ProMotion screens |

## Get started

1. Install the full Xcode and sign in to your Apple Account in Xcode.
2. Put your `prod.keys` in `MyGameFiles/keys/`.
3. Put each NSP in the folder for its game.
4. Double-click `Touchable.command`.
5. Select a game.
6. Select "Build and install" or "Build IPA only".

The first run needs a network connection. The launcher prepares the local tools.

## Where to find things

| Location | Purpose |
| --- | --- |
| [MyGameFiles](MyGameFiles/README.md) | Put your own NSP files and keys here |
| [IPA](IPA/README.md) | Find the finished install packages |
| [docs](docs/README.md) | Read the user guides |
| [internal](internal/README.md) | See the code and developer tools |

For detailed requirements, see [Supported games](docs/supported-games.md).
For install steps, see [Signing and installation](docs/signing-and-install.md).
If an error occurs, first read [Troubleshooting](docs/troubleshooting.md).
Before development, read [AGENTS.md](AGENTS.md).
This project is based on [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile).

## License and disclaimer

The code is under the [MIT License](LICENSE).

This project is not affiliated with or endorsed by Nintendo, Team Cherry, or any other game publisher.
It does not contain game files, keys, or firmware. You must supply files from games that you own.
Game names, logos, and artwork belong to their owners. The app icons and the Silksong logo in this repository are used only to identify the games.
