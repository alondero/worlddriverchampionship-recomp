# Android scaffold

The Gradle project is present so package identity, minimum API level, and
signing/release conventions can be established early. Native dependency
fetching, ROM import, Vulkan loader wiring, touch controls, and APK packaging
remain gated until the desktop WDC runtime bridge is implemented and tested.

Do not put a ROM or save data in `android/` or an APK. The release workflow
must remove the private ROM before packaging and should inspect the APK for
forbidden game-data extensions.
