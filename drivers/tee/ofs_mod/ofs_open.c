#include <ofs/ofs_msg.h>
#include <ofs/ofs_util.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/fdtable.h>
#include "ofs_syscall.h"

/* TODO: refactor for code reuse */
static inline void ofs_open_response(struct ofs_msg *msg, int fd) {
	ofs_prep_fs_response(msg, OFS_FS_RESPONSE, fd, -1, -1, -1);
	printk("lwg:%s:%d:complete fd = [%d]\n", __func__, __LINE__, msg->msg.fs_response.fd);
}

int ofs_open_handler(void *data) {
	char buf[15];
	int len;
	struct ofs_msg *msg;
	struct file *file;
	struct ofs_fs_request *req = (struct ofs_fs_request *)data;
	int fd = ofs_open(req->filename, 0666);
	file = fget(fd);
	if (IS_ERR(file)) {
		printk("lwg:%s:%d:ERROR, no file pointer\n", __func__, __LINE__);
	}
	__fd_install(&ofs_files, fd, file);
	printk("lwg:%s:%d:fd [%d] installed to ofs_files\n", __func__, __LINE__, fd);
	msg = requests_to_msg(req, fs_request);
	ofs_open_response(msg, fd);
	/* FIXME: dirty fix this */
	ofs_res.a3 = return_thread;
	return 0;
}


