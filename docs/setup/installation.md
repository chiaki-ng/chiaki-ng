# Installing `chiaki-ng`

!!! Warning "Only download `chiaki-ng` from official sources"

    The official sources are the [GitHub releases page](https://github.com/chiaki-ng/chiaki-ng/releases){target="_blank" rel="noopener"}, [Flathub](https://flathub.org/apps/io.github.streetpea.Chiaki4deck){target="_blank" rel="noopener"}, winget (`winget install --id=StreetPea.chiaki-ng -e`) and the Homebrew cask below. Other websites offering `chiaki-ng` downloads, such as chiaki-ng.com, are not run by this project, and we can't vouch for the files they serve.

=== "Linux Flatpak"

    !!! Tip "Copying from and Pasting into Konsole Windows"

        You can copy from and paste into `konsole` windows with ++ctrl+shift+c++ (copy) and ++ctrl+shift+v++ (paste) instead of the normal ++ctrl+c++ (copy) and ++ctrl+v++ (paste) shortcuts. In fact, ++ctrl+c++ is a shortcut to terminate the current process in the `konsole`. Additionally, you can still right-click and select copy or paste as per normal.

    === "Using the Discover Store (Recommended)"

        1. Open the Discover store

            ![Open Discover](images/OpenDiscover.png)

        2. Search for `chiaki-ng` in the search bar

            ![Install chiaki-ng](images/InstallChiaking.png)

        3. Click Install

    === "Using the `konsole` (If for some reason it doesn't show up on the Discover store)"

        1. Run the following command in the `konsole`

            ```
            flatpak install -y io.github.streetpea.Chiaki4deck
            ```

    !!! Note "About `chiaki-ng`"

        The above instructions are for the official `chiaki-ng` flatpak on Flathub.
        
        However, you can also build the flatpak yourself (recommended for users who want to add their own source code changes on top of the ones I've made) by following the instructions in [Building the flatpak yourself](../diy/buildit.md){target="_blank" rel="noopener"}.

=== "MacOS via Brew"

    Requires macOS 15 (Sequoia) or later on Apple Silicon.

    1. Install with the following command in the terminal

        ```
        brew install --cask streetpea/streetpea/chiaki-ng
        ```

=== "MacOS/Windows/Linux Appimage Package"

    1. Download the appopriate package from the [releases page](https://github.com/chiaki-ng/chiaki-ng/releases){target="_blank" rel="noopener"} on GitHub (for macOS, use the Apple Silicon `-arm64` package, which requires macOS 15 (Sequoia) or later; Intel `-amd64`/`-x86_64` packages are no longer built)

## If `chiaki-ng` Doesn't Open or Shows a Blank Window

This usually means Vulkan isn't working on your system, for example because of an outdated graphics driver or another program's Vulkan layer (screen recorders and game launchers can install these). If `chiaki-ng` crashes while starting Vulkan, it switches to OpenGL by itself the next time you open it. If it opens to a blank or white window instead, start it once with the `--opengl` option, which switches the renderer to OpenGL from then on:

=== "Windows"

    Open `chiaki-ng (OpenGL)` from the Start menu. With the portable package, run `chiaki.exe --opengl` from a command prompt in its folder.

=== "Linux Flatpak"

    ```
    flatpak run io.github.streetpea.Chiaki4deck --opengl
    ```

=== "Linux AppImage"

    ```
    ./chiaki-ng.AppImage_x86_64 --opengl
    ```

=== "MacOS"

    ```
    /Applications/chiaki-ng.app/Contents/MacOS/chiaki --opengl
    ```

You can switch back to Vulkan later under Settings > Video > Renderer Backend. The OpenGL renderer doesn't support HDR.
