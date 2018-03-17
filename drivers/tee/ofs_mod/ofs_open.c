#include <ofs/ofs_msg.h>
#include <ofs/ofs_util.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/fdtable.h>
#include "ofs_syscall.h"

static inline void ofs_open_response(struct ofs_msg *msg, int fd) {
	msg->op = OFS_FS_RESPONSE;
	msg->msg.fs_response.fd = fd;
	msg->msg.fs_response.blocknr = -1;
	msg->msg.fs_response.pa		 = -1;
	msg->msg.fs_response.rw		 = -1;
	smp_mb();
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
#if 0
	if (fd >= 0) {
		/* we don't need this, test only */
		len = _ofs_read(fd, buf, 10);
		if (len > 0) {
			buf[len] = '\0';
			printk("%s\n", buf);
		}
	}
#endif
	/* FIXME: dirty fix this */
	ofs_res.a3 = return_thread;
	ofs_switch_resume(&ofs_res);
	return 0;
}


