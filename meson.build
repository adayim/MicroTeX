project('microtex', 'cpp', 'c',
	version : '1.0.0',
	default_options : ['buildtype=release', 'warning_level=0', 'cpp_std=c++17']
)

cpp = meson.get_compiler('cpp')

# the API (and ABI) is compatiable if API version is the same
microtex_api_version = 1

if get_option('TARGET_DEVEL')
  pkgconfig = import('pkgconfig')
endif

subdir('lib')

# The graphics backends are inherited from upstream MicroTeX, and the engine
# does not need one (see example/svg).
if get_option('CAIRO')
  subdir('platform/cairo')
endif

if get_option('QT')
  subdir('platform/qt')
endif

gdi_dep = cpp.find_library('gdiplus', required: get_option('GDI'))
if gdi_dep.found() and cpp.has_header('windows.h')
  subdir('platform/gdi_win')
  if get_option('EXAMPLE_WIN32')
    subdir('example/win32')
  endif
else
  if get_option('EXAMPLE_WIN32')
    error('EXAMPLE_WIN32 was enabled, but it requires GDI platform')
  endif
endif
