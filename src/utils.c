#include "configator.h"
#include "dragonfail.h"

#include "inputs.h"
#include "config.h"
#include "utils.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>

#if defined(__DragonFly__) || defined(__FreeBSD__)
	#include <sys/consio.h>
#else // linux
	#include <linux/vt.h>
#endif

void desktop_crawl(
	struct desktop* target,
	char* sessions,
	enum display_server server)
{
	DIR* dir;
	struct dirent* dir_info;
	int ok;

	ok = access(sessions, F_OK);

	if (ok == -1)
	{
		dgn_throw(DGN_XSESSIONS_DIR);
		return;
	}

	dir = opendir(sessions);

	if (dir == NULL)
	{
		dgn_throw(DGN_XSESSIONS_OPEN);
		return;
	}

	char* name = NULL;
	char* exec = NULL;

	struct configator_param map_desktop[] =
	{
		{"Exec", &exec, config_handle_str},
		{"Name", &name, config_handle_str},
	};

	struct configator_param* map[] =
	{
		NULL,
		map_desktop,
	};

	struct configator_param sections[] =	
	{
		{"Desktop Entry", NULL, NULL},
	};

	uint16_t map_len[] = {0, 2};
	uint16_t sections_len = 1;

	struct configator desktop_config;
	desktop_config.map = map;
	desktop_config.map_len = map_len;
	desktop_config.sections = sections;
	desktop_config.sections_len = sections_len;

#if defined(NAME_MAX)
	char path[NAME_MAX];
#elif defined(_POSIX_PATH_MAX)
	char path[_POSIX_PATH_MAX];
#else
	char path[1024];
#endif

	dir_info = readdir(dir);

	while (dir_info != NULL)
	{
		if ((dir_info->d_name)[0] == '.')
		{
			dir_info = readdir(dir);
			continue;
		}

		snprintf(path, (sizeof (path)) - 1, "%s/", sessions);
		strncat(path, dir_info->d_name, (sizeof (path)) - 1);
		configator(&desktop_config, path);

		// if these are wayland sessions, add " (Wayland)" to their names,
		// as long as their names don't already contain that string
		if (server == DS_WAYLAND && config.wayland_specifier)
		{
			const char wayland_specifier[] = " (Wayland)";
			if (strstr(name, wayland_specifier) == NULL)
			{
				name = realloc(name, (strlen(name) + sizeof(wayland_specifier) + 1));
				// using strcat is safe because the string is constant
				strcat(name, wayland_specifier);
			}
		}

		if ((name != NULL) && (exec != NULL))
		{
			input_desktop_add(target, name, exec, server);
		}

		name = NULL;
		exec = NULL;
		dir_info = readdir(dir);
	}

	closedir(dir);
}

void desktop_load(struct desktop* target)
{
	// we don't care about desktop environments presence
	// because the fallback shell is always available
	// so we just dismiss any "throw" for now
	int err = 0;

	desktop_crawl(target, config.waylandsessions, DS_WAYLAND);

	if (dgn_catch())
	{
		++err;
		dgn_reset();
	}

	desktop_crawl(target, config.xsessions, DS_XORG);

	if (dgn_catch())
	{
		++err;
		dgn_reset();
	}
}

static char* hostname_backup = NULL;

void hostname(char** out)
{
	if (hostname_backup != NULL)
	{
		*out = hostname_backup;
		return;
	}

	int maxlen = sysconf(_SC_HOST_NAME_MAX);

	if (maxlen < 0)
	{
		maxlen = _POSIX_HOST_NAME_MAX;
	}

	hostname_backup = malloc(maxlen + 1);

	if (hostname_backup == NULL)
	{
		dgn_throw(DGN_ALLOC);
		return;
	}

	if (gethostname(hostname_backup, maxlen) < 0)
	{
		dgn_throw(DGN_HOSTNAME);
		return;
	}

	hostname_backup[maxlen] = '\0';
	*out = hostname_backup;
}

void free_hostname()
{
	free(hostname_backup);
}

void switch_tty(struct term_buf* buf)
{
	FILE* console = fopen(config.console_dev, "w");

	if (console == NULL)
	{
		buf->info_line = lang.err_console_dev;
		return;
	}

	int fd = fileno(console);

	ioctl(fd, VT_ACTIVATE, config.tty);
	ioctl(fd, VT_WAITACTIVE, config.tty);

	fclose(console);
}

void save(struct desktop* desktop, struct text* login)
{
	if (config.save)
	{
		FILE* fp = fopen(config.save_file, "wb+");

		if (fp != NULL)
		{
			fprintf(fp, "%s\n%d", login->text, desktop->cur);
			fclose(fp);
		}
	}
}

void load(struct desktop* desktop, struct text* login)
{
	if (!config.load)
	{
		return;
	}

	FILE* fp = fopen(config.save_file, "rb");

	if (fp == NULL)
	{
		return;
	}

	char* line = malloc(config.max_login_len + 1);

	if (line == NULL)
	{
		fclose(fp);
		return;
	}

	if (fgets(line, config.max_login_len + 1, fp))
	{
		int len = strlen(line);
		strncpy(login->text, line, login->len);

		if (len == 0)
		{
			login->end = login->text;
		}
		else
		{
			login->end = login->text + len - 1;
			login->text[len - 1] = '\0';
		}
	}
	else
	{
		fclose(fp);
		free(line);
		return;
	}

	if (fgets(line, config.max_login_len + 1, fp))
	{
		int saved_cur = abs(atoi(line));

		if (saved_cur < desktop->len)
		{
			desktop->cur = saved_cur;
		}
	}

	fclose(fp);
	free(line);
}

void get_time(char* buf) {
	time_t now;
	time(&now);

	struct tm* local = localtime(&now);

	char* day;
	switch(local->tm_wday) {
		case 0:
			day = strdup("Sun");
			break;
		case 1:
			day = strdup("Mon");
			break;
		case 2:
			day = strdup("Tue");
			break;
		case 3:
			day = strdup("Wed");
			break;
		case 4:
			day = strdup("Thu");
			break;
		case 5:
			day = strdup("Fri");
			break;
		case 6:
			day = strdup("Sat");
			break;
	}

	char* month;
	switch(local->tm_mon) {
		case 0:
			month = strdup("Jan");
			break;
		case 1:
			month = strdup("Feb");
			break;
		case 2:
			month = strdup("Mar");
			break;
		case 3:
			month = strdup("Apr");
			break;
		case 4:
			month = strdup("May");
			break;
		case 5:
			month = strdup("Jun");
			break;
		case 6:
			month = strdup("Jul");
			break;
		case 7:
			month = strdup("Aug");
			break;
		case 8:
			month = strdup("Sep");
			break;
		case 9:
			month = strdup("Oct");
			break;
		case 10:
			month = strdup("Nov");
			break;
		case 11:
			month = strdup("Dec");
			break;
	}

	snprintf(
		buf,
		25,
		"%s %d %s %02d %02d:%02d:%02d",
		day,
		local->tm_year + 1900,
		month,
		local->tm_mday,
		local->tm_hour,
		local->tm_min,
		local->tm_sec);
}
