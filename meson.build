project('ai5_sdl2', 'c', default_options : ['c_std=c11'])
add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')
add_project_arguments('-DAI5_DATA_DIR="' + get_option('prefix') / get_option('datadir') / 'ai5-sdl2"', language : 'c')

static_libs = false
if host_machine.system() == 'windows'
  static_libs = true
endif

libm = meson.get_compiler('c').find_library('m', required: false)
sdl2 = dependency('sdl2', static : static_libs)
sdl2_ttf = dependency('SDL2_ttf', static : static_libs)
sdl2_mixer = dependency('SDL2_mixer', static : static_libs)
sndfile = dependency('sndfile', static : static_libs)

libai5_proj = subproject('libai5')
libai5_dep = libai5_proj.get_variable('libai5_dep')

incdirs = include_directories('include')

sources = [
  'src/anim.c',
  'src/asset.c',
  'src/cmdline.c',
  'src/cursor.c',
  'src/debug.c',
  'src/effect.c',
  'src/gfx.c',
  'src/ini.c',
  'src/input.c',
  'src/isaku.c',
  'src/main.c',
  'src/menu.c',
  'src/savedata.c',
  'src/shangrlia.c',
  'src/sys.c',
  'src/text.c',
  'src/util.c',
  'src/vm.c',
  'src/yuno.c',
]

deps = [libai5_dep, libm, sdl2, sdl2_ttf]

if get_option('sdl_mixer').allowed()
  add_project_arguments('-DUSE_SDL_MIXER', language : 'c')
  deps += dependency('SDL2_mixer', static : static_libs)
  sources += 'src/audio_sdl_mixer.c'
else
  deps += dependency('sndfile', static : static_libs)
  sources += 'src/audio.c'
  sources += 'src/audio_mixer.c'
endif

install_subdir('fonts', install_dir : get_option('datadir') / 'ai5-sdl2')

executable('ai5', sources,
  dependencies : deps,
  c_args : ['-Wno-unused-parameter'],
  include_directories : incdirs,
  install : true)
