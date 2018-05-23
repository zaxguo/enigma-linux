#ifndef	 OFS_HANDLER_H
#define  OFS_HANDLER_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <ofs/ofs_msg.h>
#include <linux/fdtable.h>

extern struct files_struct ofs_files;

int ofs_read_handler(void *);
int ofs_open_handler(void *);
int ofs_handle_fs_msg(struct ofs_msg *);



#endif
