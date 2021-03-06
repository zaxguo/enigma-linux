#ifndef	 OFS_HANDLER_H
#define  OFS_HANDLER_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <ofs/ofs_msg.h>
#include <linux/fdtable.h>

extern struct files_struct ofs_files;
extern struct ofs_msg *saved_msg;

static inline struct file *ofs_fget(int fd) {
	return fcheck_files(&ofs_files, fd);
}

int ofs_open_handler(void *);
int ofs_read_handler(void *);
int ofs_write_handler(void *);
int ofs_fsync_handler(void *);
int ofs_stat_handler(void *);
int ofs_fstat_handler(void *);
int ofs_mmap_handler(void *);
int ofs_munmap_handler(void *);
int ofs_handle_fs_msg(struct ofs_msg *);



#endif
