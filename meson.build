# buildtype debugoptimized (-O2 -g): the plain debug build (-O0) made every redraw several times slower
project('end4pC', ['c', 'cpp'], default_options: ['cpp_std=c++20', 'warning_level=2', 'buildtype=debugoptimized'])

fs = import('fs')
add_project_arguments('-DEND4PC_VERSION="' + fs.read('VERSION').strip() + '"', language: ['c', 'cpp'])  # shown by `end4pC --version`; the label of the installed version
cc = meson.get_compiler('c')
wayland_client = dependency('wayland-client')
pangocairo = dependency('pangocairo')
pangoft2 = dependency('pangoft2')
fontconfig = dependency('fontconfig')  # the shell font is chosen by an alias for "Sans" (src/shellfont.h)
gdkpixbuf = dependency('gdk-pixbuf-2.0')
rsvg = dependency('librsvg-2.0')
threads = dependency('threads')  # wallpaper decoding for transitions runs in worker threads
sdbus = dependency('libsystemd')  # sd-bus: the notification service talks D-Bus
pam = cc.find_library('pam')
xkb = dependency('xkbcommon')  # turns key codes into text for the launcher's search box

scanner = find_program('wayland-scanner')
xdg_xml = '/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml'
layer_xml = '/usr/share/wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml'
cursor_xml = '/usr/share/wayland-protocols/staging/cursor-shape/cursor-shape-v1.xml'
tablet_xml = '/usr/share/wayland-protocols/stable/tablet/tablet-v2.xml'
lock_xml = '/usr/share/wayland-protocols/staging/ext-session-lock/ext-session-lock-v1.xml'  # cursor-shape refers to its types
idle_xml = '/usr/share/wayland-protocols/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml'
ftl_xml = '/usr/share/wayland-protocols/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml'  # the overview: live windows
capsrc_xml = '/usr/share/wayland-protocols/staging/ext-image-capture-source/ext-image-capture-source-v1.xml'
copycap_xml = '/usr/share/wayland-protocols/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml'
blur_xml = '/usr/share/wayland-protocols/staging/ext-background-effect/ext-background-effect-v1.xml'  # blur behind translucent shell surfaces (niri)
fscale_xml = '/usr/share/wayland-protocols/staging/fractional-scale/fractional-scale-v1.xml'  # sharp drawing at fractional scales (src/hidpi.h)
viewporter_xml = '/usr/share/wayland-protocols/stable/viewporter/viewporter.xml'

generated = []
foreach xml : [xdg_xml, layer_xml, cursor_xml, tablet_xml, lock_xml, idle_xml, blur_xml, ftl_xml, capsrc_xml, copycap_xml, fscale_xml, viewporter_xml]
  name = fs.stem(xml)
  generated += custom_target(name + '-header',
    input: xml,
    output: name + '-client-protocol.h',
    command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  generated += custom_target(name + '-code',
    input: xml,
    output: name + '-protocol.c',
    command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach

executable('end4pC', 'src/main.cpp', 'src/notify_dbus.c', 'src/tray_dbus.c', 'src/polkit_dbus.c', generated,
  dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb, sdbus, threads],
  install: false)

executable('end4pC-settings', 'src/settings.cpp', generated,
  dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb],
  install: false)

text_input_xml = '/usr/share/wayland-protocols/unstable/text-input/text-input-unstable-v3.xml'
text_input = [
  custom_target('text-input-v3-header', input: text_input_xml, output: 'text-input-unstable-v3-client-protocol.h',
    command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
  custom_target('text-input-v3-code', input: text_input_xml, output: 'text-input-unstable-v3-protocol.c',
    command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
]
executable('end4pC-tools', 'src/tools.cpp', generated, text_input,
  dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb, threads],
  install: false)

executable('end4pC-lock', 'src/lock.cpp', generated,
  dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb, pam, threads],
  install: false)

# Test build: unlocks itself after 16 seconds. Never installed or bound to a key.
executable('end4pC-lock-debug', 'src/lock.cpp', generated,
  cpp_args: ['-DLOCK_DEBUG=16'],
  dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb, pam, threads],
  install: false)

# Where the desktop widgets are placed on screens of many sizes (src/deskplace.h): `meson test -C build`, or tests/desk/run.sh
test('deskplace', executable('deskplace-test', 'tests/desk/test_deskplace.cpp', install: false))

# The Markdown formatting of the Notes editor (src/mdedit.h): `meson test -C build`, or tests/md/run.sh
test('mdedit', executable('mdedit-test', 'tests/md/test_mdedit.cpp', install: false))

# The single-line text selection of the text fields (src/textsel.h): `meson test -C build`, or tests/text/run.sh
test('textsel', executable('textsel-test', 'tests/text/test_textsel.cpp', install: false))

# The geometry of the display arrangement editor (src/displaymap.h): `meson test -C build`, or tests/display/run.sh
test('displaymap', executable('displaymap-test', 'tests/display/test_displaymap.cpp', install: false))

# The Freeform board of the Tools window: shapes, concept links, undo and the file form (src/freeform.h): `meson test -C build`, or tests/freeform/run.sh
test('freeform', executable('freeform-test', 'tests/freeform/test_freeform.cpp', install: false))

# The per-monitor hot corner rules (src/hotcorners.h): `meson test -C build`, or tests/hotcorners/run.sh
test('hotcorners', executable('hotcorners-test', 'tests/hotcorners/test_hotcorners.cpp', install: false))

# Headless media integration: real shell code with a temporary fake playerctl.
test('media', executable('media-test', 'tests/media/test_media.cpp',
  'src/notify_dbus.c', 'src/tray_dbus.c', 'src/polkit_dbus.c', generated,
  cpp_args: ['-O0', '-g0'],
  dependencies: [wayland_client, pangocairo, pangoft2, fontconfig, gdkpixbuf, rsvg, xkb, sdbus, threads],
  install: false))
