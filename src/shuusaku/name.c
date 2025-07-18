/* Copyright (C) 2025 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include "nulib.h"
#include "nulib/vector.h"
#include "nulib/utfsjis.h"
#include "ai5/mes.h"

#include "anim.h"
#include "cursor.h"
#include "gfx.h"
#include "gfx_private.h"
#include "input.h"
#include "memory.h"
#include "vm.h"
#include "vm_private.h"

#include "shuusaku.h"

#define CHARS_X 80
#define CHARS_Y 32
#define CHARS_W 480
#define CHARS_H 288

struct button {
	SDL_Rect parts_normal;
	SDL_Rect parts_pressed;
	SDL_Rect pos;
	bool hold;
	uint32_t data;
	void(*pressed)(struct button*);
};

struct radio_group;

struct radio_button {
	struct button btn;
	void(*pressed)(struct radio_button*);
	struct radio_group *group;
};

struct radio_group {
	struct radio_button buttons[3];
	unsigned down;
};

struct text_input {
	SDL_Point pos;
	unsigned cursor;
	unsigned nr_chars;
	uint32_t chars[5];
};

static struct text_input myouji_input = {
	.pos = { 136, 408 },
};

static struct text_input namae_input = {
	.pos = { 280, 408 },
};

static void radio_button_pressed(struct button *btn);

static void kana_pressed(struct radio_button *btn);
static void kanji_pressed(struct radio_button *btn);
static void kigou_pressed(struct radio_button *btn);
static struct radio_group moji_group = {
	.buttons = {
		{
			{
				.parts_normal = { 0, 24, 64, 24 },
				.parts_pressed = { 0, 0, 64, 24 },
				.pos = { 72, 368, 64, 24 },
				.hold = true,
				.pressed = radio_button_pressed
			},
			.pressed = kana_pressed,
			.group = &moji_group
		},
		{
			{
				.parts_normal = { 64, 24, 64, 24 },
				.parts_pressed = { 64, 0, 64, 24 },
				.pos = { 144, 368, 64, 24 },
				.hold = true,
				.pressed = radio_button_pressed
			},
			.pressed = kanji_pressed,
			.group = &moji_group
		},
		{
			{
				.parts_normal = { 128, 24, 64, 24 },
				.parts_pressed = { 128, 0, 64, 24 },
				.pos = { 216, 368, 64, 24 },
				.hold = true,
				.pressed = radio_button_pressed
			},
			.pressed = kigou_pressed,
			.group = &moji_group
		}
	},
};

static void left_arrow_pressed(struct button *btn);
static struct button button_left_arrow = {
	.parts_normal = { 192, 24, 64, 24 },
	.parts_pressed = { 192, 0, 64, 24 },
	.pos = { 288, 368, 64, 24 },
	.pressed = left_arrow_pressed
};

static void right_arrow_pressed(struct button *btn);
static struct button button_right_arrow = {
	.parts_normal = { 256, 24, 64, 24 },
	.parts_pressed = { 256, 0, 64, 24 },
	.pos = { 360, 368, 64, 24 },
	.pressed = right_arrow_pressed
};

static void modoru_pressed(struct button *btn);
static struct button button_modoru = {
	.parts_normal = { 320, 24, 64, 24 },
	.parts_pressed = { 320, 0, 64, 24 },
	.pos = { 432, 368, 64, 24 },
	.pressed = modoru_pressed
};

static void kettei_pressed(struct button *btn);
static struct button button_kettei = {
	.parts_normal = { 384, 24, 64, 24 },
	.parts_pressed = { 384, 0, 64, 24 },
	.pos = { 504, 368, 64, 24 },
	.pressed = kettei_pressed
};

static void myouji_pressed(struct radio_button *btn);
static void namae_pressed(struct radio_button *btn);
static struct radio_group name_group = {
	.buttons = {
		{
			{
				.parts_normal = { 448, 24, 64, 24 },
				.parts_pressed = { 448, 0, 64, 24 },
				.pos = { 416, 408, 64, 24 },
				.hold = true,
				.pressed = radio_button_pressed
			},
			.pressed = myouji_pressed,
			.group = &name_group
		},
		{
			{
				.parts_normal = { 512, 24, 64, 24 },
				.parts_pressed = { 512, 0, 64, 24 },
				.pos = { 488, 408, 64, 24 },
				.hold = true,
				.pressed = radio_button_pressed
			},
			.pressed = namae_pressed,
			.group = &name_group
		}
	}
};

static void char_button_pressed(struct button *_button);

static struct button *buttons[] = {
	&moji_group.buttons[0].btn,
	&moji_group.buttons[1].btn,
	&moji_group.buttons[2].btn,
	&button_left_arrow,
	&button_right_arrow,
	&button_modoru,
	&button_kettei,
	&name_group.buttons[0].btn,
	&name_group.buttons[1].btn,
};

static void nav_jump_pressed(struct button *button);
static void nav_up_pressed(struct button *button);
static void nav_down_pressed(struct button *button);
struct button kanji_nav_btn[] = {
	{
		.parts_normal = { 0, 64, 32, 16 },
		.parts_pressed = { 0, 48, 32, 16 },
		.pos = { 80, 328, 32, 16 },
		.data = 0,
		.pressed = nav_jump_pressed
	}, {
		.parts_normal = { 32, 64, 32, 16 },
		.parts_pressed = { 32, 48, 32, 16 },
		.pos = { 116, 328, 32, 16 },
		.data = 14,
		.pressed = nav_jump_pressed
	}, {
		.parts_normal = { 64, 64, 32, 16 },
		.parts_pressed = { 64, 48, 32, 16 },
		.pos = { 152, 328, 32, 16 },
		.data = 58,
		.pressed = nav_jump_pressed
	}, {
		.parts_normal = { 96, 64, 32, 16 },
		.parts_pressed = { 96, 48, 32, 16 },
		.pos = { 188, 328, 32, 16 },
		.data = 104,
		.pressed = nav_jump_pressed
	}, {
		.parts_normal = { 128, 64, 32, 16 },
		.parts_pressed = { 128, 48, 32, 16 },
		.pos = { 224, 328, 32, 16 },
		.data = 129,
		.pressed = nav_jump_pressed
	}, {
		.parts_normal = { 160, 64, 32, 16 },
		.parts_pressed = { 160, 48, 32, 16 },
		.pos = { 260, 328, 32, 16 },
		.data = 136,
		.pressed = nav_jump_pressed
	}, {
		.parts_normal = { 192, 64, 32, 16 },
		.parts_pressed = { 192, 48, 32, 16 },
		.pos = { 296, 328, 32, 16 },
		.data = 160,
		.pressed = nav_jump_pressed
	}, {
		.parts_normal = { 224, 64, 32, 16 },
		.parts_pressed = { 224, 48, 32, 16 },
		.pos = { 332, 328, 32, 16 },
		.data = 167,
		.pressed = nav_jump_pressed
	}, {
		.parts_normal = { 256, 64, 32, 16 },
		.parts_pressed = { 256, 48, 32, 16 },
		.pos = { 368, 328, 32, 16 },
		.data = 173,
		.pressed = nav_jump_pressed
	}, {
		.parts_normal = { 288, 64, 32, 16 },
		.parts_pressed = { 288, 48, 32, 16 },
		.pos = { 404, 328, 32, 16 },
		.data = 184,
		.pressed = nav_jump_pressed
	}, {
		.parts_normal = { 320, 64, 48, 16 },
		.parts_pressed = { 320, 48, 48, 16 },
		.pos = { 460, 328, 48, 16 },
		.pressed = nav_up_pressed
	}, {
		.parts_normal = { 368, 64, 48, 16 },
		.parts_pressed = { 368, 48, 48, 16 },
		.pos = { 512, 328, 48, 16 },
		.pressed = nav_down_pressed
	}
};

typedef vector_t(struct button) button_list;
typedef vector_t(button_list) button_list_list;

static SDL_Surface *kana_s;
static button_list kana_btn;

static SDL_Surface *kanji_s;
static button_list_list kanji_btn;
static unsigned kanji_row = 0;

static SDL_Surface *kigou_s;
static button_list kigou_btn;

static bool kettei = false;

static SDL_Surface *alloc_surface(unsigned w, unsigned h)
{
	SDL_Surface *s;
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, s, 0, w, h,
			GFX_INDEXED_BPP, GFX_INDEXED_FORMAT);
	SDL_CALL(SDL_FillRect, s, NULL, MASK_COLOR);
	return s;
}

// This returns the code point encoded at **s and advances *s to point to the
// next character. Thus it can easily be used in a loop.
uint32_t decode_utf8_code_point(const char **s)
{
	int k = **s ? __builtin_clz(~(**s << 24)) : 0; // Count # of leading 1 bits.
	int mask = (1 << (8 - k)) - 1;                 // All 1s with k leading 0s.
	int value = **s & mask;
	// k = 0 for one-byte code points; otherwise, k = #total bytes.
	for (++(*s), --k; k > 0 && (((**s) & 0xc0) == 0x80); --k, ++(*s)) {
		value <<= 6;
		value += (**s & 0x3F);
	}
	return value;
}

static void char_button_init(struct button *btn, int x, int y, uint32_t ch)
{
	btn->parts_normal = (SDL_Rect) { 0, 80, 24, 24 };
	btn->parts_pressed = (SDL_Rect) { 24, 80, 24, 24 };
	btn->pos = (SDL_Rect) { CHARS_X + x, CHARS_Y + y, 24, 24 };
	btn->hold = false;
	btn->pressed = char_button_pressed;
	btn->data = ch;
}

// {{{ Kana

static const char *kana[] = {
	"あいうえお", "かきくけこ", "さしすせそ",
	"たちつてと", "なにぬねの", "はひふへほ",
	"まみむめも", "や〇ゆ〇よ", "らりるれろ",
	"わゐゑをん", "がぎぐげご", "ざじずぜぞ",
	"だぢづでど", "ばびぶべぼ", "ぱぴぷぺぽ",
	"ぁぃぅぇぉ", "っゃゅょゎ", "〇〇〇〇〇",
	"アイウエオ", "カキクケコ", "サシスセソ",
	"タチツテト", "ナニヌネノ", "ハヒフヘホ",
	"マミムメモ", "ヤ〇ユ〇ヨ", "ラリルレロ",
	"ワヰヱヲン", "ガギグゲゴ", "ザジズゼゾ",
	"ダヂヅデド", "バビブベボ", "パピプペポ",
	"ァィゥェォ", "ッャュョヮ", "ヴヵヶ〇〇",
};

static void make_kana_screen(void)
{
	SDL_Surface *s = alloc_surface(CHARS_W, CHARS_H);
	button_list buttons = vector_initializer;

	for (int i = 0; i < ARRAY_SIZE(kana); i++) {
		// XXX: x,y is corner of button; text is inset 2 pixels
		unsigned x = 48 + (i % 3) * 144;
		unsigned y = (i / 3) * 24;
		const char *block = kana[i];
		for (int col = 0; col < 5 && *block; col++, x += 24) {
			// decode char
			uint32_t ch = decode_utf8_code_point(&block);
			if (ch == 0x3007) // == '〇'
				continue;
			// draw glyph
			_gfx_text_draw_glyph(s, x+2, y+2, ch);
			// create button
			struct button *btn = vector_pushp(struct button, buttons);
			char_button_init(btn, x, y, ch);
		}
	}

	kana_s = s;
	kana_btn = buttons;
}

// }}} Kana
// {{{ Kigou

static const char *kigou[] = {
	"ＡＢＣＤＥＦＧＨＩＪＫＬＭＮＯＰＱＲ",
	"ＳＴＵＶＷＸＹＺ",
	"ａｂｃｄｅｆｇｈｉｊｋｌｍｎｏｐｑｒ",
	"ｓｔｕｖｗｘｙｚ",
	"０１２３４５６７８９",
	"・￥　−：；",
	//"☆★○●◎◇◆□■△▲▽▼",
};

static void make_kigou_screen(void)
{
	kigou_s = alloc_surface(CHARS_W, CHARS_H);
	kigou_btn = (button_list)vector_initializer;

	for (int i = 0; i < ARRAY_SIZE(kigou); i++) {
		unsigned x = 48;
		unsigned y = i * 24;
		const char *line = kigou[i];
		for (int col = 0; col < 18 && *line; col++, x += 24) {
			// decode char
			uint32_t ch = decode_utf8_code_point(&line);
			// draw glyph
			_gfx_text_draw_glyph(kigou_s, x+2, y+2, ch);
			// create button
			struct button *btn = vector_pushp(struct button, kigou_btn);
			char_button_init(btn, x, y, ch);
		}
	}
}

// }}} Kigou
// {{{ Kanji

struct kanji_block {
	const char *kana;
	const char *kanji;
};

static const struct kanji_block kanji[] = {
	{
		"あ",
		"亜唖娃阿哀愛挨姶逢葵茜穐悪握渥旭葦芦"
		"鯵梓圧斡扱宛姐虻飴絢綾鮎或粟袷安庵按"
		"暗案闇鞍杏"
	}, {
		"い",
		"以伊位依偉囲夷委威尉惟意慰易椅為畏異"
		"移維緯胃萎衣謂違遺医井亥域育郁磯一壱"
		"溢逸稲茨芋鰯允印咽員因姻引飲淫胤蔭院"
		"陰隠韻吋"
	}, {
		"う",
		"右宇烏羽迂雨卯鵜窺丑碓臼渦嘘唄欝蔚鰻"
		"姥厩浦瓜閏噂云運雲"
	}, {
		"え",
		"荏餌叡営嬰影映曳栄永泳洩瑛盈穎頴英衛"
		"詠鋭液疫益駅悦謁越閲榎厭円園堰奄宴延"
		"怨掩援沿演炎焔煙燕猿縁艶苑薗遠鉛鴛塩"
	}, {
		"お",
		"於汚甥凹央奥往応押旺横欧殴王翁襖鴬鴎"
		"黄岡沖荻億屋憶臆桶牡乙俺卸恩温穏音"
	}, {
		"か",
		"下化仮何伽価佳加可嘉夏嫁家寡科暇果架"
		"歌河火珂禍禾稼箇花苛茄荷華菓蝦課嘩貨"
		"迦過霞蚊俄峨我牙画臥芽蛾賀雅餓駕介会"
		"解回塊壊廻快怪悔恢懐戒拐改魁晦械海灰"
		"界皆絵芥蟹開階貝凱劾外咳害崖慨概涯碍"
		"蓋街該鎧骸浬馨蛙垣柿蛎鈎劃嚇各廓拡撹"
		"格核殻獲確穫覚角赫較郭閣隔革学岳楽額"
		"顎掛笠樫橿梶鰍潟割喝恰括活渇滑葛褐轄"
		"且鰹叶椛樺鞄株兜竃蒲釜鎌噛鴨栢茅萱粥"
		"刈苅瓦乾侃冠寒刊勘勧巻喚堪姦完官寛干"
		"幹患感慣憾換敢柑桓棺款歓汗漢澗潅環甘"
		"監看竿管簡緩缶翰肝艦莞観諌貫還鑑間閑"
		"関陥韓館舘丸含岸巌玩癌眼岩翫贋雁頑顔"
		"願"
	}, {
		"き",
		"企伎危喜器基奇嬉寄岐希幾忌揮机旗既期"
		"棋棄機帰毅気汽畿祈季稀紀徽規記貴起軌"
		"輝飢騎鬼亀偽儀妓宜戯技擬欺犠疑祇義蟻"
		"誼議掬菊鞠吉吃喫桔橘詰砧杵黍却客脚虐"
		"逆丘久仇休及吸宮弓急救朽求汲泣灸球究"
		"窮笈級糾給旧牛去居巨拒拠挙渠虚許距鋸"
		"漁禦魚亨享京供侠僑兇競共凶協匡卿叫喬"
		"境峡強彊怯恐恭挟教橋況狂狭矯胸脅興蕎"
		"郷鏡響饗驚仰凝尭暁業局曲極玉桐粁僅勤"
		"均巾錦斤欣欽琴禁禽筋緊芹菌衿襟謹近金"
		"吟銀"
	}, {
		"く",
		"九倶句区狗玖矩苦躯駆駈駒具愚虞喰空偶"
		"寓遇隅串櫛釧屑屈掘窟沓靴轡窪熊隈粂栗"
		"繰桑鍬勲君薫訓群軍郡"
	}, {
		"け",
		"卦袈祁係傾刑兄啓圭珪型契形径恵慶慧憩"
		"掲携敬景桂渓畦稽系経継繋罫茎荊蛍計詣"
		"警軽頚鶏芸迎鯨劇戟撃激隙桁傑欠決潔穴"
		"結血訣月件倹倦健兼券剣喧圏堅嫌建憲懸"
		"拳捲検権牽犬献研硯絹県肩見謙賢軒遣鍵"
		"険顕験鹸元原厳幻弦減源玄現絃舷言諺限"
	}, {
		"こ",
		"乎個古呼固姑孤己庫弧戸故枯湖狐糊袴股"
		"胡菰虎誇跨鈷雇顧鼓五互伍午呉吾娯後御"
		"悟梧檎瑚碁語誤護醐乞鯉交佼侯候倖光公"
		"功効勾厚口向后喉坑垢好孔孝宏工巧巷幸"
		"広庚康弘恒慌抗拘控攻昂晃更杭校梗構江"
		"洪浩港溝甲皇硬稿糠紅紘絞綱耕考肯肱腔"
		"膏航荒行衡講貢購郊酵鉱砿鋼閤降項香高"
		"鴻剛劫号合壕拷濠豪轟麹克刻告国穀酷鵠"
		"黒獄漉腰甑忽惚骨狛込此頃今困坤墾婚恨"
		"懇昏昆根梱混痕紺艮魂"
	}, {
		"さ",
		"些佐叉唆嵯左差査沙瑳砂詐鎖裟坐座挫債"
		"催再最哉塞妻宰彩才採栽歳済災采犀砕砦"
		"祭斎細菜裁載際剤在材罪財冴坂阪堺榊肴"
		"咲崎埼碕鷺作削咋搾昨朔柵窄策索錯桜鮭"
		"笹匙冊刷察拶撮擦札殺薩雑皐鯖捌錆鮫皿"
		"晒三傘参山惨撒散桟燦珊産算纂蚕讃賛酸"
		"餐斬暫残"
	}, {
		"し",
		"仕仔伺使刺司史嗣四士始姉姿子屍市師志"
		"思指支孜斯施旨枝止死氏獅祉私糸紙紫肢"
		"脂至視詞詩試誌諮資賜雌飼歯事似侍児字"
		"寺慈持時次滋治爾璽痔磁示而耳自蒔辞汐"
		"鹿式識鴫竺軸宍雫七叱執失嫉室悉湿漆疾"
		"質実蔀篠偲柴芝屡蕊縞舎写射捨赦斜煮社"
		"紗者謝車遮蛇邪借勺尺杓灼爵酌釈錫若寂"
		"弱惹主取守手朱殊狩珠種腫趣酒首儒受呪"
		"寿授樹綬需囚収周宗就州修愁拾洲秀秋終"
		"繍習臭舟蒐衆襲讐蹴輯週酋酬集醜什住充"
		"十従戎柔汁渋獣縦重銃叔夙宿淑祝縮粛塾"
		"熟出術述俊峻春瞬竣舜駿准循旬楯殉淳準"
		"潤盾純巡遵醇順処初所暑曙渚庶緒署書薯"
		"藷諸助叙女序徐恕鋤除傷償勝匠升召哨商"
		"唱嘗奨妾娼宵将小少尚庄床廠彰承抄招掌"
		"捷昇昌昭晶松梢樟樵沼消渉湘焼焦照症省"
		"硝礁祥称章笑粧紹肖菖蒋蕉衝裳訟証詔詳"
		"象賞醤鉦鍾鐘障鞘上丈丞乗冗剰城場壌嬢"
		"常情擾条杖浄状畳穣蒸譲醸錠嘱埴飾拭植"
		"殖燭織職色触食蝕辱尻伸信侵唇娠寝審心"
		"慎振新晋森榛浸深申疹真神秦紳臣芯薪親"
		"診身辛進針震人仁刃塵壬尋甚尽腎訊迅陣"
		"靭"
	}, {
		"す",
		"笥諏須酢図厨逗吹垂帥推水炊睡粋翠衰遂"
		"酔錐錘随瑞髄崇嵩数枢趨雛据杉椙菅頗雀"
		"裾澄摺寸"
	}, {
		"せ",
		"世瀬畝是凄制勢姓征性成政整星晴棲栖正"
		"清牲生盛精聖声製西誠誓請逝醒青静斉税"
		"脆隻席惜戚斥昔析石積籍績脊責赤跡蹟碩"
		"切拙接摂折設窃節説雪絶舌蝉仙先千占宣"
		"専尖川戦扇撰栓栴泉浅洗染潜煎煽旋穿箭"
		"線繊羨腺舛船薦詮賎践選遷銭銑閃鮮前善"
		"漸然全禅繕膳糎"
	}, {
		"そ",
		"噌塑岨措曾曽楚狙疏疎礎祖租粗素組蘇訴"
		"阻遡鼠僧創双叢倉喪壮奏爽宋層匝惣想捜"
		"掃挿掻操早曹巣槍槽漕燥争痩相窓糟総綜"
		"聡草荘葬蒼藻装走送遭鎗霜騒像増憎臓蔵"
		"贈造促側則即息捉束測足速俗属賊族続卒"
		"袖其揃存孫尊損村遜"
	}, {
		"た",
		"他多太汰詑唾堕妥惰打柁舵楕陀駄騨体堆"
		"対耐岱帯待怠態戴替泰滞胎腿苔袋貸退逮"
		"隊黛鯛代台大第醍題鷹滝瀧卓啄宅托択拓"
		"沢濯琢託鐸濁諾茸凧蛸只叩但達辰奪脱巽"
		"竪辿棚谷狸鱈樽誰丹単嘆坦担探旦歎淡湛"
		"炭短端箪綻耽胆蛋誕鍛団壇弾断暖檀段男"
		"談"
	}, {
		"ち",
		"値知地弛恥智池痴稚置致蜘遅馳築畜竹筑"
		"蓄逐秩窒茶嫡着中仲宙忠抽昼柱注虫衷註"
		"酎鋳駐樗瀦猪苧著貯丁兆凋喋寵帖帳庁弔"
		"張彫徴懲挑暢朝潮牒町眺聴脹腸蝶調諜超"
		"跳銚長頂鳥勅捗直朕沈珍賃鎮陳"
	}, {
		"つ",
		"津墜椎槌追鎚痛通塚栂掴槻佃漬柘辻蔦綴"
		"鍔椿潰坪壷嬬紬爪吊釣鶴"
	}, {
		"て",
		"亭低停偵剃貞呈堤定帝底庭廷弟悌抵挺提"
		"梯汀碇禎程締艇訂諦蹄逓邸鄭釘鼎泥摘擢"
		"敵滴的笛適鏑溺哲徹撤轍迭鉄典填天展店"
		"添纏甜貼転顛点伝殿澱田電"
	}, {
		"と",
		"兎吐堵塗妬屠徒斗杜渡登菟賭途都鍍砥砺"
		"努度土奴怒倒党冬凍刀唐塔塘套宕島嶋悼"
		"投搭東桃梼棟盗淘湯涛灯燈当痘祷等答筒"
		"糖統到董蕩藤討謄豆踏逃透鐙陶頭騰闘働"
		"動同堂導憧撞洞瞳童胴萄道銅峠鴇匿得徳"
		"涜特督禿篤毒独読栃橡凸突椴届鳶苫寅酉"
		"瀞噸屯惇敦沌豚遁頓呑曇鈍"
	}, {
		"な",
		"奈那内乍凪薙謎灘捺鍋楢馴縄畷南楠軟難"
		"汝"
	}, {
		"に",
		"二尼弐迩匂賑肉虹廿日乳入如尿韮任妊忍"
		"認"
	}, {
		"ぬ",
		"濡"
	}, {
		"ね",
		"禰祢寧葱猫熱年念捻撚燃粘"
	}, {
		"の",
		"乃廼之埜嚢悩濃納能脳膿農覗蚤"
	}, {
		"は",
		"巴把播覇杷波派琶破婆罵芭馬俳廃拝排敗"
		"杯盃牌背肺輩配倍培媒梅楳煤狽買売賠陪"
		"這蝿秤矧萩伯剥博拍柏泊白箔粕舶薄迫曝"
		"漠爆縛莫駁麦函箱硲箸肇筈櫨幡肌畑畠八"
		"鉢溌発醗髪伐罰抜筏閥鳩噺塙蛤隼伴判半"
		"反叛帆搬斑板氾汎版犯班畔繁般藩販範釆"
		"煩頒飯挽晩番盤磐蕃蛮"
	}, {
		"ひ",
		"匪卑否妃庇彼悲扉批披斐比泌疲皮碑秘緋"
		"罷肥被誹費避非飛樋簸備尾微枇毘琵眉美"
		"鼻柊稗匹疋髭彦膝菱肘弼必畢筆逼桧姫媛"
		"紐百謬俵彪標氷漂瓢票表評豹廟描病秒苗"
		"錨鋲蒜蛭鰭品彬斌浜瀕貧賓頻敏瓶"
	}, {
		"ふ",
		"不付埠夫婦富冨布府怖扶敷斧普浮父符腐"
		"膚芙譜負賦赴阜附侮撫武舞葡蕪部封楓風"
		"葺蕗伏副復幅服福腹複覆淵弗払沸仏物鮒"
		"分吻噴墳憤扮焚奮粉糞紛雰文聞"
	}, {
		"へ",
		"丙併兵塀幣平弊柄並蔽閉陛米頁僻壁癖碧"
		"別瞥蔑箆偏変片篇編辺返遍便勉娩弁鞭"
	}, {
		"ほ",
		"保舗鋪圃捕歩甫補輔穂募墓慕戊暮母簿菩"
		"倣俸包呆報奉宝峰峯崩庖抱捧放方朋法泡"
		"烹砲縫胞芳萌蓬蜂褒訪豊邦鋒飽鳳鵬乏亡"
		"傍剖坊妨帽忘忙房暴望某棒冒紡肪膨謀貌"
		"貿鉾防吠頬北僕卜墨撲朴牧睦穆釦勃没殆"
		"堀幌奔本翻凡盆"
	}, {
		"ま",
		"摩磨魔麻埋妹昧枚毎哩槙幕膜枕鮪柾鱒桝"
		"亦俣又抹末沫迄侭繭麿万慢満漫蔓"
	}, {
		"み",
		"味未魅巳箕岬密蜜湊蓑稔脈妙粍民眠"
	}, {
		"む",
		"務夢無牟矛霧鵡椋婿娘"
	}, {
		"め",
		"冥名命明盟迷銘鳴姪牝滅免棉綿緬面麺"
	}, {
		"も",
		"摸模茂妄孟毛猛盲網耗蒙儲木黙目杢勿餅"
		"尤戻籾貰問悶紋門匁"
	}, {
		"や",
		"也冶夜爺耶野弥矢厄役約薬訳躍靖柳薮鑓"
	}, {
		"ゆ",
		"愉愈油癒諭輸唯佑優勇友宥幽悠憂揖有柚"
		"湧涌猶猷由祐裕誘遊邑郵雄融夕"
	}, {
		"よ",
		"予余与誉輿預傭幼妖容庸揚揺擁曜楊様洋"
		"溶熔用窯羊耀葉蓉要謡踊遥陽養慾抑欲沃"
		"浴翌翼淀"
	}, {
		"ら",
		"羅螺裸来莱頼雷洛絡落酪乱卵嵐欄濫藍蘭"
		"覧"
	}, {
		"り",
		"利吏履李梨理璃痢裏裡里離陸律率立葎掠"
		"略劉流溜琉留硫粒隆竜龍侶慮旅虜了亮僚"
		"両凌寮料梁涼猟療瞭稜糧良諒遼量陵領力"
		"緑倫厘林淋燐琳臨輪隣鱗麟"
	}, {
		"る",
		"瑠塁涙累類"
	}, {
		"れ",
		"令伶例冷励嶺怜玲礼苓鈴隷零霊麗齢暦歴"
		"列劣烈裂廉恋憐漣煉簾練聯蓮連錬"
	}, {
		"ろ",
		"呂魯櫓炉賂路露労婁廊弄朗楼榔浪漏牢狼"
		"篭老聾蝋郎六麓禄肋録論"
	}, {
		"わ",
		"倭和話歪賄脇惑枠鷲亙亘鰐詫藁蕨椀湾碗"
		"腕"
	}
};

/*
 * For the kanji screen, we render all characters to an oversized surface
 * in advance. This way the scrolling code can be relatively dumb.
 */
static void make_kanji_screen(void)
{
	// decode UTF-8 characters for each block
	uint32_t block_chars[ARRAY_SIZE(kanji)][23*18];
	int block_nr_chars[ARRAY_SIZE(kanji)];
	for (int i = 0; i < ARRAY_SIZE(kanji); i++) {
		const char *s = kanji[i].kanji;
		int j;
		for (j = 0; *s; j++) {
			block_chars[i][j] = decode_utf8_code_point(&s);
		}
		block_chars[i][j] = 0;
		block_nr_chars[i] = j;
	}

	// count the number of lines required
	int nr_lines = 0;
	for (int i = 0; i < ARRAY_SIZE(kanji); i++) {
		nr_lines += block_nr_chars[i] / 18;
		if (block_nr_chars[i] % 18)
			nr_lines++;
	}

	SDL_Surface *s = alloc_surface(CHARS_W, nr_lines * 24);
	button_list_list buttons = vector_initializer;

	int y = 0;
	SDL_Surface *parts = gfx_get_surface(1);
	for (int i = 0; i < ARRAY_SIZE(kanji); i++) {
		// kana label
		const char *kana_s = kanji[i].kana;
		uint32_t kana_ch = decode_utf8_code_point(&kana_s);
		_gfx_text_draw_glyph(s, 13, y+2, kana_ch);
		_gfx_indexed_copy_masked(420, 48, 40, 24, parts, 4, y, s, MASK_COLOR);

		// kanji
		int x = 48;
		button_list line_btn = vector_initializer;
		for (int j = 0; block_chars[i][j]; j++, x += 24) {
			if (j > 0 && j % 18 == 0) {
				vector_push(button_list, buttons, line_btn);
				line_btn = (button_list)vector_initializer;
				x = 48;
				y += 24;
			}
			// draw glyph
			_gfx_text_draw_glyph(s, x+2, y+2, block_chars[i][j]);
			// create button
			struct button *btn = vector_pushp(struct button, line_btn);
			char_button_init(btn, x, y, block_chars[i][j]);
		}
		if (vector_length(line_btn) != 0) {
			vector_push(button_list, buttons, line_btn);
			y += 24;
		}
	}

	kanji_s = s;
	kanji_btn = buttons;
}

// }}} Kanji

#define SCREEN 0
#define PARTS 1
#define BACKGROUND 2
#define ANIM 6

static void draw_button_normal(struct button *button)
{
	SDL_Rect *src = &button->parts_normal;
	SDL_Rect *dst = &button->pos;
	gfx_copy_masked(src->x, src->y, src->w, src->h, PARTS,
			dst->x, dst->y, SCREEN, MASK_COLOR);
}

static void draw_button_pressed(struct button *button)
{
	SDL_Rect *src = &button->parts_pressed;
	SDL_Rect *dst = &button->pos;
	gfx_copy_masked(src->x, src->y, src->w, src->h, PARTS,
			dst->x, dst->y, SCREEN, MASK_COLOR);
}

static void draw_buttons(struct button *buttons, unsigned nr_buttons)
{
	struct button *b = buttons;
	for (int i = 0; i < nr_buttons; i++, b++) {
		draw_button_normal(b);
	}
}

static void draw_cursor(struct text_input *input)
{
	assert(input->cursor < 5);
	gfx_copy_masked(48, 80, 24, 23, PARTS, input->pos.x + input->cursor*24, input->pos.y,
			SCREEN, MASK_COLOR);
}

static void clear_cursor(struct text_input *input)
{
	unsigned x = input->pos.x + input->cursor * 24;
	unsigned y = 408;
	assert(input->cursor < 5);
	gfx_fill(x, y, 24, 3, SCREEN, 42);
	gfx_fill(x, y+20, 24, 3, SCREEN, 42);
	gfx_fill(x, y+3, 2, 17, SCREEN, 42);
	gfx_fill(x+22, y+3, 2, 17, SCREEN, 42);
}

static void draw_char(struct text_input *input, unsigned i, uint32_t ch)
{
	gfx_text_draw_glyph(input->pos.x + 2 + i*24, input->pos.y + 2, SCREEN, ch); 
}

static void clear_char(struct text_input *input, unsigned i)
{
	gfx_fill(input->pos.x + i * 24, input->pos.y, 24, 23, SCREEN, 42);
}

static void draw_kana_screen(void)
{
	// buttons
	draw_buttons(kana_btn.a, kana_btn.n);
	// text
	_gfx_indexed_copy_masked(0, 0, CHARS_W, CHARS_H, kana_s, CHARS_X, CHARS_Y,
			gfx_get_surface(SCREEN), MASK_COLOR);
	// clear kanji navigation buttons
	gfx_copy(72, 80, 480, 16, 1, 80, 328, 0);
}

static void draw_kanji_screen(bool draw_nav)
{
	// buttons
	for (unsigned i = 0; i < 12 && kanji_row + i < vector_length(kanji_btn); i++) {
		button_list *row_btn = &vector_A(kanji_btn, kanji_row + i);
		// adjust button Y
		struct button *btn;
		vector_foreach_p(btn, *row_btn) {
			btn->pos.y = CHARS_Y + i * 24;
		}
		draw_buttons(row_btn->a, row_btn->n);
	}
	// text
	_gfx_indexed_copy_masked(0, kanji_row*24, CHARS_W, CHARS_H, kanji_s, CHARS_X, CHARS_Y,
			gfx_get_surface(SCREEN), MASK_COLOR);
	// draw kanji navigation buttons
	if (draw_nav)
		gfx_copy(72, 96, 480, 16, 1, 80, 328, 0);
}

static void draw_kigou_screen(void)
{
	// buttons
	draw_buttons(kigou_btn.a, kigou_btn.n);
	// text
	_gfx_indexed_copy_masked(0, 0, CHARS_W, CHARS_H, kigou_s, CHARS_X, CHARS_Y,
			gfx_get_surface(SCREEN), MASK_COLOR);
	// clear kanji navigation buttons
	gfx_copy(72, 80, 480, 16, 1, 80, 328, 0);
}

static void clear_chars_screen(void)
{
	gfx_copy(CHARS_X, CHARS_Y, CHARS_W, CHARS_H, BACKGROUND, CHARS_X, CHARS_Y, SCREEN);
}

static enum moji_mode {
	MODE_KANA,
	MODE_KANJI,
	MODE_KIGOU
} moji_mode = MODE_KANA;

// location of peephole area on screen (top left corner)
#define PEEP_DST_X 560
#define PEEP_DST_Y 96
// location of peephole background on anim surface
#define PEEP_BG_SRC_X 152
#define PEEP_BG_SRC_Y 240
// location of peephole mask on anim surface
#define PEEP_MASK_SRC_X 0
#define PEEP_MASK_SRC_Y 256
#define PEEP_W 56
#define PEEP_H 32

// eye center coordinate on screen
#define PEEP_BG_CENTER_X 590
#define PEEP_BG_CENTER_Y 112

// eye center coordinate on peephole mask (anim surface)
#define PEEP_MASK_CENTER_X 30
#define PEEP_MASK_CENTER_Y 272

// location of pupil on anim surface (top left corner)
#define PUPIL_SRC_X 81
#define PUPIL_SRC_Y 241
#define PUPIL_W 14
#define PUPIL_H 14

static void update_eye(SDL_Point mouse)
{
	static SDL_Point prev_mouse = { 0, 0 };
	static SDL_Point prev_pupil = { 0, 0 };
	if (prev_mouse.x == mouse.x && prev_mouse.y == mouse.y)
		return;
	prev_mouse = mouse;

	// We use Bresenham's line drawing algorithm to trace a line from the
	// center of the eye to the first solid pixel on the peephole mask.
	// The pupil will be centered on the last transparent pixel.
	SDL_Surface *surf = gfx_get_surface(ANIM);

	int x = PEEP_BG_CENTER_X;
	int y = PEEP_BG_CENTER_Y;
	int dx = abs(mouse.x - x);
	int dy = -abs(mouse.y - y);
	int sx = x < mouse.x ? 1 : -1;
	int sy = y < mouse.y ? 1 : -1;
	int err = dx + dy;

	int pupil_x = x;
	int pupil_y = y;
	for (int i = 0; true; i++) {
		if (x == mouse.x && y == mouse.y)
			break;

		// get pixel location on anim surface
		int px = PEEP_MASK_CENTER_X + (PEEP_BG_CENTER_X - x);
		int py = PEEP_MASK_CENTER_Y - (PEEP_BG_CENTER_Y - y);
		if (unlikely(px < 0 || py < 0 || px >= surf->w || py >= surf->h))
			VM_ERROR("Failed to find pupil location");

		// break if solid pixel
		uint8_t c = *(((uint8_t*)surf->pixels) + surf->pitch * py + px);
		if (c != MASK_COLOR)
			break;

		pupil_x = x;
		pupil_y = y;

		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y += sy;
		}
	}

	if (prev_pupil.x == pupil_x && prev_pupil.y == pupil_y)
		return;

	// draw peephole background
	gfx_copy(PEEP_BG_SRC_X, PEEP_BG_SRC_Y, PEEP_W, PEEP_H, ANIM,
			PEEP_DST_X, PEEP_DST_Y, SCREEN);
	// draw pupil centered at pupil_{x,y}
	gfx_copy_masked(PUPIL_SRC_X, PUPIL_SRC_Y, PUPIL_W, PUPIL_H, ANIM,
			pupil_x - PUPIL_W/2, pupil_y - PUPIL_H/2, SCREEN, MASK_COLOR);
	// draw peephole mask
	gfx_copy_masked(PEEP_MASK_SRC_X, PEEP_MASK_SRC_Y, PEEP_W, PEEP_H, ANIM,
			PEEP_DST_X, PEEP_DST_Y, SCREEN, MASK_COLOR);

	prev_pupil.x = pupil_x;
	prev_pupil.y = pupil_y;
}

static void update_buttons(SDL_Point mouse)
{
	static struct button *pressed_button = NULL;
	if (pressed_button) {
		// wait for release
		if (input_down(INPUT_ACTIVATE))
			return;
		if (!pressed_button->hold)
			draw_button_normal(pressed_button);
		pressed_button = NULL;
	}

	if (!input_down(INPUT_ACTIVATE))
		return;

	for (unsigned i = 0; i < ARRAY_SIZE(buttons); i++) {
		if (SDL_PointInRect(&mouse, &buttons[i]->pos)) {
			pressed_button = buttons[i];
			goto handle_button_press;
		}
	}

	// check kana buttons
	if (moji_mode == MODE_KANA) {
		struct button *btn;
		vector_foreach_p(btn, kana_btn) {
			if (SDL_PointInRect(&mouse, &btn->pos)) {
				pressed_button = btn;
				goto handle_button_press;
			}
		}
	} else if (moji_mode == MODE_KANJI) {
		for (unsigned i = 0; i < 12 && kanji_row + i < vector_length(kanji_btn); i++) {
			button_list *row_btn = &vector_A(kanji_btn, kanji_row + i);
			struct button *btn;
			vector_foreach_p(btn, *row_btn) {
				if (SDL_PointInRect(&mouse, &btn->pos)) {
					pressed_button = btn;
					goto handle_button_press;
				}
			}
		}
		for (unsigned i = 0; i < ARRAY_SIZE(kanji_nav_btn); i++) {
			if (SDL_PointInRect(&mouse, &kanji_nav_btn[i].pos)) {
				pressed_button = &kanji_nav_btn[i];
				goto handle_button_press;
			}
		}
	} else if (moji_mode == MODE_KIGOU) {
		struct button *btn;
		vector_foreach_p(btn, kigou_btn) {
			if (SDL_PointInRect(&mouse, &btn->pos)) {
				pressed_button = btn;
				goto handle_button_press;
			}
		}
	}
	return;
handle_button_press:
	draw_button_pressed(pressed_button);
	pressed_button->pressed(pressed_button);
}

static void name_screen_handle_input(void)
{
	SDL_Point mouse;
	cursor_get_pos((unsigned*)&mouse.x, (unsigned*)&mouse.y);
	if (!anim_stream_running(0))
		update_eye(mouse);
	update_buttons(mouse);
}

void shuusaku_name_input_screen(uint8_t *myouji, uint8_t *namae)
{
	// namepart.gpx loaded to surface 1
	// name.gpx loaded to surface 2 & surface 0
	// namean.gpx loaded to surface 6

	uint32_t saved_bg, saved_fg;
	gfx_text_get_colors(&saved_bg, &saved_fg);
	gfx_text_set_colors(saved_bg, 58);

	gfx_text_set_size(20, 0);
	text_shadow = TEXT_SHADOW_NONE;

	// prepare kana input screen
	make_kana_screen();
	make_kanji_screen();
	make_kigou_screen();

	draw_kana_screen();
	draw_button_pressed(&moji_group.buttons[moji_group.down].btn);
	draw_button_pressed(&name_group.buttons[name_group.down].btn);
	draw_cursor(&myouji_input);

	gfx_text_set_colors(saved_bg, saved_fg);
	shuusaku_crossfade(memory.palette, false);
	anim_start(0);

	cursor_load(0, 1, NULL);
	while (!kettei) {
		vm_peek();
		enum moji_mode old_mode = moji_mode;
		name_screen_handle_input();
		if (moji_mode != old_mode) {
			clear_chars_screen();
			if (moji_mode == MODE_KANA) {
				draw_kana_screen();
			} else if (moji_mode == MODE_KANJI) {
				draw_kanji_screen(true);
			} else if (moji_mode == MODE_KIGOU) {
				draw_kigou_screen();
			}
		}
		vm_delay(16);
	}
	cursor_unload();

	SDL_FreeSurface(kana_s);
	vector_destroy(kana_btn);
	SDL_FreeSurface(kanji_s);
	button_list *p;
	vector_foreach_p(p, kanji_btn) {
		vector_destroy(*p);
	}
	vector_destroy(kanji_btn);
	SDL_FreeSurface(kigou_s);
	vector_destroy(kigou_btn);

	text_shadow = TEXT_SHADOW_B;
	gfx_text_set_size(16, 1);

	for (unsigned i = 0; i < myouji_input.nr_chars; i++) {
		uint16_t c = unicode_to_sjis(myouji_input.chars[i]);
		myouji[i*2+0] = c >> 8;
		myouji[i*2+1] = c & 0xff;
	}
	myouji[myouji_input.nr_chars * 2] = 0xff;
	for (unsigned i = 0; i < namae_input.nr_chars; i++) {
		uint16_t c = unicode_to_sjis(namae_input.chars[i]);
		namae[i*2+0] = c >> 8;
		namae[i*2+1] = c & 0xff;
	}
	namae[namae_input.nr_chars * 2] = 0xff;
}

static void radio_button_pressed(struct button *_btn)
{
	struct radio_button *btn = (struct radio_button*)_btn;
	struct radio_group *group = btn->group;
	struct radio_button *prev = &group->buttons[group->down];

	if (prev == btn)
		return;

	// update downed button index
	for (unsigned i = 0; i < 3; i++) {
		if (&group->buttons[i] == btn) {
			group->down = i;
			break;
		}
	}

	// restore previous downed button to normal state
	draw_button_normal(&prev->btn);
	// run callback
	btn->pressed(btn);
}

static void kana_pressed(struct radio_button *btn)
{
	moji_mode = MODE_KANA;
}

static void kanji_pressed(struct radio_button *btn)
{
	moji_mode = MODE_KANJI;
}

static void kigou_pressed(struct radio_button *btn)
{
	moji_mode = MODE_KIGOU;
}

static struct text_input *get_text_input(void)
{
	return name_group.down == 0 ? &myouji_input : &namae_input;
}

static void left_arrow_pressed(struct button *btn)
{
	struct text_input *input = get_text_input();
	if (input->cursor > 0) {
		clear_cursor(input);
		input->cursor--;
		draw_cursor(input);
	}
}

static void right_arrow_pressed(struct button *btn)
{
	struct text_input *input = get_text_input();
	if (input->cursor < input->nr_chars) {
		clear_cursor(input);
		input->cursor++;
		draw_cursor(input);
	}
}

static void modoru_pressed(struct button *btn)
{
	struct text_input *input = get_text_input();
	if (input->cursor == 0)
		return;
	clear_char(input, input->cursor - 1);
	unsigned limit = input->nr_chars + (input->cursor == input->nr_chars ? 1 : 0);
	for (unsigned i = input->cursor; i < limit; i++) {
		unsigned src_x = input->pos.x + i * 24;
		unsigned dst_x = src_x - 24;
		gfx_copy(src_x, input->pos.y, 24, 23, SCREEN, dst_x, input->pos.y, SCREEN);
		input->chars[i - 1] = input->chars[i];
	}
	input->cursor--;
	input->nr_chars--;
	clear_char(input, input->nr_chars + (input->cursor == input->nr_chars ? 1 : 0));
}

static void kettei_pressed(struct button *btn)
{
	kettei = true;
}

static void myouji_pressed(struct radio_button *btn)
{
	clear_cursor(&namae_input);
	draw_cursor(&myouji_input);
}

static void namae_pressed(struct radio_button *btn)
{
	clear_cursor(&myouji_input);
	draw_cursor(&namae_input);
}

static void char_button_pressed(struct button *button)
{
	struct text_input *input = get_text_input();
	clear_char(input, input->cursor);
	draw_char(input, input->cursor, button->data);
	input->chars[input->cursor] = button->data;
	if (input->cursor == input->nr_chars) {
		input->nr_chars++;
	}
	if (input->cursor < 4) {
		input->cursor++;
	}
	draw_cursor(input);
}

static void nav_jump_pressed(struct button *button)
{
	kanji_row = button->data;
	clear_chars_screen();
	draw_kanji_screen(false);
}

static void nav_up_pressed(struct button *button)
{
	if (kanji_row == 0)
		return;
	kanji_row--;
	clear_chars_screen();
	draw_kanji_screen(false);
}

static void nav_down_pressed(struct button *button)
{
	// XXX: don't go below `wa` label
	if (kanji_row + 2 >= vector_length(kanji_btn))
		return;
	kanji_row++;
	clear_chars_screen();
	draw_kanji_screen(false);
}
