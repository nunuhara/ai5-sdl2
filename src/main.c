/* Copyright (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>

#include "ai5.h"
#include "ai5/game.h"
#include "nulib/file.h"
#include "nulib/string.h"
#include "nulib/utfsjis.h"

#include "asset.h"
#include "audio.h"
#include "cursor.h"
#include "debug.h"
#include "game.h"
#include "gfx.h"
#include "ini.h"
#include "input.h"
#include "memory.h"
#include "vm.h"

#include "../version.h"

#define DEFAULT_PROGRESSIVE_FRAME_TIME 4
#define DEFAULT_MSG_SKIP_DELAY 16
struct config config = {
	.progressive_frame_time = DEFAULT_PROGRESSIVE_FRAME_TIME,
	.msg_skip_delay = DEFAULT_MSG_SKIP_DELAY,
};
bool yuno_eng = false;

static int cfg_handler(void *user, const char *section, const char *name, const char *value)
{
	struct config *config = user;

#define MATCH(s, n) (!strcasecmp(section, s) && !strcasecmp(name, n))
	// [CONFIG]
	if (MATCH("CONFIG", "TITLE")) {
		config->title = sjis_cstring_to_utf8(value, strlen(value));
	} else if (MATCH("CONFIG", "STARTMES")) {
		config->start_mes = string_new(value);
	} else if (MATCH("CONFIG", "VOICE")) {
		config->voice = atoi(value);
	} else if (MATCH("CONFIG", "SOUND")) {
		config->sound = atoi(value);
	} else if (MATCH("CONFIG", "MUSIC")) {
		config->music = atoi(value);
	} else if (MATCH("CONFIG", "EFFECT")) {
		config->effect = atoi(value);
	} else if (MATCH("CONFIG", "SCREEN")) {
		config->screen = atoi(value);
	} else if (MATCH("CONFIG", "bNOTIFY")) {
		config->notify = atoi(value);
	// [FILE]
	} else if (MATCH("FILE", "bARCBG")) {
		config->file.bg.arc = atoi(value);
	} else if (MATCH("FILE", "bARCMES")) {
		config->file.mes.arc = atoi(value);
	} else if (MATCH("FILE", "bARCBGM")) {
		config->file.bgm.arc = atoi(value);
	} else if (MATCH("FILE", "bARCVOICE")) {
		config->file.voice.arc = atoi(value);
	} else if (MATCH("FILE", "bARCEFFECT")) {
		config->file.effect.arc = atoi(value);
	} else if (MATCH("FILE", "bARCDATA")) {
		config->file.data.arc = atoi(value);
	} else if (MATCH("FILE", "bARCPRIV")) {
		config->file.priv.arc = atoi(value);
	} else if (MATCH("FILE", "ARCBGNAME")) {
		config->file.bg.name = string_new(value);
	} else if (MATCH("FILE", "ARCMESNAME")) {
		config->file.mes.name = string_new(value);
	} else if (MATCH("FILE", "ARCBGMNAME")) {
		config->file.bgm.name = string_new(value);
	} else if (MATCH("FILE", "ARCVOICENAME")) {
		config->file.voice.name = string_new(value);
	} else if (MATCH("FILE", "ARCEFFECTNAME")) {
		config->file.effect.name = string_new(value);
	} else if (MATCH("FILE", "ARCDATANAME")) {
		config->file.data.name = string_new(value);
	} else if (MATCH("FILE", "ARCSPECIALNAME")) {
		config->file.priv.name = string_new(value);
	} else if (MATCH("FILE", "CDDRV")) {
		config->file.cddrv = string_new(value);
	// [GRAPHICS]
	} else if (MATCH("GRAPHICS", "bBGTYPE")) {
		config->graphics.bg_type = atoi(value);
	// [MES]
	} else if (MATCH("MES", "bMESTYPE")) {
		config->mes.mes_type = atoi(value);
	// [DATA]
	} else if (MATCH("DATA", "bDATATYPE")) {
		config->data.data_type = atoi(value);
	// [MONITOR]
	} else if (MATCH("MONITOR", "SCREEN")) {
		config->monitor.screen = atoi(value);
	// [VOLUME] / [VOLUMEINFO]
	} else if (MATCH("VOLUME", "MUSIC") || MATCH("VOLUMEINFO", "MUSIC")) {
		config->volume.music = atoi(value);
	} else if (MATCH("VOLUME", "SE") || MATCH("VOLUMEINFO", "SE")) {
		config->volume.se = atoi(value);
	} else if (MATCH("VOLUME", "EFFECT") || MATCH("VOLUMEINFO", "EFFECT")) {
		config->volume.effect = atoi(value);
	} else if (MATCH("VOLUME", "VOICE") || MATCH("VOLUMEINFO", "VOICE")) {
		config->volume.voice = atoi(value);
	// [SOUNDINFO]
	} else if (MATCH("SOUNDINFO", "MUSIC")) {
		config->soundinfo.music = atoi(value);
	} else if (MATCH("SOUNDINFO", "VOICE")) {
		config->soundinfo.voice = atoi(value);
	} else if (MATCH("SOUNDINFO", "EFFECT")) {
		config->soundinfo.effect = atoi(value);
	} else {
		WARNING("Unknown INI value: %s.%s", section, name);
		return 0;
	}
#undef MATCH
	return 1;
}

static void usage(void)
{
	printf("Usage: ai5 [options] [inifile-or-directory]\n");
	printf("    --cg-load-frame-time=<ms>  Set the frame time for progressive CG loads (default: %u)\n",
			DEFAULT_PROGRESSIVE_FRAME_TIME);
	printf("    -d, --debug                Start in the debugger REPL\n");
	printf("    --font                     Specify the font\n");
	printf("    --font-face=<n>            Specify the font face index\n");
	printf("    --game=<game>              Specify the game to run\n");
	printf("                               (valid options are: yuno, yuno-eng)\n");
	printf("    -h, --help                 Display this message and exit\n");
	printf("    --msg-skip-delay=<ms>      Set the message skip delay time (default: %u)\n",
			DEFAULT_MSG_SKIP_DELAY);
	printf("    --version                  Display the AI5-SDL2 version and exit\n");
}

static _Noreturn void _usage_error(const char *fmt, ...)
{
	usage();
	puts("");

	va_list ap;
	va_start(ap, fmt);
	sys_verror(fmt, ap);
}
#define usage_error(fmt, ...) _usage_error("Error: " fmt "\n", ##__VA_ARGS__)

static void set_game(const char *name)
{
	if (!strcmp(name, "yuno-eng")) {
		yuno_eng = true;
		name = "yuno";
	}
	ai5_set_game(name);
	switch (ai5_target_game) {
#ifdef BUILD_DEBUG
	case GAME_AI_SHIMAI:
		game = &game_ai_shimai;
		break;
	case GAME_ISAKU:
		game = &game_isaku;
		break;
	case GAME_SHANGRLIA:
		game = &game_shangrlia;
		break;
#endif
	case GAME_YUNO:
		game = &game_yuno;
		break;
	default:
		sys_error("Game \"%s\" not supported", name);
	}
}

static bool set_game_from_config(void)
{
	const char *name = NULL;
	if (!strcmp(config.title, "～この世の果てで恋を唄う少女～")) {
		name = "yuno";
	} else if (!strcmp(config.title, "YU-NO - The Girl that Chants Love at the Edge of the World")) {
		name = "yuno-eng";
	} else if (!strcmp(config.title, "ｼｬﾝｸﾞﾘﾗ")) {
		name = "shangrlia";
	} else if (!strcmp(config.title, "ｼｬﾝｸﾞﾘﾗ2")) {
		name = "shangrlia2";
	} else if (!strcmp(config.title, "遺作９８")) {
		name = "isaku";
	} else if (!strcmp(config.title, "Isaku98")) {
		name = "isaku";
	} else if (!strcmp(config.title, "AISHIMAI")) {
		name = "aishimai";
	}
	if (!name)
		return false;
	set_game(name);
	return true;
}

enum {
	LOPT_HELP = 256,
	LOPT_VERSION,
	LOPT_CG_LOAD_FRAME_TIME,
	LOPT_DEBUG,
	LOPT_FONT,
	LOPT_FONT_FACE,
	LOPT_GAME,
	LOPT_MSG_SKIP_DELAY,
};

int main(int argc, char *argv[])
{
	bool have_game = false;
	char *ini_name = NULL;
	bool debug = false;
	char *font_path = NULL;
	int font_face = -1;

	while (1) {
		static struct option long_options[] = {
			{ "game", required_argument, 0, LOPT_GAME },
			{ "cg-load-frame-time", required_argument, 0, LOPT_CG_LOAD_FRAME_TIME },
			{ "debug", no_argument, 0, LOPT_DEBUG },
			{ "font", required_argument, 0, LOPT_FONT },
			{ "font-face", required_argument, 0, LOPT_FONT_FACE },
			{ "help", no_argument, 0, LOPT_HELP },
			{ "msg-skip-delay", required_argument, 0, LOPT_MSG_SKIP_DELAY },
			{ "version", no_argument, 0, LOPT_VERSION },
			{0}
		};
		int option_index = 0;
		int c = getopt_long(argc, argv, "hd", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
		case LOPT_HELP:
			usage();
			return 0;
		case LOPT_VERSION:
			NOTICE("AI5-SDL2 version %s", AI5_SDL2_VERSION);
			sys_exit(0);
			break;
		case LOPT_GAME:
			set_game(optarg);
			have_game = true;
			break;
		case LOPT_CG_LOAD_FRAME_TIME:
			config.progressive_frame_time = clamp(0, 100, atoi(optarg));
			break;
		case 'd':
		case LOPT_DEBUG:
			debug = true;
			debug_on_error = true;
			debug_on_F12 = true;
			break;
		case LOPT_FONT:
			font_path = optarg;
			break;
		case LOPT_FONT_FACE:
			font_face = atoi(optarg);
			break;
		case LOPT_MSG_SKIP_DELAY:
			config.msg_skip_delay = clamp(0, 5000, atoi(optarg));
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 1)
		usage_error("Too many arguments");

	if (argc > 0) {
		ustat s;
		if (stat_utf8(argv[0], &s))
			usage_error("Couldn't read \"%s\": %s", argv[0], strerror(errno));
		if (S_ISDIR(s.st_mode)) {
			// if argv[0] is a directory, chdir to it
			if (chdir(argv[0]))
				ERROR("chdir(\"%s\"): %s", argv[0], strerror(errno));
		} else if (S_ISREG(s.st_mode)) {
			// if argv[0] is a regular file, set as ini filename and chdir
			// to directory
			char *dir = path_dirname(argv[0]);
			char *base = path_basename(argv[0]);
			if (chdir(dir))
				ERROR("chdir(\"%s\"): %s", dir, strerror(errno));
			ini_name = strdup(base);
		} else {
			usage_error("\"%s\" isn't a regular file or directory", argv[0]);
		}
	}

	// get ini filename if not specified
	if (!ini_name) {
		// try AI5ENG.INI first (YU-NO Eng TL)
		ini_name = path_get_icase("AI5ENG.INI");
		if (!ini_name)
			ini_name = path_get_icase("AI5WIN.INI");
	}
	if (!ini_name)
		usage_error("Couldn't find AI5WIN.INI (not a game directory?)");

	// parse ini file
	if (ini_parse(ini_name, cfg_handler, &config) < 0)
		sys_error("Failed to read INI file \"%s\"\n", ini_name);
	if (!config.start_mes)
		config.start_mes = string_new("MAIN.MES");
#define DEFAULT_NAME(f, n) if (f.arc && !f.name) { f.name = string_new(n); }
	DEFAULT_NAME(config.file.bg, "BG.ARC");
	DEFAULT_NAME(config.file.mes, "MES.ARC");
	DEFAULT_NAME(config.file.bgm, "BGM.ARC");
	DEFAULT_NAME(config.file.voice, "VOICE.ARC");
	DEFAULT_NAME(config.file.effect, "BGM.ARC");
	DEFAULT_NAME(config.file.data, "DATA.ARC");
	DEFAULT_NAME(config.file.priv, "PRIV.ARC");
#undef DEFAULT_NAME

	string exe_name = file_replace_extension(ini_name, "EXE");
	config.exe_path = path_get_icase(exe_name);
	string_free(exe_name);
	free(ini_name);

	if (!have_game && !set_game_from_config()) {
		usage();
		puts("");
		puts("Valid game names are:");
		for (unsigned i = 0; i < ARRAY_SIZE(ai5_games); i++) {
			printf("    %-11s - %s\n", ai5_games[i].name, ai5_games[i].description);
		}
		printf("    %-11s - %s\n", "yuno-eng", "English translation of YU-NO");
		puts("");
		sys_error("Error: No game specified");
	}

	// intitialize subsystems
	srand(time(NULL));
	asset_init();
	game->mem_init();
	gfx_init(config.title);
	gfx_text_init(font_path, font_face);
	input_init();
	cursor_init(config.exe_path);
	audio_init();
	vm_init();

	if (game->init)
		game->init();

	// execute start mes file
	vm_load_mes(config.start_mes);
	if (debug)
		dbg_repl();
	vm_exec();
	return 0;
}
