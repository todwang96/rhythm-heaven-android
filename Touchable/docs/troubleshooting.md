# Troubleshooting

## No menu shows after a double-click

Make sure that you use an Apple Silicon Mac.
Make sure that the full Xcode is installed.
In Xcode, select Command Line Tools.
If the file has no execute permission, run this in the project folder:

```sh
chmod +x Touchable.command
./Touchable.command
```

## The NSP or keys are not found

Read the [game file placement instructions](../MyGameFiles/README.md).
Keep the `.nsp` extension.
Name the keys file `prod.keys`.
If there is more than one candidate file, select one in the interactive menu.

## The version or modules do not match

Read the [supported versions](supported-games.md).
Use the necessary base package and update package.
Do not change the hashes in the profile to bypass the check.

## The rewritten files do not match the records

Select "Full rebuild from NSP".
This operation uses a new folder. The old work files and saves stay.

## Signing failed or expired

In Xcode, check the Apple Account, the team, and the device registration.
Build the app again. An expired local provisioning profile cannot be used for installation.

## The iPhone is not found

Connect the iPhone. Unlock the iPhone.
Complete the trust and Developer Mode setup.
Run "Check environment" again.

## The build failed

Read the log path that the window shows.
You can also select "Open logs folder".
When you run the build again, it checks which stages it can reuse.
The first tool preparation needs a network connection.
A failed build does not replace the current IPA.
