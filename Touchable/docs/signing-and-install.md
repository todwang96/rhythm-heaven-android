# Signing and installation

## First setup

1. In Xcode Settings > Accounts, sign in to your Apple Account.
2. Connect the iPhone.
3. On the iPhone, trust this Mac.
4. On the iPhone, enable Developer Mode.
5. Keep the iPhone unlocked.
6. In the launcher, select "Build and install".

A new user must enter the Team ID of the Xcode development team.
The launcher makes a separate Bundle ID for each game.
These settings are saved in `.nitenphone/config.json`.
The settings do not go into Git.
To change the team or the signing method, select "Signing settings" in the menu.

An existing workspace keeps its original Bundle ID.
If a compatible signing provisioning profile exists, the launcher can sign with the local certificate.
The provisioning profile must not be expired.
The local certificate must have its private key.
New signing uses Xcode automatic management.

## Install an existing IPA

1. Double-click `Touchable.command`.
2. Select a game.
3. Select "Install existing IPA".

If more than one iPhone is connected, select the target iPhone.
The launcher checks the IPA checksum and the app identity.
The launcher checks the signature expiry date and the device authorization.
If the app is already on the iPhone, the launcher first backs up Documents.
If the backup fails, the installation stops.
The backup is in `.nitenphone/saves/<game ID>/phone-before-install-*/`.

After installation, open the game on the iPhone.
Make sure that the original save data loads.
A different Bundle ID makes a different app container.
This change needs a separate save migration.
