/* A2 Racer II for PS5 - the game's files from the player's own disc image (decision log D4).
 *
 * The player puts the Redump image (A2 Racer II (Netherlands) (v1.01).cue/.bin, one MODE2/2352 track) in the title
 * folder's dump/. On first start the data folder of its Joliet tree (the original capitalisation the game asks for:
 * spel.dat, Game.ini) is written to the title folder's data/; the image stays where it is. tools/extract-disc.py
 * does the same on a PC. */
#include "a2_ps5.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define SECTOR 2352
#define USER 2048

/* The real opendir: ps5-posix.c wraps opendir to list from dirs.txt, which knows nothing of dump/. */
DIR *__real_opendir(const char *path);
struct dirent *__real_readdir(DIR *dir);
int __real_closedir(DIR *dir);

typedef struct {
	FILE *f;
	long sectors;
	uint8_t raw[SECTOR];
} image;

/* The 2048 user bytes of a sector: offset 24 for mode 2 form 1, 16 for mode 1. */
static const uint8_t *user_data(image *im, uint32_t lba)
{
	if ((long)lba >= im->sectors || fseek(im->f, (long)lba * SECTOR, SEEK_SET) != 0 ||
	    fread(im->raw, 1, SECTOR, im->f) != SECTOR)
		return NULL;
	return im->raw + (im->raw[15] == 2 ? 24 : 16);
}

static int read_extent(image *im, uint32_t lba, uint32_t length, uint8_t *out)
{
	for (uint32_t done = 0; done < length; lba++) {
		const uint8_t *d = user_data(im, lba);
		if (!d)
			return -1;
		uint32_t n = length - done < USER ? length - done : USER;
		memcpy(out + done, d, n);
		done += n;
	}
	return 0;
}

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* A Joliet name (UCS-2 big-endian) as ASCII, without ";1"; non-ASCII characters become '_'. */
static void joliet_name(const uint8_t *p, int len, char *out, size_t cap)
{
	size_t n = 0;
	for (int i = 0; i + 1 < len && n + 1 < cap; i += 2) {
		unsigned c = (unsigned)p[i] << 8 | p[i + 1];
		if (c == ';')
			break;
		out[n++] = c >= 32 && c < 127 && c != '/' ? (char)c : '_';
	}
	out[n] = 0;
}

typedef int (*entry_fn)(image *im, const char *name, int is_dir, uint32_t lba, uint32_t size, void *ctx);

/* Calls fn for each entry of the directory at lba; stops when fn returns non-zero (and returns that). */
static int each_entry(image *im, uint32_t lba, uint32_t size, entry_fn fn, void *ctx)
{
	uint8_t *dir = malloc(size);
	if (!dir || read_extent(im, lba, size, dir) != 0) {
		free(dir);
		return -1;
	}
	int r = 0;
	for (uint32_t at = 0; at < size && r == 0;) {
		uint8_t n = dir[at];
		if (n == 0) { /* records do not cross a sector: on to the next one */
			at = (at / USER + 1) * USER;
			continue;
		}
		const uint8_t *e = dir + at;
		at += n;
		int name_len = e[32];
		if (name_len == 1 && (e[33] == 0 || e[33] == 1))
			continue;
		char name[128];
		joliet_name(e + 33, name_len, name, sizeof name);
		r = fn(im, name, (e[25] & 2) != 0, le32(e + 2), le32(e + 10), ctx);
	}
	free(dir);
	return r;
}

typedef struct {
	uint32_t lba, size;
	const char *want;
} found;

static int find_dir(image *im, const char *name, int is_dir, uint32_t lba, uint32_t size, void *ctx)
{
	(void)im;
	found *f = ctx;
	if (is_dir && strcasecmp(name, f->want) == 0) {
		f->lba = lba;
		f->size = size;
		return 1;
	}
	return 0;
}

static int find_file(image *im, const char *name, int is_dir, uint32_t lba, uint32_t size, void *ctx)
{
	(void)im;
	(void)lba;
	(void)size;
	found *f = ctx;
	return !is_dir && strcmp(name, f->want) == 0;
}

typedef struct {
	const char *out_dir;
	a2ps5_progress progress;
	uint64_t done, total;
	int files;
} copy_job;

static int sum_sizes(image *im, const char *name, int is_dir, uint32_t lba, uint32_t size, void *ctx)
{
	(void)im;
	(void)name;
	(void)lba;
	if (!is_dir)
		((copy_job *)ctx)->total += size;
	return 0;
}

static int copy_file(image *im, const char *name, int is_dir, uint32_t lba, uint32_t size, void *ctx)
{
	copy_job *job = ctx;
	if (is_dir)
		return 0; /* the game's data folder is flat */
	char path[512], part[520];
	snprintf(path, sizeof path, "%s/%s", job->out_dir, name);
	snprintf(part, sizeof part, "%s.part", path);
	FILE *w = fopen(part, "wb");
	if (!w) {
		fprintf(stderr, "a2: disc: cannot write %s\n", part);
		return -1;
	}
	for (uint32_t done = 0; done < size; lba++) {
		const uint8_t *d = user_data(im, lba);
		uint32_t n = size - done < USER ? size - done : USER;
		if (!d || fwrite(d, 1, n, w) != n) {
			fclose(w);
			remove(part);
			fprintf(stderr, "a2: disc: %s could not be copied\n", name);
			return -1;
		}
		done += n;
		job->done += n;
		if ((lba & 255) == 0 && job->progress)
			job->progress(job->total ? (int)(job->done * 100 / job->total) : 0);
	}
	if (fclose(w) != 0 || rename(part, path) != 0) {
		remove(part);
		return -1;
	}
	job->files++;
	return 0;
}

/* Opens path as an image when it holds a Joliet tree with data/spel.dat; returns that data folder's extent. */
static int open_image(const char *path, image *im, found *data)
{
	memset(im, 0, sizeof *im);
	im->f = fopen(path, "rb");
	if (!im->f)
		return -1;
	fseek(im->f, 0, SEEK_END);
	im->sectors = ftell(im->f) / SECTOR;
	const uint8_t *pvd = user_data(im, 16);
	if (!pvd || memcmp(pvd + 1, "CD001", 5) != 0)
		goto fail;
	for (uint32_t lba = 17; lba < 32; lba++) { /* the Joliet descriptor: type 2 with a UCS-2 escape */
		const uint8_t *vd = user_data(im, lba);
		if (!vd || memcmp(vd + 1, "CD001", 5) != 0 || vd[0] == 255)
			break;
		if (vd[0] == 2 && vd[88] == '%' && vd[89] == '/' && (vd[90] == '@' || vd[90] == 'C' || vd[90] == 'E')) {
			uint32_t root_lba = le32(vd + 156 + 2), root_size = le32(vd + 156 + 10);
			data->want = "data";
			if (each_entry(im, root_lba, root_size, find_dir, data) != 1)
				goto fail;
			found spel = {0, 0, "spel.dat"};
			if (each_entry(im, data->lba, data->size, find_file, &spel) != 1)
				goto fail;
			return 0;
		}
	}
fail:
	fclose(im->f);
	im->f = NULL;
	return -1;
}

static int ends_with_bin(const char *name)
{
	size_t n = strlen(name);
	return n > 4 && strcasecmp(name + n - 4, ".bin") == 0;
}

int a2ps5_import_disc(const char *dump_dir, const char *out_dir, a2ps5_progress progress, char *status, size_t cap)
{
	char path[512];
	image im;
	found data = {0, 0, NULL};
	int opened = 0;
	/* Any .bin in dump/: the folder listing, when the title may list it; else Redump's file name. */
	DIR *d = __real_opendir(dump_dir);
	if (d) {
		struct dirent *e;
		while (!opened && (e = __real_readdir(d)) != NULL) {
			if (!ends_with_bin(e->d_name))
				continue;
			snprintf(path, sizeof path, "%s/%s", dump_dir, e->d_name);
			opened = open_image(path, &im, &data) == 0;
			fprintf(stderr, "a2: disc: %s: %s\n", path, opened ? "A2 Racer II" : "not this game's disc");
		}
		__real_closedir(d);
	} else {
		fprintf(stderr, "a2: disc: %s cannot be listed, trying the Redump name\n", dump_dir);
	}
	if (!opened) {
		snprintf(path, sizeof path, "%s/A2 Racer II (Netherlands) (v1.01).bin", dump_dir);
		opened = open_image(path, &im, &data) == 0;
	}
	if (!opened) {
		snprintf(status, cap, "No A2 Racer II disc image found in the dump folder");
		return -1;
	}
	mkdir(out_dir, 0777);
	copy_job job = {out_dir, progress, 0, 0, 0};
	each_entry(&im, data.lba, data.size, sum_sizes, &job);
	fprintf(stderr, "a2: disc: copying %llu bytes from %s\n", (unsigned long long)job.total, path);
	int r = each_entry(&im, data.lba, data.size, copy_file, &job);
	fclose(im.f);
	if (r != 0) {
		snprintf(status, cap, "Copying from the disc image failed (see a2racer2.log)");
		return -1;
	}
	if (progress)
		progress(100);
	snprintf(status, cap, "%d files copied from the disc image", job.files);
	fprintf(stderr, "a2: disc: %s\n", status);
	return 0;
}
