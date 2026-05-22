# GirlBerg 🌸

Goldberg Steam Emulator fork with CustomAero overlay.

Based on [gbe_fork](https://github.com/Detanup01/gbe_fork) with custom overlay frontend.

## Features
- All gbe_fork features (SDK 1.60+, all Steam interfaces)
- CustomAero overlay with custom colors, themes, and UI
- Build via GitHub Actions (Windows)

## Building
```bash
git clone --recursive https://github.com/AriCuteGirl/GirlBerg.git
cd GirlBerg
premake5 vs2022
msbuild build/project/vs2022/x86_64/Goldberg-Emulator.sln /p:Configuration=Release /p:Platform=x64
```
