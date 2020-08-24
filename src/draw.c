#include "dragonfail.h"
#include "termbox.h"
#include "ctypes.h"

#include "inputs.h"
#include "utils.h"
#include "config.h"
#include "draw.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__DragonFly__) || defined(__FreeBSD__)
	#include <sys/kbio.h>
#else // linux
	#include <linux/kd.h>
#endif

#define DOOM_STEPS 13
#define DOOM_FRAMES 0
#define MATRIX_FRAMES 10

void draw_init(struct term_buf* buf)
{
	buf->width = tb_width();
	buf->height = tb_height();
	hostname(&buf->info_line);

	u16 len_login = strlen(lang.login);
	u16 len_password = strlen(lang.password);

	if (len_login > len_password)
	{
		buf->labels_max_len = len_login;
	}
	else
	{
		buf->labels_max_len = len_password;
	}

	buf->box_height = 7 + (2 * config.margin_box_v);
	buf->box_width =
		(2 * config.margin_box_h)
		+ (config.input_len + 1)
		+ buf->labels_max_len;

#if defined(__linux__)
	buf->box_chars.left_up = 0x250c;
	buf->box_chars.left_down = 0x2514;
	buf->box_chars.right_up = 0x2510;
	buf->box_chars.right_down = 0x2518;
	buf->box_chars.top = 0x2500;
	buf->box_chars.bot = 0x2500;
	buf->box_chars.left = 0x2502;
	buf->box_chars.right = 0x2502;
#else
	buf->box_chars.left_up = '+';
	buf->box_chars.left_down = '+';
	buf->box_chars.right_up = '+';
	buf->box_chars.right_down= '+';
	buf->box_chars.top = '-';
	buf->box_chars.bot = '-';
	buf->box_chars.left = '|';
	buf->box_chars.right = '|';
#endif
}

void draw_free(struct term_buf* buf)
{
	if (config.animate)
	{
		free(buf->tmp_buf);
	}
}

void draw_box(struct term_buf* buf)
{
	u16 box_x = (buf->width - buf->box_width) / 2;
	u16 box_y = (buf->height - buf->box_height) / 2;
	u16 box_x2 = (buf->width + buf->box_width) / 2;
	u16 box_y2 = (buf->height + buf->box_height) / 2;
	buf->box_x = box_x;
	buf->box_y = box_y;

	if (!config.hide_borders)
	{
		// corners
		tb_change_cell(
			box_x - 1,
			box_y - 1,
			buf->box_chars.left_up,
			config.fg,
			config.bg);
		tb_change_cell(
			box_x2,
			box_y - 1,
			buf->box_chars.right_up,
			config.fg,
			config.bg);
		tb_change_cell(
			box_x - 1,
			box_y2,
			buf->box_chars.left_down,
			config.fg,
			config.bg);
		tb_change_cell(
			box_x2,
			box_y2,
			buf->box_chars.right_down,
			config.fg,
			config.bg);

		// top and bottom
		struct tb_cell c1 = {buf->box_chars.top, config.fg, config.bg};
		struct tb_cell c2 = {buf->box_chars.bot, config.fg, config.bg};

		for (u8 i = 0; i < buf->box_width; ++i)
		{
			tb_put_cell(
				box_x + i,
				box_y - 1,
				&c1);
			tb_put_cell(
				box_x + i,
				box_y2,
				&c2);
		}

		// left and right
		c1.ch = buf->box_chars.left;
		c2.ch = buf->box_chars.right;

		for (u8 i = 0; i < buf->box_height; ++i)
		{
			tb_put_cell(
				box_x - 1,
				box_y + i,
				&c1);

			tb_put_cell(
				box_x2,
				box_y + i,
				&c2);
		}
	}

	if (config.blank_box)
	{
		struct tb_cell blank = {' ', config.fg, config.bg};

		for (u8 i = 0; i < buf->box_height; ++i)
		{
			for (u8 k = 0; k < buf->box_width; ++k)
			{
				tb_put_cell(
					box_x + k,
					box_y + i,
					&blank);
			}
		}
	}
}

struct tb_cell* strn_cell(char* s, u16 len, u8 fg, u8 bg) // throws
{
	struct tb_cell* cells = malloc((sizeof (struct tb_cell)) * len);
	char* s2 = s;
	u32 c;

	if (cells != NULL)
	{
		for (u16 i = 0; i < len; ++i)
		{
			if ((s2 - s) >= len)
			{
				break;
			}

			s2 += utf8_char_to_unicode(&c, s2);

			cells[i].ch = c;
			cells[i].bg = bg;
			cells[i].fg = fg;
		}
	}
	else
	{
		dgn_throw(DGN_ALLOC);
	}

	return cells;
}

struct tb_cell* str_cell(char* s, u8 fg, u8 bg) // throws
{
	return strn_cell(s, strlen(s), fg, bg);
}

void draw_labels(struct term_buf* buf) // throws
{
	// login text
	struct tb_cell* login = str_cell(lang.login, config.fg, config.bg);

	if (dgn_catch())
	{
		dgn_reset();
	}
	else
	{
		tb_blit(
			buf->box_x + config.margin_box_h,
			buf->box_y + config.margin_box_v + 4,
			strlen(lang.login),
			1,
			login);
		free(login);
	}

	// password text
	struct tb_cell* password = str_cell(lang.password, config.fg, config.bg);

	if (dgn_catch())
	{
		dgn_reset();
	}
	else
	{
		tb_blit(
			buf->box_x + config.margin_box_h,
			buf->box_y + config.margin_box_v + 6,
			strlen(lang.password),
			1,
			password);
		free(password);
	}

	if (buf->info_line != NULL)
	{
		u16 len = strlen(buf->info_line);
		struct tb_cell* info_cell = str_cell(buf->info_line, config.fg, config.bg);

		if (dgn_catch())
		{
			dgn_reset();
		}
		else
		{
			tb_blit(
				buf->box_x + ((buf->box_width - len) / 2),
				buf->box_y + config.margin_box_v,
				len,
				1,
				info_cell);
			free(info_cell);
		}
	}
}

void draw_info_bar(struct term_buf* buf)
{
	int width = tb_width();

	struct tb_cell* f1 = str_cell(lang.f1, config.fg, config.bg_bar_diff ? config.bg_bar : config.bg);

	if (dgn_catch())
	{
		dgn_reset();
	}
	else
	{
		tb_blit(0, 0, strlen(lang.f1), 1, f1);
		free(f1);
	}

	struct tb_cell* f2 = str_cell(lang.f2, config.fg, config.bg_bar_diff ? config.bg_bar : config.bg);

	if (dgn_catch())
	{
		dgn_reset();
	}
	else
	{
		tb_blit(strlen(lang.f1) + 1, 0, strlen(lang.f2), 1, f2);
		free(f2);
	}

	struct tb_cell* empty = str_cell(" ", config.fg, config.bg_bar_diff ? config.bg_bar : config.bg);

	if (dgn_catch())
	{
		dgn_reset();
	}
	else
	{
		int i = strlen(lang.f1) + 1 + strlen(lang.f2);

		tb_blit(strlen(lang.f1), 0, 1, 1, empty);
		for(; i < width; ++i) {
			tb_blit(i, 0, 1, 1, empty);
		}

		free(empty);
	}

	// TODO: Prevent null character from attaching to the end
	char* time = (char*) malloc(25);
	get_time(time);
	time = (char*) realloc(time, 24);

	struct tb_cell* date_time = str_cell(time, config.fg, config.bg_bar_diff ? config.bg_bar : config.bg);

	if (dgn_catch())
	{
		dgn_reset();
	}
	else
	{
		tb_blit(
			buf->box_x + ((buf->box_width - 24) / 2),
			0,
			24,
			1,
			date_time);

		free(date_time);
		free(time);
	}
}

void draw_lock_state(struct term_buf* buf)
{
	// get values
	int fd = open(config.console_dev, O_RDONLY);

	if (fd < 0)
	{
		buf->info_line = lang.err_console_dev;
		return;
	}

	bool numlock_on;
	bool capslock_on;

#if defined(__DragonFly__) || defined(__FreeBSD__)
	int led;
	ioctl(fd, KDGETLED, &led);
	numlock_on = led & LED_NUM;
	capslock_on = led & LED_CAP;
#else // linux
	char led;
	ioctl(fd, KDGKBLED, &led);
	numlock_on = led & K_NUMLOCK;
	capslock_on = led & K_CAPSLOCK;
#endif

	close(fd);

	// print text
	u16 pos_x = buf->width - strlen(lang.numlock);

	if (numlock_on)
	{
		struct tb_cell* numlock = str_cell(lang.numlock, config.fg, config.bg_bar_diff ? config.bg_bar : config.bg);

		if (dgn_catch())
		{
			dgn_reset();
		}
		else
		{
			tb_blit(pos_x, 0, strlen(lang.numlock), 1, numlock);
			free(numlock);
		}
	}

	pos_x -= strlen(lang.capslock) + 1;

	if (capslock_on)
	{
		struct tb_cell* capslock = str_cell(lang.capslock, config.fg, config.bg_bar_diff ? config.bg_bar : config.bg);

		if (dgn_catch())
		{
			dgn_reset();
		}
		else
		{
			tb_blit(pos_x, 0, strlen(lang.capslock), 1, capslock);
			free(capslock);
		}
	}
}

void draw_desktop(struct desktop* target)
{
	u16 len = strlen(target->list[target->cur]);

	if (len > (target->visible_len - 3))
	{
		len = target->visible_len - 3;
	}

	tb_change_cell(
		target->x,
		target->y,
		'<',
		config.fg,
		config.bg);

	tb_change_cell(
		target->x + target->visible_len - 1,
		target->y,
		'>',
		config.fg,
		config.bg);

	for (u16 i = 0; i < len; ++ i)
	{
		tb_change_cell(
			target->x + i + 2,
			target->y,
			target->list[target->cur][i],
			config.fg,
			config.bg);
	}
}

void draw_input(struct text* input)
{
	u16 len = strlen(input->text);
	u16 visible_len = input->visible_len;

	if (len > visible_len)
	{
		len = visible_len;
	}

	struct tb_cell* cells = strn_cell(input->visible_start, len, config.fg, config.bg);

	if (dgn_catch())
	{
		dgn_reset();
	}
	else
	{
		tb_blit(input->x, input->y, len, 1, cells);
		free(cells);

		struct tb_cell c1 = {' ', config.fg, config.bg};

		for (u16 i = input->end - input->visible_start; i < visible_len; ++i)
		{
			tb_put_cell(
				input->x + i,
				input->y,
				&c1);
		}
	}
}

void draw_input_mask(struct text* input)
{
	u16 len = strlen(input->text);
	u16 visible_len = input->visible_len;

	if (len > visible_len)
	{
		len = visible_len;
	}

	struct tb_cell c1 = {config.asterisk, config.fg, config.bg};
	struct tb_cell c2 = {' ', config.fg, config.bg};

	for (u16 i = 0; i < visible_len; ++i)
	{
		if (input->visible_start + i < input->end)
		{
			tb_put_cell(
				input->x + i,
				input->y,
				&c1);
		}
		else
		{
			tb_put_cell(
				input->x + i,
				input->y,
				&c2);
		}
	}
}

void position_input(
	struct term_buf* buf,
	struct desktop* desktop,
	struct text* login,
	struct text* password)
{
	u16 x = buf->box_x + config.margin_box_h + buf->labels_max_len + 1;
	i32 len = buf->box_x + buf->box_width - config.margin_box_h - x;

	if (len < 0)
	{
		return;
	}

	desktop->x = x;
	desktop->y = buf->box_y + config.margin_box_v + 2;
	desktop->visible_len = len;

	login->x = x;
	login->y = buf->box_y + config.margin_box_v + 4;
	login->visible_len = len;

	password->x = x;
	password->y = buf->box_y + config.margin_box_v + 6;
	password->visible_len = len;
}

static void doom_init(struct term_buf* buf)
{
	buf->init_width = buf->width;
	buf->init_height = buf->height;

	u16 tmp_len = buf->width * buf->height;
	buf->tmp_buf = malloc(tmp_len);
	tmp_len -= buf->width;

	if (buf->tmp_buf == NULL)
	{
		dgn_throw(DGN_ALLOC);
	}

	memset(buf->tmp_buf, 0, tmp_len);
	memset(buf->tmp_buf + tmp_len, DOOM_STEPS - 1, buf->width);
}

static void matrix_init(struct term_buf* buf)
{
	buf->init_width = buf->width;
	buf->init_height = buf->height;

	u16 tmp_len = buf->width * buf->height;
	buf->tmp_buf = malloc(tmp_len);

	if (buf->tmp_buf == NULL)
	{
		dgn_throw(DGN_ALLOC);
	}

	memset(buf->tmp_buf, 0, tmp_len);
}

void animate_init(struct term_buf* buf)
{
	if (config.animate)
	{
		switch(config.animation)
		{
			case 1:
			{
				matrix_init(buf);
				break;
			}
			default:
			{
				doom_init(buf);
				break;
			}
		}
	}
}

static void doom(struct term_buf* term_buf)
{
	static struct tb_cell fire[DOOM_STEPS] =
	{
		{' ', 7, 0}, // black
		{0x2591, 1, 0}, // red
		{0x2592, 1, 0}, // red
		{0x2593, 1, 0}, // red
		{0x2588, 1, 0}, // red
		{0x2591, 3, 1}, // yellow
		{0x2592, 3, 1}, // yellow
		{0x2593, 3, 1}, // yellow
		{0x2588, 3, 1}, // yellow
		{0x2591, 7, 3}, // white
		{0x2592, 7, 3}, // white
		{0x2593, 7, 3}, // white
		{0x2588, 7, 3}, // white
	};

	u16 src;
	u16 random;
	u16 dst;

	u16 w = term_buf->init_width;
	u8* tmp = term_buf->tmp_buf;

	if ((term_buf->width != term_buf->init_width) || (term_buf->height != term_buf->init_height))
	{
		return;
	}

	struct tb_cell* buf = tb_cell_buffer();

	for (u16 x = 0; x < w; ++x)
	{
		for (u16 y = 1; y < term_buf->init_height; ++y)
		{
			src = y * w + x;
			random = ((rand() % 7) & 3);
			dst = src - random + 1;

			if (w > dst)
			{
				dst = 0;
			}
			else
			{
				dst -= w;
			}

			tmp[dst] = tmp[src] - (random & 1);

			if (tmp[dst] > 12)
			{
				tmp[dst] = 0;
			}

			buf[dst] = fire[tmp[dst]];
			buf[src] = fire[tmp[src]];
		}
	}
}

static void matrix(struct term_buf* term_buf)
{
	u16 src;
	u16 random;
	u16 dst;

	u16 w = term_buf->init_width;
	u8* tmp = term_buf->tmp_buf;

	if ((term_buf->width != term_buf->init_width) || (term_buf->height != term_buf->init_height))
	{
		return;
	}

	struct tb_cell* buf = tb_cell_buffer();

	for (u16 x = 0; x < w; ++x)
	{
		for (u16 y = term_buf->init_height - 1; y > 0; --y)
		{
			dst = y * w + x;
			src = dst - w;

			if (tmp[src])
			{
				if (tmp[dst])
				{
					buf[dst].ch = tmp[src];
					buf[dst].fg = 2;
					buf[dst].bg = 0;
				}
				else
				{
					buf[dst].ch = tmp[src];
					buf[dst].fg = 7;
					buf[dst].bg = 0;
				}

				tmp[dst] = tmp[src];
			}
			else
			{
				if (tmp[dst])
				{
					tmp[dst] = 0;
				}

				buf[dst].ch = 0;
				buf[dst].fg = 7;
				buf[dst].bg = 0;
			}
		}

		random = ((rand() % 32) & 30);

		if (random)
		{
			if (tmp[x + w])
			{
				random = (rand() % 94) + 33;
				tmp[x] = random;

				buf[x].ch = random;
				buf[x].fg = 2;
				buf[x].bg = 0;
			}
			else
			{
				tmp[x] = 0;

				buf[x].ch = 0;
				buf[x].fg = 7;
				buf[x].bg = 0;
			}
		}
		else
		{
			if (tmp[src + w])
			{
				tmp[src] = 0;

				buf[src].ch = 0;
				buf[src].fg = 7;
				buf[src].bg = 0;
			}
			else
			{
				random = (rand() % 94) + 33;
				tmp[src] = random;

				buf[src].ch = random;
				buf[src].fg = 7;
				buf[src].bg = 0;
			}
		}
	}
}

static void matrix_repeat(struct term_buf* term_buf)
{
	u16 src;

	u16 w = term_buf->init_width;
	u8* tmp = term_buf->tmp_buf;

	if ((term_buf->width != term_buf->init_width) || (term_buf->height != term_buf->init_height))
	{
		return;
	}

	struct tb_cell* buf = tb_cell_buffer();

	for (u16 x = 0; x < term_buf->width; ++x)
	{
		for (u16 y = 0; y < term_buf->height - 1; ++y)
		{
			src = y * w + x;

			if (tmp[src])
			{
				buf[src].ch = tmp[src];
				buf[src].fg = tmp[src + w] ? 2 : 7;
				buf[src].bg = 0;
			}
			else
			{
				buf[src].ch = ' ';
				buf[src].fg = 7;
				buf[src].bg = 0;
			}
		}
	}
}

void animate(struct term_buf* buf)
{
	buf->width = tb_width();
	buf->height = tb_height();

	if (config.animate)
	{
		switch(config.animation)
		{
			case 1:
			{
				static u8 frames = MATRIX_FRAMES;

				if (--frames)
				{
					matrix_repeat(buf);
				}
				else
				{
					frames = MATRIX_FRAMES;
					matrix(buf);
				}

				break;
			}
			default:
			{
				doom(buf);
				break;
			}
		}
	}
}

bool cascade(struct term_buf* term_buf, u8* fails)
{
	u16 width = term_buf->width;
	u16 height = term_buf->height;

	struct tb_cell* buf = tb_cell_buffer();
	bool changes = false;
	char c_under;
	char c;

	for (int i = height - 2; i >= 0; --i)
	{
		for (int k = 0; k < width; ++k)
		{
			c = buf[i * width + k].ch;

			if (isspace(c))
			{
				continue;
			}

			c_under = buf[(i + 1) * width + k].ch;
			
			if (!isspace(c_under))
			{
				continue;
			}

			if (!changes)
			{
				changes = true;
			}

			if ((rand() % 10) > 7)
			{
				continue;
			}

			buf[(i + 1) * width + k] = buf[i * width + k];
			buf[i * width + k].ch = ' ';
		}
	}

	// stop force-updating 
	if (!changes)
	{
		sleep(7);
		*fails = 0;

		return false;
	}

	// force-update
	return true;
}

bool checkUpdate() {
	return false;

	static u8 frames = 0;

	if (!frames)
	{
		switch (config.animation)
		{
			case 1:
			{
				frames = MATRIX_FRAMES;
				break;
			}
			default:
			{
				frames = DOOM_FRAMES;
				break;
			}
		}

		return true;
	}

	--frames;
	return false;
}
