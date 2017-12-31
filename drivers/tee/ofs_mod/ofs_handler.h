#ifndef	 OFS_HANDLER_H
#define  OFS_HANDLER_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <ofs/ofs_msg.h>



int ofs_handle_fs_msg(struct ofs_msg *);



#endif
