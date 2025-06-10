glesv2 = dependency('glesv2', required: true)
egl = dependency('egl', required: true)

examples = {
	'egl_render_info': {
		'src': 'egl_render_info.c',
		'dep': [wayland_egl, egl],
	},
}

foreach name, element : examples
	executable(
		name,
		element.get('src', []),
		dependencies: [wayland_client, element.get('dep', [])],
	)
endforeach
