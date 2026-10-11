# PR0 — Lambda1VR on Steam Frame

**EXPERIMENTAL.** Source release of the Steam Frame fork of [Team Beef's Lambda1VR](https://github.com/Team-Beef-Studios/Lambda1VR). See the [README](../README.md) for features, controls, required game data and build instructions.

Target: Steam Frame / Lepton Android 11 / ARM64 / SteamVR OpenXR / GLES 3.2.

| Record | Revision |
| --- | --- |
| Earlier hygiene audit | `f3773cb04d004a8d51ae095f0fac8c383d6bbafc` |
| Host-tested source | `b5f1779386801d997cb1afef02bff875d80d61db` |
| Public upstream release lineage | Team Beef v1.7.4 at `99a0d62` |

`bash tools/steam_frame/tests/run_tests.sh` passed: bindings, LAN host options, diagnostics/BMP, eye and pointer math, controller mapping and deploy lock safety. No fresh full APK build was run for this publication update.

The root GPLv3 and engine/SDK notices are retained. `assets/extras.pak` matches inherited Team Beef source, and the shared Android debug key is development build material. Machine-specific `local.properties` files are removed from the tracked tip and ignored; the old upstream configuration remains in history.

Supply the retail Half-Life files from your own copy. Half-Life: Source is incompatible. The source license does not grant redistribution rights to the commercial game. No APK or game-data package is published here.

[Exact metadata](PR0_METADATA.json) · [Changelog](../CHANGELOG.md)
