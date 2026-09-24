# Building and running the original game (macOS)

The comparison tools need the original game, unmodified, beside this repository:
a plain clone of the open-source Cortex Command Community Project at the commit
the browser port is based on, `20dfb3ea5` (upstream's `development` branch head as
of 2026-09-24), in `../original-code` relative to this repository, or wherever
`ORIGINAL_CODE` points. It is not part of this repository. Nothing in it is
edited: `git status` shows no changes. Everything below only adds ignored build output (`build-mac/`,
`Cortex Command.app/`) and the game's own runtime files (`Userdata/`, logs,
`ScreenShots/`).

## Fetch

From the folder that contains this repository:

```sh
git clone https://github.com/cortex-command-community/Cortex-Command-Community-Project.git original-code
git -C original-code checkout 20dfb3ea57c916e94fdeb94a5cf871c80fd5fc36
```

## Build

GCC 13 from Homebrew, with Meson and Ninja, from `original-code/`:

```sh
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
export MACOSX_DEPLOYMENT_TARGET=15.0
export CC=gcc-13 CXX=g++-13
meson setup build-mac --buildtype=release "-Dcpp_args=-D_Static_assert=static_assert -include cstdlib"
ninja -C build-mac -j 8
```

- `DEVELOPER_DIR`: the Xcode licence is not accepted on this machine; the Command
  Line Tools work without it (`git` needs it too).
- `MACOSX_DEPLOYMENT_TARGET=15.0`: GCC 13 misdetects macOS 27 otherwise.
- `-D_Static_assert=static_assert`: the macOS SDK's C static assertions, for GCC.
- `-include cstdlib`: two bundled RakNet files (`HTTPConnection2.cpp`,
  `Rackspace.cpp`) use `<cstdlib>` functions without including it. Passing the
  include on the command line builds them without editing the source.

About 430 steps; the linker warns about symbol visibility, which is harmless.

## Package

The app is assembled from upstream's own files plus the fresh executable:

```sh
APP="Cortex Command.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"
cp build-mac/Resources/Info.plist "$APP/Contents/Info.plist"      # configured by Meson from Resources/Info.plist
cp Resources/ccicon.icns "$APP/Contents/Resources/"
cp external/lib/macos/libfmod.dylib "$APP/Contents/Frameworks/"
cp build-mac/CortexCommand "$APP/Contents/MacOS/CortexCommand"
install_name_tool -add_rpath @executable_path/../Frameworks "$APP/Contents/MacOS/CortexCommand"
codesign --force --deep --sign - "$APP"
```

The other libraries (SDL3, libpng, minizip, TBB, GCC's runtime) come from
Homebrew, so this app only runs on the Mac that built it. The upstream bundling script could
not rewrite Homebrew's GCC runtime references, which is why it is not used.

## Run

```sh
open "original-code/Cortex Command.app"
```

The app must stay inside `original-code/`, next to `Data/`. The first run writes
upstream's default `Userdata/Settings.ini` (960×540 at 1×, VSync on, intro shown).
If macOS offers to reopen windows after a crash, silence it once with
`defaults write org.cortex-command-community.cccp ApplePersistenceIgnoreState -bool YES`.

## Comparing with the browser port

`tools/parity-probe/` and `tools/native-input/` run this app with their
own settings (864×558 at 1.5×, VSync off, scripted input, a probe mod) and put
`Userdata/Settings.ini`, `Mods/` and `ScreenShots/` back afterwards. Close your
own copy of the game first: they share `Userdata/`. See `notes/parity.md` and
`notes/testing.md`.

An experimental variant of the original (for example without fused
multiply-add) belongs in a scratch build directory,
`meson setup <scratch> original-code …`, so nothing lands in the reference.
