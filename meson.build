project('ai5_sdl2', 'c', default_options : ['c_std=c11', 'b_ndebug=if-release'])
add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')
add_project_arguments('-DAI5_DATA_DIR="' + get_option('prefix') / get_option('datadir') / 'ai5-sdl2"', language : 'c')

static_libs = false
static_link_args = []
embed_fonts = false
no_embed_noto = false
if host_machine.system() == 'windows'
  static_libs = true
  static_link_args = ['-static', '-lstdc++']
  embed_fonts = true
  no_embed_noto = true
endif

libm = meson.get_compiler('c').find_library('m', required: false)
sdl2 = dependency('sdl2', static : static_libs)
sdl2_ttf = dependency('SDL2_ttf', static : static_libs)

avcodec = dependency('libavcodec', required: false, static : static_libs)
avformat = dependency('libavformat', required: false, static : static_libs)
avutil = dependency('libavutil', required: false, static : static_libs)
swscale = dependency('libswscale', required: false, static : static_libs)

libai5_proj = subproject('libai5')
libai5_dep = libai5_proj.get_variable('libai5_dep')

incdirs = include_directories('include')

version_h = vcs_tag(fallback : 'unknown',
                    input : 'version.h.in',
                    output : 'version.h')

sources = [
  version_h,
  'src/aishimai.c',
  'src/anim.c',
  'src/asset.c',
  'src/backlog.c',
  'src/beyond.c',
  'src/classics.c',
  'src/cmdline.c',
  'src/cursor.c',
  'src/debug.c',
  'src/doukyuusei.c',
  'src/dungeon.c',
  'src/effect.c',
  'src/gfx.c',
  'src/ini.c',
  'src/input.c',
  'src/isaku.c',
  'src/kakyuusei.c',
  'src/main.c',
  'src/map.c',
  'src/menu.c',
  'src/popup_menu.c',
  'src/savedata.c',
  'src/shangrlia.c',
  'src/sys.c',
  'src/text.c',
  'src/texthook.c',
  'src/vm.c',
  'src/yuno.c',
]

if embed_fonts
  xxd = find_program('xxd')
  packfont = generator(xxd,
    output : '@BASENAME@.c',
    capture : true,
    arguments : ['-i', '@EXTRA_ARGS@', '@INPUT@'])
  sources += packfont.process('fonts/DotGothic16-Regular.ttf', extra_args : ['-n', 'font_dotgothic'])
  sources += packfont.process('fonts/Kosugi-Regular.ttf', extra_args : ['-n', 'font_kosugi'])
  sources += packfont.process('fonts/wine_tahoma.ttf', extra_args : ['-n', 'font_tahoma'])
  add_project_arguments('-DEMBED_DOTGOTHIC', language : 'c')
  add_project_arguments('-DEMBED_KOSUGI', language : 'c')
  add_project_arguments('-DEMBED_TAHOMA', language : 'c')
  message('Embedding fonts/DotGothic16-Regular.ttf')
  message('Embedding fonts/Kosugi-Regular.ttf')
  message('Embedding fonts/wine_tahoma.ttf')
  if not no_embed_noto
    sources += packfont.process('fonts/NotoSansJP-Thin.ttf', extra_args : ['-n', 'font_noto'])
    add_project_arguments('-DEMBED_NOTO', language : 'c')
    message('Embedding fonts/NotoSansJP-Thin.ttf')
  endif
endif

deps = [libai5_dep, libm, sdl2, sdl2_ttf]
if avcodec.found() and avformat.found() and avutil.found() and swscale.found()
  add_project_arguments('-DHAVE_FFMPEG', language : 'c')
  sources += 'src/movie.c'
  deps += [avcodec, avformat, avutil, swscale]
endif

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

winsys = 'console'
if get_option('buildtype').startswith('release')
  winsys = 'windows'
endif

executable('ai5', sources,
  dependencies : deps,
  c_args : ['-Wno-unused-parameter'],
  link_args : static_link_args,
  include_directories : incdirs,
  win_subsystem : winsys,
  install : true)
