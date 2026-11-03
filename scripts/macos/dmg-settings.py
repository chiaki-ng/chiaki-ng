# dmgbuild settings for the chiaki-ng DMG: the app beside an Applications shortcut over a background
# that says to drag one onto the other. dmgbuild writes the window layout itself, so unlike
# create-dmg it needs no Finder scripting and works on headless CI runners.
# Pass -D app=<path to chiaki-ng.app> -D background=<background.tiff>
import os.path

app = defines["app"]
app_name = os.path.basename(app)

format = "UDZO"
files = [app]
symlinks = {"Applications": "/Applications"}
icon = os.path.join(app, "Contents", "Resources", "chiaking.icns")

background = defines["background"]
window_rect = ((200, 120), (660, 400))
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
default_view = "icon-view"
icon_size = 128
text_size = 13
icon_locations = {
    app_name: (180, 190),
    "Applications": (480, 190),
}
