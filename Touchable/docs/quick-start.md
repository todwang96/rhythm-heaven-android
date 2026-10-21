# First use

Use an Apple Silicon Mac. Install the full Xcode.
The current deployment targets are macOS 26 and iOS 26.
In Xcode Settings > Locations, select Command Line Tools.
Start Xcode one time. Complete the component installation and license steps that it requests.

1. Put your own `prod.keys` in `MyGameFiles/keys/`.
2. Put the NSP in the applicable game folder.
3. Double-click `Touchable.command` at the top level.
4. Select a game.
5. Select an action.

The first build needs a network connection.
The launcher prepares Python, the unpack tools, and the shader tools.
When signing is necessary, follow the prompts in the window to set the development team.
The completed packages are in `IPA/<game ID>/`.

A normal build checks and reuses valid unpacked and rewritten files.
"Full rebuild from NSP" uses a new local work directory.
It unpacks again, rewrites the modules, and builds the shaders and the app.
A Silksong build also runs a Mac launch check automatically.
The check uses a separate save. The game window can show for a short time.
It keeps the original work directory and saves.

When the operation ends, press Return to close the window.
To cancel during an operation, press Control-C.
Then read [Signing and installation](signing-and-install.md).
