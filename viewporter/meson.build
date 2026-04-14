wayland_protocols = dependency('wayland-protocols')
wayland_scanner = dependency('wayland-scanner', native: true)
wayland_scanner_prog = find_program(wayland_scanner.get_variable('wayland_scanner'), required: true)

wayland_protocols_datadir = wayland_protocols.get_variable('pkgdatadir')

xdg_shell_xml = wayland_protocols_datadir + '/stable/xdg-shell/xdg-shell.xml'
xdg_decoration_xml = wayland_protocols_datadir + '/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml'
viewporter_xml = wayland_protocols_datadir + '/stable/viewporter/viewporter.xml'

xdg_shell_client_header = custom_target(
	'xdg-shell-client-protocol.h',
	input: xdg_shell_xml,
	output: 'xdg-shell-client-protocol.h',
	command: [wayland_scanner_prog, 'client-header', '@INPUT@', '@OUTPUT@']
)

xdg_decoration_code = custom_target(
	'xdg-decoration-unstable-v1-protocol.c',
	input: xdg_decoration_xml,
	output: 'xdg-decoration-unstable-v1-protocol.c',
	command: [wayland_scanner_prog, 'private-code', '@INPUT@', '@OUTPUT@']
)

xdg_decoration_client_header = custom_target(
	'xdg-decoration-unstable-v1-client-protocol.h',
	input: xdg_decoration_xml,
	output: 'xdg-decoration-unstable-v1-client-protocol.h',
	command: [wayland_scanner_prog, 'client-header', '@INPUT@', '@OUTPUT@']
)

xdg_shell_code = custom_target(
	'xdg-shell-protocol.c',
	input: xdg_shell_xml,
	output: 'xdg-shell-protocol.c',
	command: [wayland_scanner_prog, 'private-code', '@INPUT@', '@OUTPUT@']
)

viewporter_client_header = custom_target(
	'viewporter-client-protocol.h',
	input: viewporter_xml,
	output: 'viewporter-client-protocol.h',
	command: [wayland_scanner_prog, 'client-header', '@INPUT@', '@OUTPUT@']
)

viewporter_code = custom_target(
	'viewporter-unstable-v1-protocol.c',
	input: viewporter_xml,
	output: 'viewporter-unstable-v1-protocol.c',
	command: [wayland_scanner_prog, 'private-code', '@INPUT@', '@OUTPUT@']
)

viewporter_test = executable(
	'viewporter-test',
	['viewporter-test.c', xdg_shell_client_header, xdg_shell_code,
	 xdg_decoration_client_header, xdg_decoration_code,
	 viewporter_client_header, viewporter_code],
	dependencies: [wayland_client],
	include_directories: include_directories('.')
)
