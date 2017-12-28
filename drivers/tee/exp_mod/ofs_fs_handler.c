#include "ofs_msg.h"
#include "ofs_opcode.h"
#include "ofs_handler.h"


int ofs_handle_fs_msg(struct ofs_msg *msg) {
	struct ofs_fs_request *req;
	int request;
	req = &(msg->msg.fs_request);
	request = req->request;
	switch (request) {
		case OFS_MKDIR:
//			ofs_mkdir()
		printk("lwg:%s:mkdirat:%d:%d:\"%s\"\n", __func__, 
						msg->op,\
					   	msg->msg.fs_request.request,\
					   	msg->msg.fs_request.filename);
				break;
		case OFS_OPEN:
		case OFS_READ:
		case OFS_WRITE:
			BUG();
	}
	return 0;
}


