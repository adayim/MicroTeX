src = []
inc = []

install_headerfiles = get_option('TARGET_DEVEL')

microtex_src = ['microtex.cpp']
src += microtex_src

microtex_inc = include_directories('.')
inc += microtex_inc

subdir('atom')
src += atom_src

subdir('box')
src += box_src

subdir('core')
src += core_src

subdir('env')
src += env_src

subdir('graphic')
src += graphic_src

subdir('macro')
src += macro_src

subdir('otf')
src += otf_src

subdir('unimath')
src += unimath_src

subdir('utils')
src += utils_src

subdir('render')
src += render_src

subdir('front')
src += front_src

microtex_version = meson.project_version().split('.')
config_h = configure_file(
    input: 'microtexconfig.h.in',
    output: 'microtexconfig.h',
    configuration: {
        'PROJECT_VERSION_MAJOR': microtex_version[0],
        'PROJECT_VERSION_MINOR': microtex_version[1],
        'PROJECT_VERSION_PATCH': microtex_version[2],
    },
    format: 'cmake@'
)

# A font is read straight from its OpenType file, which needs FreeType.
microtex_deps = [dependency('freetype2')]

# Bidirectional reordering when FriBidi is there; without it text keeps
# logical order.
fribidi_dep = dependency('fribidi', required: false)
if fribidi_dep.found()
	microtex_deps += fribidi_dep
	add_project_arguments('-DHAVE_FRIBIDI', language: 'cpp')
endif

microtex_lib = library('microtex', src,
	dependencies: microtex_deps,
	include_directories: inc,
	version: meson.project_version(),
	soversion: microtex_api_version,
	install: true
)

microtex_dep = declare_dependency(
	link_with: microtex_lib,
	include_directories: inc,
	version: meson.project_version()
)

if get_option('TARGET_DEVEL')
    pkgconfig.generate(microtex_lib,
        version: meson.project_version(),
        name: 'microtex',
        filebase: 'microtex',
        subdirs: 'microtex',
        description: 'A micro, fast, cross-platform, and embeddable LaTeX rendering library'
    )
endif

if install_headerfiles
	install_headers([
		'microtexexport.h',
		'microtex.h',
		config_h
	], subdir: 'microtex')
endif
