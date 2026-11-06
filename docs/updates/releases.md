# `chiaki-ng` Releases
## Moving to FlatHub

!!! Caution "Legacy Repo Deprecation"

    `chiaki-ng` is now on flathub. If you previously installed `chiaki-ng` via the konsole the flatpak has changed to `io.github.streetpea.chiaki-ng`. The old repo is now **deprecated** in favor of using Flathub since Flathub is added by default to the Steam Deck and the hosting is at no cost to the project (unlike the initial repo). While the software will still be accessible from the old repo, it is encouraged for users to switch over.

To migrate to the flathub repo do the following:

1. Move chiaki-ng's configuration files to their new location

    ``` bash
    mv ~/.var/app/re.chiaki.Chiaki4deck ~/.var/app/io.github.streetpea.Chiaki4deck
    ```

2. Update automatic launcher script to use `io.github.streetpea.Chiaki4deck`

    ``` bash
    sed -i 's/re.chiaki.Chiaki4deck/io.github.streetpea.Chiaki4deck/g' ~/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki/Chiaki-launcher.sh 
    ```

3. Uninstall the existing legacy `chiaki-ng` flatpak

    ``` bash
    flatpak uninstall -y re.chiaki.Chiaki4deck
    ```

4. Install chiaki-ng from flathub using Discover store app or

    ``` bash
    flatpak install -y --user flathub io.github.streetpea.Chiaki4deck
    ```

5. Change properties of non-steam game to point to:

    ``` bash
    /home/deck/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki/Chiaki-launcher.sh
    ```

## Updating `chiaki-ng`

In order to update your already installed `chiaki-ng` to the newest version, either:

=== "Linux Flatpak"

    - Check for updates in `Discover` and update there

        **OR**

    - Update via the `konsole` with:

        === "chiaki-ng Installed Via Discover Store"

            ``` bash
            flatpak update -y io.github.streetpea.Chiaki4deck
            ```

        === "chiaki-ng Legacy Install Via Konsole"

            ``` bash
            flatpak update --user -y re.chiaki.Chiaki4deck
            ```

=== "MacOS Brew"

    - Update via the terminal with:

        ``` bash
        brew install --cask streetpea/streetpea/chiaki-ng
        ```

=== "Windows/MacOS/Linux Appimage"

     Download the appropriate package from the [releases page](https://github.com/chiaki-ng/chiaki-ng/releases){target="_blank" rel="noopener"} on GitHub (for macOS, use the Apple Silicon `-arm64` package; Intel `-amd64`/`-x86_64` packages are no longer built)

## Releases (Newest First)

### 1.10.1

- Fix controller buttons that could stop working or stay held mid-stream, and get adaptive trigger effects from more PS5 games
- Improve stream stability: recover from a lost keyframe instead of freezing, keep PSN connections open through bad packets, resend more lost packets, and fix dropped frames after packet loss
- Add direct frame mapping (on by default) along with frame pacing and renderer fixes, and fix crashes when a session ends or the video queue resets and a VRAM leak while the session is locked
- Bring back Vulkan as the default renderer on macOS, which brings back HDR, with MoltenVK 1.4.2; the macOS app is now signed and notarized, so it opens without a security exception, and it no longer freezes when you quit during a stream
- Keep DualSense rumble working after switching between Bluetooth and USB, start wired haptics as soon as the controller's audio device appears, and keep haptics in step after network stalls
- Fix the microphone cutting out, mostly with quieter mics, and resend each mic frame in the next two packets
- Fix the AppImage crashing at PSN login on distros with a different NSS version and make it use its own SDL3
- Resend the wakeup while the console wakes, fix UPnP discovery, connecting by ports and renaming consoles, and recover incomplete certificate chains for PSN
- Stop writing the login PIN to the session log
- Update libplacebo, SDL, curl and other libraries, move every package to FFmpeg 9.0.2, and move the Flatpak to KDE runtime 6.11 and the AppImages to Qt 6.9.3

### 1.10.0

- Improve streaming quality and stability with better queueing, reset handling, congestion control, and streaming metrics
- Add an OpenGL renderer backend plus major renderer and libplacebo fixes, adaptive queue depth, and custom upscalers
- Add VSync support plus spatial upscaler presets under settings->video->Render
- Improve PSN remote play reliability with configurable NAT port guessing and holepunching fixes
- Rework audio, microphone, and haptics paths for better recovery and fewer failure cases
- Fix macOS ARM rendering, crash-on-exit, and controller input issues, and add iOS/tvOS compilation support

### 1.9.9

- Add wake and sleep functionality to Windows and MacOS
- Allow using custom path for Steam when creating Steam shortcut
- Allow vulkan with amd for HDR on Windows
- Allow disabling keyboard and mouse touch
- Fix psn token generation in Windows build

### 1.9.8

(Windows Only) Bug Fix

- fix remote connection over PSN for Windows

### 1.9.7

Small improvements and bug fixes:

- add Windows Arm build
- add streamer option for people sharing their screen (hiding sensitive info from UI)
- limit filesystem access for flatpak to least necessary privileges
- fix potential freeze when switching between workspaces
- set player index based on data coming from console
- disable fragmentation after senkusha to optimize performance
- fix issue where console could fail to start a session when waiting for login pin
- enable psn hosts to appear immediately after registering a console (instead of only after restarting the program after registration)
- increase max slots as the current slot limit could cause problems at high bitrates

### 1.9.6

Quality of life improvements and bug fixes:

- improve controller mapping by adding support for many more controllers and improving support of existing controllers
- greatly improve success rate for generating remote connection via PSN tokens via in-app browser
- fix issue where console that becomes available right after clicking on it is never recognized as available, causing you to have to relaunch to begin a session
- add option to disable audio and/or video
- add ability to disable or alter stream shortcut
- fix issue where remote connection via PSN fails if UPNP gateway is discovered but mapping fails
- prevent Linux guests from sleeping/turning on screensaver if there is active controller movement
- remove dependency on hidapi-hidraw from appimage
- add native wayland support for flatpak and appimage
- fix delay in adaptive triggers when using DualSense in bluetooth mode
- tweak haptic rumble code for higher accuracy
- add option to set stream stats to always on in audio/wifi
- adjust rumble intensity based off of console setting
- fix address not automatically being filled in for regular registration
- improve DualSense controller rumble
- fix issue where DualSense could lose its haptics/rumble after connecting while low on battery
- allow configuring true haptic intensity manually
- small improvements to settings UI
- change audio size buffer to slider in 10ms increments
- change bitrate to slider in 1 Mbps increments
- change bitrate to default for its resolution when changing resolution
- disable Steam on-screen keyboard in desktop mode on Steam Deck as it performs erratically

### 1.9.5

Bug fix release:

- Make sure popup dialogs don't appear during autoconnect
- Use existing appids from steam shortcuts to prevent losing images and proton prefixes when creating a new shortcut
- Improve motion controller reset detection
- Add steam shortcut creation to Windows msvc build
- Allow adjusting volume in-app
- Resize gui to last set size upon opening + add stream option for adjustable stream window
- Change default bitrate from 30,000 to 15,000 to match the official remote play app's default

### 1.9.4

Small patch update:

- add automatic (pinless) registration for locally discovered consoles
- adjust adaptive trigger and haptic intensity based on the PS5 settings
- Set led (lightbar) colors during gameplay for applicable controllers in games that support this feature
- patch bug causing stream session to fail connecting in certain instances
- reduce app load time
- add controller navigation icons to GUI
- enable zero-copy decoding for hdr on Windows using the AMD radeon driver (was previously disabled due to a driver bug)
- add Winget and Chocolatey support for Windows
- patch bug causing remote connection via PSN to fail in an edge case
- add Windows portable zip release to releases in addition to installer
- change cancelling button for auto-connect and remote connection via psn to back button / escape instead of any button
- add configurable display options
- enable adjusting libplacebo custom renderer preset + display options via stream menu (so you can see the effects while adjusting them)
- add reminder for create steam shortcut on platforms where it's enabled + add remind me option to remote play setup popup
- fix controller shortcut not adding when using the create steam shortcut button and the non-Steam game already exists
- fix unstreamable content message not appearing in some instances
- fix issue causing L2 adaptive trigger to lock in certain games
- fix controller mapping appearing for Steam Virtual controllers
- fix Steam Deck desktop mode controller mode (activated by holding options button) not working with chiaki-ng
- fix Google Stadia controller mapping
- fix controller navigation temporarily losing focus in certain edge cases

### 1.9.3

Small patch update:

- add Linux arm64 appimage
- fix bug that can cause chiaki-ng to fail to start after new PlayStation firmware update if user has a login pin and in some other edge cases

### 1.9.2

Small patch update:

- HDR support for MacOS and Windows
- Add Windows installer
- Increase stability of motion controls
- Add native webview for obtaining PSN tokens, making it possible to obtain the token easily in Steam OS game mode
- Add ability to set custom window resolution
- Fix import/export dialogs to work with appimage and Steam OS game mode
- enable switching between dpad mode and dpad touch emulation mode with any chosen combo up to 4 keys set in settings
- expand motion control reset to work with more games beyond the Resident Evil 4 demo
- Fix issue where session wouldn't close if canceled after waking the console but before connecting
- Fix issue where session may fail to connect if it receives invalid frames initially
- Show settings maximized at start instead of 720p


### 1.9.1

Small patch update:

- Adds dpad touch emulation to use the dpad for touchscreen touches and swipes [see dpad touch emulation](../setup/controlling.md#dpad-touch-emulation){target="_blank" rel="noopener"}
- Add --exit-app-on-stream-exit option to exit `chiaki-ng` immediately after closing a streaming session
- Fix registration issues related to broadcast settings by automatically detecting when broadcast should be used
- Reset motion controls when necessary to prevent jumping to position when using motion controls to aim in games that activate motion controls via a trigger press such as Resident Evil 4 Remake
- Fix bug where upnp discovery could take too long, causing the remote connection via PSN to fail
- Notify users of the possibility of remote connection via PSN as many users aren't aware of this option still
- Add mapping for Share button on Xbox Series and Xbox One Controllers
- Display current profile name with colon after application name as Application Display Name
- Update controller mappings to be portable across all platforms (Linux, Mac/OS, and Windows)
- Add controller name for controller mapping for controllers that don't have a name configured in the mapping itself
- Allow entering controller mapping and reset mapping using the back button
- Make log dialogs, registered consoles, and hidden consoles scrollable with a controller
- Increase STUN timeouts to 5 seconds + add timeouts for curl of 10 seconds
- Make key mapping dialog navigable with controller
- Disable zero-copy for hw cards that don't support it
- Increase wait time for DualSense haptics of DualSense edge to come online to 15 seconds
- Add homebrew cask for `chiaki-ng` for MacOS
- Fix an error causing a crash when random stun allocation was used for remote connection via PSN
- Fix a memory leak in remote connection via PSN

### 1.9.0

Brings ability to set controller mappings for chiaki-ng

- adds **Controllers** section to chiaki-ng settings which allows you to configure the mapping for your controller (especially nice for mapping Xbox and Switch controllers to PlayStation inputs)

    !!!Note "Controllers mapped via Steam"

        Controllers mapped via Steam should be mapped directly in the Steam UI gamepad configurator as opposed to this menu. If you try to map a controller that is mapped via Steam in this menu it will give you a notification that it should be mapped via Steam.

- adds Custom renderer option which allows you to configure your renderer options very granularly with the options at https://libplacebo.org/options/
- add defaults to all settings so users are aware of the defaults/which settings they've changed
- add different haptic rumble intensity settings for users to configure if the default is not to their liking
- for manual connection show 1 pane with all relevant information instead of 2 panes when console is discovered + allow user to choose between registered consoles (regardless of whether or not they are currently discovered) + make PS5 default console type
- disable double click by default and allow re-enabling via the **Video** section of the Settings
- fix crash when user has more than 1 PS5 registered with PSN
- fix crash that could happen when a user uses the wake from sleep feature
- fix corrupt stream that could occur after several hours of streaming
- fix launching from a path with non-ascii characters on Windows
- fix decimal points turning into scientific notation numbers in QSliders
- properly terminate ipv6 discovery service
- ping all network interfaces on Linux and MacOS allowing discovery of previously undiscoverable consoles

### 1.8.1

Small patch update

- enable haptic feedback for DualSense on MacOS (see [enabling haptic feedback for DualSense on MacOS](../setup/controlling.md#enabling-dualsense-haptics-on-macos){target="_blank" rel="noopener"})
- fix issue where sleeping your client device on Linux would cause chiaki-ng to crash
- give user link to use in a browser when using psn login in game mode on Steam Deck
- creating Steam shortcut improvements (i.e., add a timestamp to the backup file so multiple can be saved and not allowing creating another shortcut while currently creating one)
- scale the official Steam icons for chiaki-ng used in the create a Steam shortcut button to the appropriate sizes to fix pause will scrolling through Steam menu with chiaki-ng added as a non-steam game in Steam big-picture mode
- changes MacOS icons to give them the "MacOS" style (i.e., rounded edges, etc.)

### 1.8.0

Name change to chiaki-ng

- changes name and artwork for chiaki-ng
- changes current profile when using --profile option

### 1.7.4

Small patch update

- adds notification when psn creds expire
- use ipv4 for hostnames as ps5/4 don't support using IPv6 for remote play
- allow entering pin using enter key as alternative to selecting ok

### 1.7.3

Adds ability to create/delete and switch between different profiles (i.e., different users)

- Switch, create and delete profiles via Settings (Gear icon)->Config->Manage Profiles. You can also make a shortcut launching from a specific profile with the --profile=profile_name option where profile_name is the given profile's name
- Fix issue where discovery doesn't work in certain setups on Windows
- Fix issue where autoconnect doesn't work with manually added connection on Windows

### 1.7.2

Provides unique remote and local Stream Settings for each console (PS4/PS5)

- Separate Local and Remote and PS4/PS5 settings so you can set appropriate settings for each type of connection
- Enable more network types to work with remote connection via PSN (Note: some can only be made to work a % of the time due to limitations with remote play imposed by Sony)
- Export/import option for settings to transfer across devices/platforms
- Adds `auto` hw decoder option which is now the default. It chooses the best decoder for your platform from the available decoders
- Make chiaki-ng ipv6 compatible (ipv6 not yet supported by remote play on the console [i.e., in Sony's PlayStation firmware] so can't be used yet)

### 1.7.1

Improves + adds PS4 support to remote connection via PSN

- remote connection via PSN now supports PS4 consoles
- remote connection via PSN now supports more network types

### 1.7.0

Enables remote connection via PSN

- Remote connection via PSN now available without the need for port forwarding ([see remote connection docs for more details](../setup/remoteconnection.md#remote-connection-via-psn){target="_blank" rel="noopener"}) on how to setup and use.
- Allow zooming out from resolution for users targeting a resolution beyond their screen resolution (i.e., 1080p on Steam Deck)
- Enable don't fragment for MacOS Big Sur and later for more accurate MTU readings during Senkusha
- Create a fallback session id when session id isn't received instead of erroring out
- Workaround patch for vulkan ffmpeg hw decoder bug affecting Nvidia 30 series graphics card users
- Add additional option for obtaining the PSN AccountId via logging in with your psn username and password (in addition to the public lookup)

### 1.6.6

Lowers required mtu from 1435 to 576

- Enable using mtu as low as 576
- Enable gyro on Steam Deck automatically (can now set gyro to none in Steam Settings and still use native gyro)
- Adjust haptic rumble to work better with rumble motors
- Add variable zoom (accessible via Stream Menu)
- Enable mic support on MacOS (now on all platforms)
- Fix issues with address when registering manual consoles
- Update steam shortcut creation to cover additional edge cases
- Add option to reset key mappings to default in Settings
- Add Steam shortcut option to Windows build

### 1.6.5

Graduate DualSense features from experimental status

- Provides quiet haptic rumble for Steam Deck (default) in addition to optional noisy haptics
- Adds haptic rumble for MacOS
- Allow escape key to be selected for key mappings in Settings
- Add option to start the stream with the mic unmuted
- Add Steam Shortcut now also available on Windows

### 1.6.4

Enables creating steam shortcut with default controller profile from menu

- Adds create Steam Shortcut option for Linux and Mac
- Allow setting console pin for console in settings
- Add window type option in settings (fullscreen, stretch, zoom)
- Fix issue causing extra blank ip manual console added when registering non-manual console
- Add option to adjust when Wifi connection symbol appears based on % of dropped packets in 200ms interval

### 1.6.3

Small patch release

- Trade-off minor image artifacts for smoother stream
- Fix race condition causing remote play on console has crashed
- Fix issue where dropped packets results in losing mic connection
- Correctly set render preset
- Set Dualsense microphone and LED to match microphone mute status
- Add PS5 Rumble for controllers other than DualSense and Steam Deck (which have native haptics support)
- Enable game mode in MacOS
- Fix congestion control stop on Windows and log Auto audio output when chosen
- Add alternate option of using flipscreen.games to query PSN Login
- Correctly replace old reference frames and update bitstream parsing

### 1.6.2

Small patch release

- Fixes framepacing regression introduced in 1.6.0
- Dualsense haptics now work again on Linux
- Corrects stretch and zoom when using stream command
- Stop stream freezing on close on Windows
- Stop stream crashing on close on Windows
- Report corrupted frames earlier resulting in less frames dropped when a corrupted frame occurs
- Fix mac arm build not opening and reporting as damaged due to not being signed (was also backported to 1.6.1)

### 1.6.1

Small patch release

- Fixes console registration
- Fixes fullscreen double-click and F11 shortcuts
- Don't close main window when closing stream session on MacOS
- Adds option to sleep PlayStation when sleeping Steam Deck

### 1.6.0

Touch friendly and controller navigable GUI

- New touch-friendly and controller navigable GUI
- MacOS support and Windows libplacebo renderer support
- Resume connection from sleep mode on Steam Deck
- Ability to login to PlayStation for account ID via GUI
- Fix Senkusha, lowering time for console connection and properly setting MTU
- Audio Switch to SDL including fixing audio lag building over session
- New Logo
- Qt6 support

### 1.5.1

Small patch release

- Workaround for gamescope bug (in Steam Deck Preview channel) causing HDR surface swapchain creation to hang.

### 1.5.0

HDR support

- Adds HDR support for chiaki-ng perfect with the Steam Deck OLED ([see the configuration section](../setup/configuration.md#hdr-high-dynamic-range) for more details).
- New libplacebo vulkan renderer with better picture quality due to post-processing techniques like debanding (now the default renderer)
- Adds option to use controller by positional layout instead of button labels (particularly for Nintendo-style controllers)
- Adds launcher script for appimage
- Adds vulkan video decoding for video cards that support it (Steam Deck doesn't)
- Implements basic FEC error concealment to improve streaming experience (white flashses / green blocking)

### 1.4.1

Small patch release

- Adds multiplier to accelerometer values to match acceleration values of lighter DualSense/DualShock 4 controller. Fixes issue in some games where the acceleration value wasn't high enough when moving/shaking the Steam Deck to trigger the in-game action.

### 1.4.0

Mic support

- Adds mic support to chiaki-ng
- Noise suppression and echo cancelling for mic configurable in the chiaki-ng menu

!!! Question "Why doesn't my bluetooth headset mic show up as an available microphone on Steam Deck?"

    The Steam Deck does not currently support microphones over bluetooth by default so you will need to either use a hardwired headset or the Steam Deck microphone unless you change your Steam Deck settings to enable bluetooth headset support (it is currently experimental and turned off by default due to a noticeable drop in audio quality). Thus, it's advised to use either a hardwired headset or the internal microphone. If you really want to use a bluetooth headset and can live with the drop in audio quality see [How to enable bluetooth headset modes on Steam Deck](https://steamdecki.org/Steam_Deck/Wireless/Bluetooth#Enabling_More_Codecs_and_Enabling_Headsets){target="_blank" rel="noopener"}.

#### Update Actions for Existing Users

1. [Updating `chiaki-ng`](#updating-chiaki-ng)

2. [Optional] Switch to the new default control layout `chiaki4deck+ mic` which adds toggle mic mute to `L4`. Alternatively, you can manually add toggle mic mute to a button of your choice by mapping ++ctrl+m++ to that button.

### 1.3.4

Small patch release

- Adds lowpass filter for haptics and tweaks haptic response => reduced noise while using Steam Deck haptics
- Automatic connection option for GUI
- Update automation script to work for PS4 remote connection

### 1.3.3

Small cosmetic release

- Update icons and display name to `chiaki-ng`

### 1.3.2

Small patch release

- disable Steam Deck haptics when external controllers connected
- add vertical orientation option for motion controls
- let analog trigger actions work w/out PlayStation features enabled
- merge update to RGB mapping with HW accelerated graphics from jonibim

### 1.3.1

Small patch release

- Added scrollbar to settings since bottom of page was cut off on Steam Deck
- Fixed gyro mapping regression (causing drift in some games [i.e., Dreams])
- Updated HIDAPI (dependency) to 0.13.1 due to critical bug in release 0.13.0

Install update following [updating `chiaki-ng`](#updating-chiaki-ng).

### 1.3.0

- Native gyro support for Steam Deck
- Haptics support for DualSense (via USB) and Steam Deck [experimental]
- Adaptive triggers with DualSense (via USB or Bluetooth)
- Automation script tweaks (allow using hostname and specifying external IP / hostname in addition to local one)
- Documentation Updates (new DIY sections on building docs + development builds on Steam Deck + document various 1.3.0 features and changes for new and existing users)
- Bug fixes (audio bug causing crash in base Chiaki fixed, mismatch between cli and automation script leading and lagging space handling fixed, etc.)

#### Update Actions for Existing Users

1. [Updating `chiaki-ng`](#updating-chiaki-ng)

2. [Optional] Enable the experimental PlayStation 5 features (enables PlayStation 5 haptics for Steam Deck and DualSense [via USB] and adaptive triggers for DualSense [via USB or bluetooth]).

    1. Check the box shown in the image below in the GUI.

        ![Enable PlayStation 5 Features](images/EnablePlayStation5Features.png)

    2. [If you are using a DualSense] Turn off Steam Input for the DualSense following the "Turning off Steam Input" tab in [this section](../setup/controlling.md#enabling-chiaki-ng-to-work-with-dualsense-dualshock-4){target="_blank" rel="noopener"}.

3. [Optional] Add an external IP/hostname to the automation by revisiting (running back through) the [Automation section](../setup/automation.md){target="_blank" rel="noopener"}. Doing this will make the automation use your external address if you aren't connected to your home wireless network.

    !!! Note

        This is for those that have gone through the process to make a connection outside of their local network to get an external IP/hostname to use.
    
### 1.2.0

`chiaki-ng`'s 2nd update since initial release.

#### What You Need to Do to Update

1. [Updating `chiaki-ng`](#updating-chiaki-ng)

2. Updating your controller config to the new default (`chiaki-ng+`) and/or updating your custom controller layouts to take advantage of native touchscreen / trackpad controls. See the [Controller Options](../setup/controlling.md#default-controller-profile){target="_blank" rel="noopener"} section for details.

    - Using the Default Controller Profile (Recommended Starting Point):
    
        Open the `chiaki-ng` [Controller Options](../setup/controlling.md#default-controller-profile){target="_blank" rel="noopener"} section, browsing the `COMMUNITY LAYOUTS` tab for the `chiaki4deck+` config, downloading it, and setting it as your new layout.
        
        [Approximate Time Estimate: 1 minute]
        
    - Creating a Custom Controller Profile (Great for tinkering, especially for updating the default profile to meet your needs exactly):
    
        Make a custom controller profile using the [Creating Your Own Controller Profile](../setup/controlling.md){target="_blank" rel="noopener"} section, taking special note of the [Special Button Mappings](../setup/controlling.md#special-button-mappings-you-need-to-assign-these-yourself){target="_blank" rel="noopener"} and [Using Steam Deck Controller Touchscreen in Your Custom Controller Profile](../setup/controlling.md#using-steam-deck-controller-touchscreen-in-your-custom-controller-profile){target="_blank" rel="noopener"}

        [Approximate Time Estimate: 10-20 minutes depending on your experience with Steam Deck Controller Layouts]

3. [OPTIONAL] If you have a PlayStation Login Passcode and want entering it to be automated, please revisit (run back through) the [Automation section](../setup/automation.md){target="_blank" rel="noopener"} (don't need to revisit any of the other sections). This is really quick if you use the "Automated Instructions (Recommended)" Tab. [Approximate Time Estimate: 5 minutes]

    !!! Success
        Once your script is updated, since it will be in the same location as before, the Game Mode and controller setup will automatically carry over to this updated automation script. Thus, after revisiting the [Automation section](../setup/automation.md){target="_blank" rel="noopener"}, you can immediately go back to using `chiaki-ng`.

#### What you Get by Updating

1. Full mapping for the PlayStation touchpad to the Steam Deck's touchscreen and trackpad (you can use either or switch between them if you so choose). See the [touchscreen](../setup/controlling.md#using-steam-deck-controller-touchscreen-in-your-custom-controller-profile){target="_blank" rel="noopener"} and [trackpad](../setup/controlling.md#default-chiaki-ng-layout-trackpad-mapping){target="_blank" rel="noopener"} mapping subsections of the [Controller Options](../setup/controlling.md#default-controller-profile){target="_blank" rel="noopener"} section for details.

    !!! note "General Touchscreen and Mouse Support"
        
        This update adds general touchscreen and mouse support for the PlayStation touchpad. Thus, it's applicable beyond the Steam Deck. With regard to `chiaki-ng`, the Steam Deck is the focus / inspiration for the update.

2. Updated RGB color mapping used in OpenGL widget to be more color accurate thanks to [Egoistically's](https://github.com/Egoistically){target="_blank" rel="noopener"} Chiaki fork. See [Updated RGB Mapping](done.md#updated-rgb-mapping){target="_blank" rel="noopener"} for details and a before and after comparison in Ghost of Tsushima.

3. Automatic Login Passcode Entry (For Users with a Login Passcode)

    If you have to enter a login passcode each time you turn on your PlayStation console and login, you can now enter it into the automation to login from your personal Steam Deck without the hassle of bringing up the virtual keyboard with `Steam` ++plus+x++ and entering it in each time.

### 1.1.0

`chiaki-ng`'s 1st update since initial release.

#### What You Need to Do to Update

1. [Updating `chiaki-ng`](#updating-chiaki-ng)

2. Revisiting (running back through) the [Automation section](../setup/automation.md){target="_blank" rel="noopener"} (don't need to revisit any of the other sections). This is really quick if you use the "Automated Instructions (Recommended)" Tab. 

    !!! Success
        Once your script is updated, since it will be in the same location as before, the Game Mode and controller setup will automatically carry over to this updated automation script. Thus, after revisiting the [Automation section](../setup/automation.md){target="_blank" rel="noopener"}, you can immediately go back to using `chiaki-ng`.

3. *[If desired]* Visit the [Using a DualSense and/or DualShock4 Controller with `chiaki-ng`](../setup/controlling.md#using-a-dualsense-andor-dualshock4-controller-with-chiaki-ng){target="_blank" rel="noopener"} to see how to setup native touchpad and gyro controls when playing `chiaki-ng` with the DualShock4 and DualSense controllers.

#### What you Get by Updating

1. PlayStation controller native touchpad + gyro controller support enabled for the flatpak with setup instructions in [Using a DualSense and/or DualShock4 Controller with `chiaki-ng`](../setup/controlling.md#using-a-dualsense-andor-dualshock4-controller-with-chiaki-ng){target="_blank" rel="noopener"}. This is great for when your using a TV or monitor with your Steam Deck.

2. Enhanced automated launch

    Specifically, I upgraded the discovery cli command to work properly and updated the automation script (and accompanying generator script) to to take advantage of this command instead of ping to handle some cases that failed intermittently before.

    Now, the automation works for edge cases such as:

    - user is remote playing from outside their home wireless network (given they've already done the networking setup for that)

    - console is in the process of going to sleep or coming online when remote play session launched

    - console is currently downloading a large game

    - user doesn't have ping enabled on his/her wireless network

### 1.0.0

`chiaki-ng's` initial release including the following notable updates:

1. 3 view modes for non-standard screen sizes 

2. Quit function ++ctrl+q++

3. Enabled Automated Launch
