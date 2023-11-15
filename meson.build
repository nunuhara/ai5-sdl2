project('ai5_sdl2', 'c', default_options : ['c_std=c11'])
add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')
add_project_arguments('-DAI5_DATA_DIR="' + get_option('prefix') / get_option('datadir') / 'ai5-sdl2"', language : 'c')

static_libs = false
if host_machine.system() == 'windows'
  static_libs = true
endif

sdl2 = dependency('sdl2', static : static_libs)
sdl2_ttf = dependency('SDL2_ttf', static : static_libs)

libai5_proj = subproject('libai5')
libai5_dep = libai5_proj.get_variable('libai5_dep')

incdirs = include_directories('include')

sources = [
  'src/asset.c',
  'src/gfx.c',
  'src/ini.c',
  'src/input.c',
  'src/main.c',
  'src/memory.c',
  'src/text.c',
  'src/vm.c',
]

install_subdir('fonts', install_dir : get_option('datadir') / 'ai5-sdl2')

executable('ai5', sources,
  dependencies : [libai5_dep, sdl2, sdl2_ttf],
  c_args : ['-Wno-unused-parameter'],
  include_directories : incdirs,
  install : true)
