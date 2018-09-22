#include <linux/types.h>
#include <linux/random.h>
#include "ofs_obfuscation.h"
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <asm/page.h>


#define LOWER	97
#define UPPER	122
#define CURR_OBFUS_LV	0
#define MAX_OBFUS_LV	25
#define MAX_OBFUS_OPS	6
#define MIN(a,b)	(((a) < (b)) ? (a) : (b))


static struct file *decoy_files[MAX_OBFUS_LV] = {0};
static int decoy_file_cnt = 0;

/* create a decoy file at idx in the decoy file list */
static void ofs_obfus_open(int idx) {
	int i, file_len;
	char dir[] = "/mnt/ext2/";
	int max_filename = MAX_FILENAME - sizeof(dir) - 1;
	char *filename  = kzalloc(max_filename, GFP_KERNEL);
	char *decoyfile = kzalloc(MAX_FILENAME, GFP_KERNEL);
	strncpy(decoyfile, dir, sizeof(dir));
	file_len = (get_random_int() % max_filename) + 1;
	get_random_bytes(filename, file_len);
	/* wrap it to ascii 0-z */
	for (i = 0; i < file_len; i++) {
		filename[i] = filename[i] % (UPPER - LOWER + 1) + LOWER;
	}
	strncat(decoyfile, filename, max_filename);
	decoy_files[idx] = filp_open(decoyfile, O_RDWR | O_CREAT, 0600);
	ofs_printk("decoy: opening file name = %s\n", decoyfile);
	kfree(filename);
	kfree(decoyfile);
}

static struct file *get_rand_decoy_file(void) {
	struct file *f;
	int rand = get_random_int() % MIN(decoy_file_cnt, MAX_OBFUS_LV);
	f = decoy_files[rand];
	if (!f) {
		ofs_printk("idx %d no file!\n", rand);
		WARN_ON(1);
		return NULL;
	}
	return f;
}


static int ofs_obfus_rw(int rw) {
	int size, ret;
	struct file *f;
	char *buf = kmalloc(1024, GFP_KERNEL);
	ret = 0;
	f = get_rand_decoy_file();
	if (!f) {
		ofs_printk("%s:err -- no files...\n", __func__);
		return 0;
	}
	if (rw == 0) {
		size = get_random_int() % 1024;
		ret = kernel_read(f, 0, buf, size);
	} else {
		size = get_random_int() % 1024;
		get_random_bytes(buf, size);
		ret = kernel_write(f, buf, size, 0);
	}
	return ret;
}


static void ofs_obfus_llseek(void) {
	int whence, off;
	struct file *f = get_rand_decoy_file();
	whence = get_random_int() % 3;
	off = get_random_int() % 100;
	vfs_llseek(f, off, whence);
}

static void ofs_obfus_stat(void) {
	int error;
	struct kstat stat;
	struct file *f;
	f = get_rand_decoy_file();
	error = vfs_getattr(&f->f_path, &stat);
}

static void ofs_obfus_truncate(void) {
	struct file *f;
	int len = get_random_int() % 1024;
	f = get_rand_decoy_file();
	vfs_truncate(&f->f_path, len);
}

static void ofs_obfus_fsync(void) {
	int datasync = get_random_int() % 2;
	struct file *f = get_rand_decoy_file();
	vfs_fsync(f, datasync);
}

void ofs_obfuscate(int req) {
	int i, j;
	int obfus_lv = CURR_OBFUS_LV;
	/* prepare for decoy files operations */
	if (req == OFS_OPEN) {
		for (j = 0; j < obfus_lv; j++) {
			ofs_obfus_open(decoy_file_cnt++);
		}
	}
	for (i = 0; i < obfus_lv; i++) {
		int op = get_random_int() % MAX_OBFUS_OPS;
		ofs_printk("%s:decoy op = %d\n", __func__, op);
		switch (op) {
			case	0:
				ofs_obfus_rw(0);
				break;
			case	1:
				ofs_obfus_rw(1);
				break;
			case	2:
				ofs_obfus_llseek();
				break;
			case	3:
				ofs_obfus_truncate();
				break;
			case	4:
				ofs_obfus_stat();
				break;
			case	5:
				ofs_obfus_fsync();
				break;
			default:
				break;
		}
	}
	if (req == OFS_FSYNC) {
		for (i = 0; i < MIN(obfus_lv, decoy_file_cnt); i++) {
			struct file *f = decoy_files[i];
			vfs_fsync(f, 1);
		}
	}
	ofs_printk("%s:finished!\n", __func__);
}
