# Borderlands Online Launcher (Linux)

Linux launcher made with C++, GTK 3 and Glade, it shows the server list from the master server, lets you join a server, and starts `BOL.exe` with Wine or Proton, can also start the local server to play offline.

## What you need

- Fedora: `sudo dnf install gcc-c++ cmake gtk3-devel`
- Ubuntu / Debian: `sudo apt install g++ cmake libgtk-3-dev`
- Arch: `sudo pacman -S gcc cmake gtk3`

To play you also need Wine or Proton (install Proton from Steam or with ProtonUp-Qt).

## Build

```sh
cmake -B build
cmake --build build
./build/bol-launcher
```

The UI (`ui/launcher.glade`) is built into the executable, so you can copy `bol-launcher` anywhere. To change the UI, open `ui/launcher.glade` in Glade and build again.

## Portable AppImage

To get one file that runs on most Linux distros (Ubuntu 20.04 / Fedora 32 or newer and similar), you need `podman` or `docker`:

```sh
./build-appimage.sh
```

It builds inside an Ubuntu 20.04 container and puts GTK and all its libraries inside `Borderlands_Online_Launcher-x86_64.AppImage`. The C++ runtime is linked statically. Users only need to run:

```sh
chmod +x Borderlands_Online_Launcher-x86_64.AppImage
./Borderlands_Online_Launcher-x86_64.AppImage
```

The first build downloads linuxdeploy into `tools/`, later builds reuse it.

## How to use

1. Go to **Options**:
   - Pick `BOL.exe` and the Linux local server (`eridian-server`, it runs natively, not with Wine).
   - Choose Wine or Proton.
   - Change the prefix folder if you want.
   - Press **Save**.
2. In **Servers**, press **Refresh**. Then pick a server and press **Connect**, or double click it.
3. **Play Offline** starts the local server first and, 5 seconds later, the game. The server log is shown live in your system terminal (ptyxis, GNOME Terminal, Konsole, xterm...). Closing that terminal (or the launcher) stops the server.

Options are saved in `~/.config/bol-launcher/settings.ini`.

The default prefix is `~/.local/share/bol-launcher/prefix`.
