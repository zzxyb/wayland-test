wayland_protocols = dependency('wayland-protocols')
wayland_scanner = dependency('wayland-scanner', native: true)
wayland_scanner_prog = find_program(wayland_scanner.get_variable('wayland_scanner'), required: true)

wayland_protocols_datadir = wayland_protocols.get_variable('pkgdatadir')

alpha_modifier_xml = wayland_protocols_datadir + '/staging/alpha-modifier/alpha-modifier-v1.xml'
xdg_shell_xml = wayland_protocols_datadir + '/stable/xdg-shell/xdg-shell.xml'
xdg_decoration_xml = wayland_protocols_datadir + '/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml'

alpha_modifier_client_header = custom_target(
	'alpha-modifier-v1-client-protocol.h',
	input: alpha_modifier_xml,
	output: 'alpha-modifier-v1-client-protocol.h',
	command: [wayland_scanner_prog, 'client-header', '@INPUT@', '@OUTPUT@']
)

xdg_shell_client_header = custom_target(
	'xdg-shell-client-protocol.h',
	input: xdg_shell_xml,
	output: 'xdg-shell-client-protocol.h',
	command: [wayland_scanner_prog, 'client-header', '@INPUT@', '@OUTPUT@']
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

xdg_decoration_code = custom_target(
	'xdg-decoration-unstable-v1-protocol.c',
	input: xdg_decoration_xml,
	output: 'xdg-decoration-unstable-v1-protocol.c',
	command: [wayland_scanner_prog, 'private-code', '@INPUT@', '@OUTPUT@']
)

alpha_modifier_code = custom_target(
	'alpha-modifier-v1-protocol.c',
	input: alpha_modifier_xml,
	output: 'alpha-modifier-v1-protocol.c',
	command: [wayland_scanner_prog, 'private-code', '@INPUT@', '@OUTPUT@']
)

alpha_modifier_test = executable(
	'alpha-modifier-test',
	['alpha-modifier-test.c', alpha_modifier_client_header, alpha_modifier_code,
	 xdg_shell_client_header, xdg_shell_code,
	 xdg_decoration_client_header, xdg_decoration_code],
	dependencies: [wayland_client],
	include_directories: include_directories('.')
)
