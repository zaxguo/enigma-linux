#include <ofs/ofs_msg.h>
#include <ofs/ofs_opcode.h>
#include "ofs_syscall.h"
#include "ofs_handler.h"


int ofs_handle_fs_msg(struct ofs_msg *msg) {
	struct ofs_fs_request *req;
	int request, fd;
	char *filename;
	char buf[10];
	char __user *p;
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
		fd = ofs_open(filename, 0666);
		if (fd < 0)
			BUG();
		printk("lwg:%s:open:fd = %d\n", __func__, fd);
		p = (__force char __user *)buf;
		ofs_read(fd, p, 9);
		printk("lwg:%s:reading %s\n", __func__, buf);
		printk("lwg:%s:reading %s\n", __func__, p);
				break;
		case OFS_READ:
		case OFS_WRITE:
			BUG();
	}
	return 0;
}


