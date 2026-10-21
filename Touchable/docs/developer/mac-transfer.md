# Transfer one game to another Mac

Use this procedure to reuse the current iPhone build inputs on another Apple Silicon Mac.
The transfer package contains game code and assets. Keep it private.
The package does not contain original packages, keys, signing identities, provisioning profiles, IPA files, or personal saves.

## Create a package

Select one registered game. Use a new output directory.

```sh
.nitenphone/venv/bin/python internal/tools/package_mac_transfer.py --game rhythm-heaven \
  --output .nitenphone/build/transfers/rhythm-heaven-build15-mac
```

The tool reads paths from `internal/tools/workspace.py` and the selected game manifest.
It checks the input module hashes and Build IDs.
It checks rewritten files against their recorded verification hashes.
It checks the pinned upstream revision and the current upstream patch.
It copies the current upstream source files, including local changes.
It includes only the selected game's source and game data.
It includes the active iOS shader chain. It omits shader capture and translator caches.
It writes a ZIP, a SHA-256 file, a file manifest, and a package report.
It refuses to replace an existing output directory.

## Use the package

1. Copy the ZIP and its `.sha256` file to the new Mac.
2. Install full Xcode. The source workspace uses Xcode 27.0.
3. Open Xcode once.
4. Select Command Line Tools in Xcode Settings > Locations.
5. Sign in with the original Apple account.
6. Extract the ZIP to a local disk.
7. Open the package's `README-FIRST.md`.
8. Run the package's `Open-<Product>.command` file.

The launcher creates a local Python environment with Xcode's Python.
It uses only the Python standard library.
It verifies the packaged files.
It regenerates the Xcode project for the new path.
The generated header search paths preserve spaces in directory names.
It opens that project in Xcode.
It does not download dependencies or rebuild game modules and shaders.
Run the launcher again after moving the extracted directory.

Select the physical iPhone in Xcode. Select the original development team.
Keep the existing Bundle ID and save directory.
Back up phone saves before installing over the current app.
Use Xcode Run to build and install.

Signing remains local to the new Mac.
To reuse an existing signing certificate, transfer its private key with the certificate.
A provisioning profile does not contain that private key.
See [Apple's signing identity procedure](https://developer.apple.com/documentation/xcode/sharing-your-teams-signing-certificates).

## Required checks

1. Run the Python tool tests.
2. Extract the ZIP to a new path with spaces.
3. Run the launcher with `--check-only`.
4. Run an unsigned iPhone SDK build from the extracted copy.
5. Confirm that the archive has one game's data and no signing or key files.
6. Record the ZIP hash and the observed Xcode result.

An unsigned build checks compilation and linking.
It does not check signing, device installation, or gameplay on the new Mac.
Store evidence in `.nitenphone/work/<id>/reports/` and `.nitenphone/build/games/<id>/logs/`.
