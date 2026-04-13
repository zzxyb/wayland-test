wayland_protocols = dependency('wayland-protocols')
wayland_scanner = dependency('wayland-scanner', native: true)
wayland_scanner_prog = find_program(wayland_scanner.get_variable('wayland_scanner'), required: true)

wayland_protocols_datadir = wayland_protocols.get_variable('pkgdatadir')

xdg_shell_xml = wayland_protocols_datadir + '/stable/xdg-shell/xdg-shell.xml'

xdg_shell_client_header = custom_target(
	'xdg-shell-client-protocol.h',
	input: xdg_shell_xml,
	output: 'xdg-shell-client-protocol.h',
	command: [wayland_scanner_prog, 'client-header', '@INPUT@', '@OUTPUT@']
)

xdg_shell_code = custom_target(
	'xdg-shell-protocol.c',
	input: xdg_shell_xml,
	output: 'xdg-shell-protocol.c',
	command: [wayland_scanner_prog, 'private-code', '@INPUT@', '@OUTPUT@']
)

set_parent_test = executable(
	'set-parent-test',
	['set-parent-test.c', xdg_shell_client_header, xdg_shell_code],
	dependencies: [wayland_client],
	include_directories: include_directories('.')
)
