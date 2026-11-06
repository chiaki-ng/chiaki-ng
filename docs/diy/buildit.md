# Building the Flatpak Yourself

!!! Warning "For Advanced Users Only"

    This is for advanced users who are comfortable going through the build process themselves, such as those who want to build on top of the changes I have made. For regular users, please install using the [Installation Section](../setup/installation.md){target="_blank" rel="noopener"}.

## Prerequisites

1. A linux operating system (can of course use a VM via VMWare, virtual box, WSL2, etc.)

2. The following packages (install instructions separated by Linux distribution below):

    1. gpg

    2. curl

    3. flatpak

    4. flatpak-builder

    === "Ubuntu/Debian"

        ``` bash
        sudo apt update && sudo apt install -y gnupg curl flatpak flatpak-builder
        ```

    === "RHEL/CentOS"

        ``` bash
        sudo yum update && sudo yum install -y gnupg curl flatpak flatpak-builder
        ```

    === "Arch Linux"

        ``` bash
        sudo pacman -Syy && sudo pacman -S gnupg curl flatpak flatpak-builder
        ```

    === "Steam OS"

        !!! Note

            Steam OS is a read-only filesystem so you will need to temporarily disable this to install flatpak-builder. Additionally, keep in mind that your installed packages may get wiped during a system update and need to be reinstalled following this process again in the future. 

        1. Disable read-only mode

            ``` bash
            sudo steamos-readonly disable
            ```

        2. Init keyring

            ``` bash
            sudo pacman-key --init
            ```

        3. Populate keyring with Arch Linux keys

            ``` bash
            sudo pacman-key --populate archlinux
            ```

        4. Populate keyring with Holo keys

            ``` bash
            sudo pacman-key --populate holo
            ```

        5. Install dependencies

            ``` bash
            sudo pacman -Syy && sudo pacman -S flatpak-builder
            ```

        6. Re-enable read-only mode

            ``` bash
            sudo steamos-readonly enable
            ```

3. [Flathub](https://flathub.org/home){target="_blank" rel="noopener"}, the default flatpak repository

    ``` bash
    flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
    ```

## Building the Flatpak

### Get Flatpak Dependencies

1. Install the necessary [flatpak runtime](https://docs.flatpak.org/en/latest/basic-concepts.html#runtimes){target="_blank" rel="noopener"}, associated [sdk](https://docs.flatpak.org/en/latest/building-introduction.html#software-development-kits-sdks){target="_blank" rel="noopener"}, and base app.

    ```bash
    flatpak install -y flathub org.kde.Platform//6.11 flathub org.kde.Sdk//6.11 flathub io.qt.qtwebengine.BaseApp//6.11
    ```

2. Create a directory for your build files and switch into it

    ```bash
    mkdir -p ~/build-chiaki-ng-flatpak && cd ~/build-chiaki-ng-flatpak
    ```

3. Get the [flatpak manifest file](https://docs.flatpak.org/en/latest/manifests.html){target="_blank" rel="noopener"} for `chiaki-ng`

    ```
    curl -Lo chiaki-ng.yaml https://raw.githubusercontent.com/chiaki-ng/chiaki-ng/main/scripts/flatpak/chiaki4deck.yaml
    ```

4. Get the patch file

    1. 0001-vulkan-ignore-frames-without-hw-context.patch

        ```
        curl -LO https://raw.githubusercontent.com/chiaki-ng/chiaki-ng/main/scripts/flatpak/0001-vulkan-ignore-frames-without-hw-context.patch
        ```

5. Get the Python modules file

    ```
    curl -LO https://raw.githubusercontent.com/chiaki-ng/chiaki-ng/main/scripts/flatpak/python3-modules.yaml
    ```

### Create `gpg` Key for Signing your Builds and Repositories

1. Create the [gpg](https://gnupg.org/gph/en/manual/c14.html){target="_blank" rel="noopener"} key pair

    ``` bash
    gpg --quick-gen-key chiaki-ng-diy
    ```

2. Export public key (private key stays on your machine in your gpg directory) [~/.gnupg by default].

    ``` bash
    gpg --export chiaki-ng-diy > chiaki-ng-diy.gpg
    ```

### Create Flatpak

4. Build the flatpak for `chiaki-ng`

    ``` bash
    flatpak-builder --repo=chiaki-ng-diy --force-clean build chiaki-ng.yaml --gpg-sign chiaki-ng-diy
    ```

    !!! Question "How long :clock: will this take?"

        This build process is compiling first the dependencies and then Chiaki itself with the updates included in `chiaki-ng`. This will take a long while (read: 15+ minutes depending on the resources of the build system itself) the first time it runs. However, since flatpak caches builds, if you make changes to just the `chiaki-ng` repo code and then run a new build, it will import the dependencies from cache and only start building from scratch when it detects the first change in the stack. This results in subsequent builds going much faster than the first build you make.

5. Update static deltas (makes upgrading require less downloaded data for end-users)

    ``` bash
    flatpak build-update-repo chiaki-ng-diy --generate-static-deltas --gpg-sign=chiaki-ng-diy
    ```


## Adding your Own Remote and Installing from It

1. Add the repository you built as a local remote repository

    ``` bash
    flatpak --user remote-add --gpg-import chiaki-ng-diy.gpg chiaki-ng-diy ~/build-chiaki-ng-flatpak/chiaki-ng-diy
    ```

2. Install your self-built `chiaki-ng` flatpak from your new remote

    ``` bash
    flatpak --user install chiaki-ng-diy io.github.streetpea.Chiaki4deck
    ```
