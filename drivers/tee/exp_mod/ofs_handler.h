#ifndef	 OFS_HANDLER_H
#define  OFS_HANDLER_H
#include "ofs_msg.h"
#include "ofs_opcode.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>



int ofs_handle_fs_msg(struct ofs_msg *);



#endif
