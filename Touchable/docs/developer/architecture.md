# Runtime boundaries

`internal/tools/workspace.py` reads `internal/games/<id>/game.json` and `profile.json`. The processing tools get the modules, source files, app identity, and output paths from this configuration.

`internal/runtime/common/` supplies the shared Switch services. `internal/platform/ios/` supplies the iPhone display, touch, controller, audio session, and Core Haptics output. The shared source files still use some `ss_` function names to keep the existing interfaces.

`internal/games/<id>/runtime/` supplies the adaptation that each game needs. Each game implements `niten_game_install()` and `niten_game_host_call()`. Fixed module addresses, engine changes, and game menu operations must stay in the game folder. The Silksong display changes and LLDB addresses are kept separately.

The build generates `niten_game_config.h` and the iOS `Info.plist`. Each game generates its own module table, shader folder, and Xcode project. Before generation, the tools check for Bundle ID conflicts. The iPhone save path comes from the game configuration.

The current load procedure uses ARM64 NSO modules. System services, graphics, and engine interfaces still need a check for each game. The same ARM64 architecture does not guarantee that a different game runs directly. A new rewriter or shader procedure needs its own tool dispatch.
