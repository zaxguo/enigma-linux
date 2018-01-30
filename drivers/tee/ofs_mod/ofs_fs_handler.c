#include <ofs/ofs_msg.h>
#include <ofs/ofs_opcode.h>
#include "ofs_syscall.h"
#include "ofs_handler.h"


int ofs_handle_fs_msg(struct ofs_msg *msg) {
	struct ofs_fs_request *req;
	int request;
	char *filename;
	req = &(msg->msg.fs_request);
	request = req->request;
	filename = req->filename;
	switch (request) {
		case OFS_MKDIR:
//		ofs_mkdir(filename, 0777);
		printk("lwg:%s:mkdirat:%d:%d:\"%s\"\n", __func__, 
						msg->op,\
					   	msg->msg.fs_request.request,\
					   	msg->msg.fs_request.filename);
				break;
		case OFS_OPEN:
		printk("lwg:%s:oepn:%d:%d:\"%s\"\n", __func__, 
						msg->op,\
					   	msg->msg.fs_request.request,\
					   	msg->msg.fs_request.filename);
				break;
		case OFS_READ:
		case OFS_WRITE:
			BUG();
	}
	return 0;
}


