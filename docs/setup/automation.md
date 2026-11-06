# Automating `chiaki-ng` Launch

!!!Tip "Use the Auto Connect Feature Instead if it Meets Your Needs"

    Now that you have `chiaki-ng` configured, it's time to make it wake up your PlayStation and connect to it automatically. Note: If you only have 1 console please try the [autoconnect feature](configuration.md#auto-connect). Otherwise, proceed with this guide to setup a script to automatically launch your different systems.

!!! Warning "Sleep Mode Required"

    For this to work, your PlayStation needs to either be in rest/sleep mode or on (so that it is listening for the wake up signal). This means you will want to keep your PlayStation in rest/sleep mode when you are not playing (instead of off).

1. Open a `konsole` session (launch it via `Applications` by clicking the Steam icon in the bottom left and searching for it in the `All Applications` section)

2. Get your PlayStation IP

    1. Get your registered PlayStation's IP Address

        1. Launch Chiaki via your desktop or via:
            
            ``` bash
            flatpak run io.github.streetpea.Chiaki4deck
            ```

        2. Get your IP address via the console widget and then close the window and return to the `konsole` session

            ![IP Address](images/GetAddress.png)

            !!! Tip "Prevent IP From Changing"

                In order to prevent your IP from changing (which would make the script stop working) if you ever disconnect your PlayStation console from your network and reconnect it (especially if other devices are added to the network in the meantime), you should go into your router settings and reserve an IP address for your PlayStation (DHCP IP reservation / "static" IP) or create a hostname for it. For a TP-Link, Netgear, Asus or Linskys router, follow [these instructions](https://www.coolblue.nl/en/advice/assign-fixed-ip-address-router.html){target="_blank" rel="noopener"}. If you have a different router, you can search (using a search engine such as DuckDuckGo or Google) for instructions for that specific router using the formula "dhcp reservation myroutername router" such as "dhcp reservation netgear router" and follow the instructions to reserve an IP for your PlayStation console so that it won't change. Alternatively, if your router has an option to set hostnames for your devices, you can set a hostname for your PlayStation console and use your hostname in the automation instead of a static IP address. 

3. Create script for waking PlayStation and launching Chiaki

    === "Automated Instructions (Recommended)"

        1. Run the [gen-launcher script](https://raw.githubusercontent.com/chiaki-ng/chiaki-ng/main/scripts/gen-launcher.sh){target="_blank" rel="noopener"} using the following command in your `konsole` and answer the prompts (you will need your IP address from step 2 above)

            ``` bash
            bash <(curl -sLo- https://raw.githubusercontent.com/chiaki-ng/chiaki-ng/main/scripts/gen-launcher.sh)
            ```

            !!! Question "What Do the Different Modes (i.e., fullscreen [uses Normal], zoom, stretch) Look Like?"

                To see examples of the different launch modes, visit the [Updates section](../updates/done.md#3-view-modes-for-non-standard-screen-sizes){target="_blank" rel="noopener"}

            !!! Tip "Connecting Outside of Your Local Network"

                You can add an external IP or hostname in addition to your local one to connect from an external network. To do so, you need to set up port forwarding as detailed in the prior [remote connection section](remoteconnection.md){target="_blank" rel="noopener"}. Then, you can choose to setup an external IP/hostname as part of the setup script process. The automation will take your home network name (SSID) to check if you are home or not to use the correct IP/hostname automatically. The script will automatically detect your current SSID. Thus, if you are on your home network while running the setup script (`gen-launcher.sh`), it will detect your home ssid so you will just need to hit ++enter++. If not, you can enter ot manually (instead of using the default of your client device's [i.e., Steam Deck's] current network's SSID). You can get your home SSID for manual entry via `iwgetid -r` from the `konsole` when connected to your home network or by looking in your network history on your client device [i.e., Steam Deck] (it will be the connection name for your home network such as `StreetPea-5G`). Of course, if you want to skip this now and setup the external address later, you can always rerun the automation or choose a placeholder external IP now and manually edit it to your desired IP/hostname later.

            ???- example "Example Output [click to expand me]"

                ``` bash
                bash <(curl -sLo- https://raw.githubusercontent.com/chiaki-ng/chiaki-ng/main/scripts/gen-launcher.sh)

                1) PlayStation 4
                2) PlayStation 5
                Please select the number corresponding to your Playstation Console: 2
                Option 2: PlayStation 5 was chosen




                HOME ADDRESS
                -------------
                1) IP
                2) hostname
                NOTICE: Use 1 unless you created a hostname (FQDN) for your PlayStation
                Please select the number corresponding to your address type: 1
                Option 1: IP was chosen

                Enter your PlayStation IP (should be xxx.xxx.xxx.xxx like 192.168.1.16):
                192.168.1.16

                Do you have a separate address (DNS or IP) to access this console away from home? (y/n):
                y
                Enter your home SSID [hit enter for default: StreetPea-5G]: 



                AWAY ADDRESS
                -------------
                1) IP
                2) hostname
                NOTICE: Use 1 unless you created a hostname (FQDN) for your PlayStation
                Please select the number corresponding to your address type: 2
                Option 2: hostname was chosen

                Enter your PlayStation FQDN (hostname) (should be abc.ident like foo.bar.com):
                foo.bar.com

                1) fullscreen
                2) zoom
                3) stretch
                Please select the number corresponding to the default mode you want to use: 3
                Option 3: stretch was chosen

                Do you have a PlayStation Login Passcode? (y/n):
                y
                Enter your 4 digit PlayStation Login Passcode:
                1111

                Would you like to test the newly created script? (y/n):
                y
                sounds good, launching script now...
                [I] Logging to file /home/deck/.var/app/io.github.streetpea.chiaki-ng/data/Chiaki/Chiaki/log/chiaki_session_2023-01-06_08-33-49-583583.log
                [I] Chiaki Version 2.1.1
                [I] Using hardware decoder "vaapi"
                Device Found
                type: 28de 1205
                path: /dev/hidraw3
                serial_number: 
                Manufacturer: Valve Software
                Product:      Steam Deck Controller
                Release:      200
                Interface:    2
                Usage (page): 0x1 (0xffff)

                [I] Connected Steam Deck ... gyro online


                [I] Controller 0 opened: "Microsoft X-Box 360 pad 0 (030079f6de280000ff11000001000000)"
                [I] Starting session request for PS5
                [I] Trying to request session from 192.168.1.16:9295
                [I] Connected to 192.168.1.16:9295
                [I] Sending session request
                [I] OpenGL initialized with version "4.6 (Core Profile) Mesa 22.2.4 (git-80df10f902)"
                [I] Session request successful
                [I] Starting ctrl
                [I] Ctrl connected to 192.168.1.16:9295
                [I] Sending ctrl request
                [I] Ctrl received http header as response
                [I] Ctrl received ctrl request http response
                [I] Ctrl got Server Type: 2
                [I] Ctrl connected
                [I] Ctrl received Login PIN request
                [I] Ctrl requested Login PIN
                [I] Session received entered Login PIN, forwarding to Ctrl
                [I] Ctrl received entered Login PIN, sending to console
                [I] Steam Deck Haptics Audio opened with 2 channels @ 3000 Hz with 150 samples per audio analysis.
                [I] Ctrl received Login message: success
                [I] Ctrl received valid Session Id: 1673022831TZLW3NGFZP76ZLUPAOHZBA64PQQ2AF6LXUET2OFDTKAJF4XVGUAXOMOQDXBVVCAS
                [I] Starting Senkusha
                [I] Enabling DualSense features
                [I] Takion connecting (version 7)
                [I] Takion enabled Don't Fragment Bit
                [I] Takion sent init
                [I] Takion received init ack with remote tag 0xb18ccf, outbound streams: 0x64, inbound streams: 0x64
                [W] Received Ctrl Message with unknown type 0x16
                [W] offset 0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f  0123456789abcdef
                [W]      0 01 ff                                           ..              
                [I] Takion sent cookie
                [I] Takion received cookie ack
                [I] Takion connected
                [I] Senkusha sending big
                [I] Senkusha successfully received bang
                [I] Senkusha Ping Test with count 10 starting
                [I] Senkusha enabled echo
                [I] Senkusha sending Ping 0 of test index 0
                [I] Senkusha received Pong, RTT = 3.019 ms
                [I] Senkusha sending Ping 1 of test index 0
                [I] Senkusha received Pong, RTT = 2.841 ms
                [I] Senkusha sending Ping 2 of test index 0
                [I] Senkusha received Pong, RTT = 3.460 ms
                [I] Senkusha sending Ping 3 of test index 0
                [I] Senkusha received Pong, RTT = 2.504 ms
                [I] Senkusha sending Ping 4 of test index 0
                [I] Senkusha received Pong, RTT = 3.030 ms
                [I] Senkusha sending Ping 5 of test index 0
                [I] Senkusha received Pong, RTT = 2.778 ms
                [I] Senkusha sending Ping 6 of test index 0
                [I] Senkusha received Pong, RTT = 3.131 ms
                [I] Senkusha sending Ping 7 of test index 0
                [I] Senkusha received Pong, RTT = 2.795 ms
                [I] Senkusha sending Ping 8 of test index 0
                [I] Senkusha received Pong, RTT = 2.992 ms
                [I] Senkusha sending Ping 9 of test index 0
                [I] Senkusha received Pong, RTT = 3.064 ms
                [I] Senkusha disabled echo
                [I] Senkusha determined average RTT = 2.961 ms
                [I] Senkusha starting MTU in test with min 576, max 1454, retries 3, timeout 14 ms
                [I] Senkusha MTU request 1454 (min 576, max 1454), id 1, attempt 0
                [I] Senkusha MTU 1454 success
                [I] Senkusha determined inbound MTU 1454
                [I] Senkusha starting MTU out test with min 576, max 1454, retries 3, timeout 14 ms
                [I] Senkusha sent initial client MTU command
                [I] Senkusha received expected Client MTU Command
                [I] Senkusha MTU 1454 out ping attempt 0
                [I] Senkusha MTU ping 1454 success
                [I] Senkusha determined outbound MTU 1454
                [I] Senkusha sending final Client MTU Command
                [I] Senkusha is disconnecting
                [I] Senkusha closed takion
                [I] Senkusha completed successfully
                [I] Takion connecting (version 12)
                [I] Takion sent init
                [I] Takion received init ack with remote tag 0x7033129, outbound streams: 0x64, inbound streams: 0x64
                [I] Takion sent cookie
                [I] Takion received cookie ack
                [I] Takion connected
                [I] StreamConnection sending big
                [I] BANG received
                [I] StreamConnection successfully received bang
                [I] Crypt has become available. Re-checking MACs of 0 packets
                [W] Received Ctrl Message with unknown type 0x41
                [W] offset 0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f  0123456789abcdef
                [W]      0 00 00 00 00 02 01 00 00                         ........        
                [D] StreamConnection received audio header:
                [D] offset 0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f  0123456789abcdef
                [D]      0 02 10 00 00 bb 80 00 00 01 e0 00 00 00 01       ..............  
                [I] Audio Header:
                [I]   channels = 2
                [I]   bits = 16
                [I]   rate = 48000
                [I]   frame size = 480
                [I]   unknown = 1
                [I] ChiakiOpusDecoder initialized
                [I] Audio Device alsa_output.pci-0000_04_00.1.hdmi-stereo-extra2 opened with 2 channels @ 48000 Hz, buffer size 19200
                [I] Video Profiles:
                [I]   0: 1920x1080
                [I] StreamConnection successfully received streaminfo
                [I] Switched to profile 0, resolution: 1920x1080
                [I] StreamConnection is disconnecting
                [I] StreamConnection sending Disconnect
                [I] StreamConnection was requested to stop
                [I] StreamConnection closed takion
                [I] StreamConnection completed successfully
                [I] Ctrl requested to stop
                [I] Ctrl stopped
                [I] Session has quit
                    .__    .__        __   .__   _____     .___             __    
                ____ |  |__ |__|____  |  | _|__| /  |  |  __| _/____   ____ |  | __
                _/ ___\|  |  \|  \__  \ |  |/ /  |/   |  |_/ __ |/ __ \_/ ___\|  |/ /
                \  \___|   Y  \  |/ __ \|    <|  /    ^   / /_/ \  ___/\  \___|    < 
                \___  >___|  /__(____  /__|_ \__\____   |\____ |\___  >\___  >__|_ \
                    \/     \/        \/     \/       |__|     \/    \/     \/     \/
                                    .__        __             .___._.
                ______ ___________|__|______/  |_  ____   __| _/| |
                /  ___// ___\_  __ \  \____ \   __\/ __ \ / __ | | |
                \___ \  \___|  | \/  |  |_> >  | \  ___// /_/ |  \|
                /____  >\___  >__|  |__|   __/|__|  \___  >____ |  __
                    \/     \/         |__|             \/     \/  \/
                ```

        
    === "Manual Instructions (For People who like to do things by hand)"

        1. Get your console nickname

            1. It was the nickname listed with your console in the console widget. It can also be obtained with

                ```bash
                flatpak run io.github.streetpea.Chiaki4deck list
                ```

                ???+ example "Example Output"

                    ``` bash
                    Host: PS5-012
                    ```

                    In this example, the console nickname is `PS5-012`, please use your nickname. If you have multiple, choose the one from the list you want and choose the corresponding regist_key in the next step (i.e.,  if you pick the console on the 2nd line here, pick the regist_key on the 2nd line in the next step).

        2. Get your remote play registration key

            1. Run the following command in your console

                ``` bash
                cat ~/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki/Chiaki.conf | grep regist_key | cut -d '(' -f2 | cut -d '\' -f1
                ```

                ???+ example "Example Output"

                    ``` bash
                    2ebf539d
                    ```

                !!! Note "Chiaki Configuration File"

                    This command is printing the Chiaki configuration file generated when you ran `chiaki-ng` for the first time. This is where the flatpak version of Chiaki saves your settings and details. In fact, all flatpaks store their details with the ~/.var/app/app_id pattern. If you want to look at the file itself and get the value you can open the file at `/home/deck/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki/Chiaki.conf` and look for the remote play registration key [`rp_regist_key`].


        3. Choose your [default launch option](../updates/done.md#3-view-modes-for-non-standard-screen-sizes){target="_blank" rel="noopener"} (fullscreen, zoom, or stretch)

            - fullscreen: A full screen picture with black bars, aspect ratio maintained

            - zoom: A full screen zoomed picture with no black bars, aspect ratio maintained, edges of image cut off by zoom

            - stretch: A full screen stretched picture with no black bars, aspect ratio slightly distorted by vertical [in the case of Steam Deck] stretch

            !!! Info "Default Launch Option"

                This is the option remote play will launch with. You can always toggle to a different option by using the ++ctrl+s++ (toggles between stretch and normal) and ++ctrl+z++ (toggles between zoom and normal) options which should be set if desired in the Steam controller configuration for the game. See [Controller Configuration Section](controlling.md){target="_blank" rel="noopener"} for more details.

        4. Get your 4 digit PlayStation login passcode (if applicable). You will know your PlayStation login passcode if you have one because you have to enter it every time you log onto your PlayStation console. If you don't have to do this, you don't have a PlayStation login passcode and will leave that field blank.

        5. Create a new blank file located in the folder `/home/deck/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki` named  `Chiaki-launcher.sh`

            ```bash
            touch "${HOME}/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki/Chiaki-launcher.sh"
            ```
        
        6. Open the `/home/deck/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki/Chiaki-launcher.sh` file in your favorite editor and save the following output.

            ``` bash
            #!/usr/bin/env bash

            connect_error_loc()
            {
                echo "Error: Couldn't connect to your PlayStation console from your local address!" >&2
                echo "Error: Please check that your Steam Deck and PlayStation are on the same network" >&2
                echo "Error: ...and that you have the right PlayStation IP address or hostname!" >&2
                exit 1
            }

            connect_error_ext()
            {
                echo "Error: Couldn't connect to your PlayStation console from your external address!" >&2
                echo "Error: Please check that you have forwarded the necessary ports on your router" >&2
                echo "Error: ...and that you have the right external PlayStation IP address or hostname!" >&2
                exit 1
            }

            wakeup_error()
            {
                echo "Error: Couldn't wake up PlayStation console from sleep!" >&2
                echo "Error: Please make sure you are using a PlayStation 5." >&2
                echo "Error: If not, change the wakeup call to use the number of your PlayStation console" >&2
                exit 2
            }

            timeout_error()
            {
                echo "Error: PlayStation console didn't become ready in 35 seconds!" >&2
                echo "Error: Please change 35 to a higher number in your script if this persists." >&2
                exit 1
            }
            if [ "$(iwgetid -r)" == "<local_ssid>" ]
            then
                addr="<local_addr>"
                local=true
            else
                addr="<external_addr>"
                local=false
            fi
            SECONDS=0
            # Wait for console to be in sleep/rest mode or on (otherwise console isn't available)
            ps_status="$(flatpak run io.github.streetpea.Chiaki4deck discover -h ${addr} 2>/dev/null)"
            while ! echo "${ps_status}" | grep -q 'ready\|standby'
            do
                if [ ${SECONDS} -gt 35 ]
                then
                    if [ "${local}" = true ]
                    then
                        connect_error_loc
                    else
                        connect_error_ext
                    fi
                fi
                sleep 1
                ps_status="$(flatpak run io.github.streetpea.Chiaki4deck discover -h ${addr} 2>/dev/null)"
            done

            # Wake up console from sleep/rest mode if not already awake
            if ! echo "${ps_status}" | grep -q ready
            then
                flatpak run io.github.streetpea.Chiaki4deck wakeup -<playstation_console> -h ${addr} -r '<remote_play_registration_key>' 2>/dev/null
            fi

            # Wait for PlayStation to report ready status, exit script on error if it never happens.
            while ! echo "${ps_status}" | grep -q ready
            do
                if [ ${SECONDS} -gt 35 ]
                then
                    if echo "${ps_status}" | grep -q standby
                    then
                        wakeup_error
                    else
                        timeout_error
                    fi
                fi
                sleep 1
                ps_status="$(flatpak run io.github.streetpea.Chiaki4deck discover -h ${addr} 2>/dev/null)"
            done

            # Begin playing PlayStation remote play via Chiaki on your Steam Deck :)
            flatpak run io.github.streetpea.Chiaki4deck --passcode <login_passcode> --<launch_option> stream '<console_nickname>' ${addr}
            ```
        
        7. Edit the `/home/deck/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki/Chiaki-launcher.sh` file, replacing the following values for your values:

            1. `<local_ssid>` local network name found with:

                === "Via konsole"

                    ``` bash
                    iwgetid -r
                    ```

                    !!! Warning "Must Be Currently Connected to Local Network"

                        You must be connected to your current local network for this `konsole` method to work. Otherwise, it will give you the network you are currently connected to or nothing (if you're not currently connected to a network). If you're not connected to your home network you can view your SSID via the GUI using the `Via GUI` tab.

                === "Via GUI"

                    The connection name/SSID for your home network in the client device's [i.e., Steam Deck] network settings such as `StreetPea-5G`.

            2. `<local_addr>` with local PlayStation IP

            3. `<external_addr>` with external IP or hostname. 
                
                !!! Tip "Filling this in Later"
                    
                    You can use your local PlayStation IP or something like `foo.bar.com` as a placeholder if haven't set up an external IP/hostname yet. Then, if you decide to set up an external connection, you can manually substitute the "real" external IP/hostname.

            4. `<console_nickname>`

                !!! Warning "Beware of '"

                    If you have any ' in your nickname itself, such as `Street Pea's PS5`, you will need enter the `'` as `'\''`. Thus, for `Street Pea's PS5` instead of using `Street Pea's PS5` you would use `Street Pea'\''s PS5`. All other characters are fine. This is due to the PlayStation console allowing any manner of names (including non-valid hostnames with all manner of special characters) and Chiaki using that name in its configuration file.


            5. `<remote_play_registration_key>`

            6. `<launch_option>`

            7. `<playstation_console>` (either 4 or 5)

            8. `<login_passcode>` (just erase this field if you don't have a login passcode)

            ???+ example "Example Script with "fake" values [yours will be different]"

                ``` bash
                #!/usr/bin/env bash

                connect_error_loc()
                {
                    echo "Error: Couldn't connect to your PlayStation console from your local address!" >&2
                    echo "Error: Please check that your Steam Deck and PlayStation are on the same network" >&2
                    echo "Error: ...and that you have the right PlayStation IP address or hostname!" >&2
                    exit 1
                }

                connect_error_ext()
                {
                    echo "Error: Couldn't connect to your PlayStation console from your external address!" >&2
                    echo "Error: Please check that you have forwarded the necessary ports on your router" >&2
                    echo "Error: ...and that you have the right external PlayStation IP address or hostname!" >&2
                    exit 1
                }

                wakeup_error()
                {
                    echo "Error: Couldn't wake up PlayStation console from sleep!" >&2
                    echo "Error: Please make sure you are using a PlayStation 5." >&2
                    echo "Error: If not, change the wakeup call to use the number of your PlayStation console" >&2
                    exit 2
                }

                timeout_error()
                {
                    echo "Error: PlayStation console didn't become ready in 35 seconds!" >&2
                    echo "Error: Please change 35 to a higher number in your script if this persists." >&2
                    exit 1
                }
                if [ "$(iwgetid -r)" == "StreetPea-5G" ]
                then
                    addr="192.168.1.16"
                    local=true
                else
                    addr="foo.bar.com"
                    local=false
                fi
                SECONDS=0
                # Wait for console to be in sleep/rest mode or on (otherwise console isn't available)
                ps_status="$(flatpak run io.github.streetpea.Chiaki4deck discover -h ${addr} 2>/dev/null)"
                while ! echo "${ps_status}" | grep -q 'ready\|standby'
                do
                    if [ ${SECONDS} -gt 35 ]
                    then
                        if [ "${local}" = true ]
                        then
                            connect_error_loc
                        else
                            connect_error_ext
                        fi
                    fi
                    sleep 1
                    ps_status="$(flatpak run io.github.streetpea.Chiaki4deck discover -h ${addr} 2>/dev/null)"
                done

                # Wake up console from sleep/rest mode if not already awake
                if ! echo "${ps_status}" | grep -q ready
                then
                    flatpak run io.github.streetpea.Chiaki4deck wakeup -5 -h ${addr} -r '2ebf539d' 2>/dev/null
                fi

                # Wait for PlayStation to report ready status, exit script on error if it never happens.
                while ! echo "${ps_status}" | grep -q ready
                do
                    if [ ${SECONDS} -gt 35 ]
                    then
                        if echo "${ps_status}" | grep -q standby
                        then
                            wakeup_error
                        else
                            timeout_error
                        fi
                    fi
                    sleep 1
                    ps_status="$(flatpak run io.github.streetpea.Chiaki4deck discover -h ${addr} 2>/dev/null)"
                done

                # Begin playing PlayStation remote play via Chiaki on your Steam Deck :)
                flatpak run io.github.streetpea.Chiaki4deck --passcode --zoom stream 'PS5-012' ${addr}
                ```

                !!! Note "Example is Without Login Passcode #"

                    If you have a passcode you would set it like `--passcode 1111`, changing the last line of the example above to `flatpak run io.github.streetpea.Chiaki4deck --passcode 1111 --zoom stream 'PS5-012' 192.168.1.16`

        8. Make the script executable:

            ```bash
            chmod +x "${HOME}/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki/Chiaki-launcher.sh"
            ```

4. Test your newly created script (if you haven't already) by running:
        
    ```bash
    "${HOME}/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki/Chiaki-launcher.sh"
    ```

    === "Your script worked!"
    
        !!! success "We have liftoff! :rocket:"

            Chiaki launched in your desired screen mode on your client device [i.e., Steam Deck]! Congratulations!

    === "Your script didn't work..."

        !!! Failure "You are no longer on the happy path :weary:"

            === "Script fails before launching stream window"

                Check the error message and fix issues if obvious. Otherwise, things to check:
                
                1. Make sure your PlayStation console is in either rest/sleep mode or on (otherwise this won't work)
                
                2. Double check the values you entered in the script to make sure they are correct.
                
                3. Open your script at: `/home/deck/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki/Chiaki-launcher.sh` and try to run each command individually to see what's causing your problem.
                
                4. If issues persist with this and `chiaki-ng` launches fine regularly via the application link, just not the automation, feel free to reach out. If you think the documentation itself needs to be updated click the :material-heart-broken: underneath "Was this page helpful?" and open the feedback form for this page. If you just need help, you can reach me via [Reddit](https://www.reddit.com/message/compose/?to=Street_Pea_6693){target="_blank" rel="noopener"} or [email](mailto:streetpea@proton.me)

            === "Error in stream window"
 
                Close `chiaki-ng` gracefully by closing any open dialog boxes and then using ++ctrl+q++ on the stream window. If `chiaki-ng` doesn't close gracefully, move your mouse to the top left corner of the stream window and a blue circle should highlight the corner of the screen. Click on it to see all of your windows and select the `konsole` window. Then, close the `konsole` window itself (click its `x` icon in the top right corner). Next, you can relaunch the script again to make sure it wasn't a one-time snafu. Note: Chiaki seems to perform better (audio/visual quality) in `Game Mode` vs. `Desktop Mode` for me. If issues persist, feel free to reach out via [Reddit](https://www.reddit.com/message/compose/?to=Street_Pea_6693){target="_blank" rel="noopener"} or [email](mailto:streetpea@proton.me). If you think the documentation itself needs to be updated click the :material-heart-broken: underneath "Was this page helpful?" and open the feedback form for this page.