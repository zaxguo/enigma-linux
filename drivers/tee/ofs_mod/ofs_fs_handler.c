#include <ofs/ofs_msg.h>
#include <ofs/ofs_opcode.h>
#include "ofs_syscall.h"
#include "ofs_handler.h"
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <linux/sched.h>

int _ofs_read(int fd, char *buf, int count) {
	struct fd f;
	struct file *filp;
	int len = 0;
	f = fdget(fd);
	filp = f.file;
	if (!IS_ERR(filp)) {
		len = kernel_read(filp, 0, buf, count);
		if (len < 0) goto err;
		buf[len] = '\0';
		printk("lwg:%s:%d:read file ino = [%lu]\n", __func__, __LINE__, filp->f_inode->i_ino);
		printk("lwg:%s:%d:read out %d bytes [%s]\n", __func__, __LINE__, len, buf);
		return len;
	}
err:
	printk("lwg:%s:%d:READ FAULT err code = [%d]\n", __func__, __LINE__, len);
	fdput(f);
	return -EFAULT;
}

int ofs_open_handler(void *data) {
	char buf[15];
	int len;
	struct ofs_fs_request *req = (struct ofs_fs_request *)data;
	int fd = ofs_open(req->filename, 0666);
	if (fd >= 0) {
		len = _ofs_read(fd, buf, 10);
		if (len > 0) {
			buf[len] = '\0';
			printk("%s\n", buf);
		}
	}
	return 0;
}


static int ofs_fs_handler(void *data) {
	char *filename;
	int request;
	struct ofs_fs_request *req= (struct ofs_fs_request *)data;
	request = req->request;
	filename = req->filename;
	switch (request) {
		case OFS_MKDIR:
			//		ofs_mkdir(filename, 0777);
			/* printk("lwg:%s:mkdirat:%d:%d:\"%s\"\n", __func__, */
			/*         msg->op,\ */
			/*         msg->msg.fs_request.request,\ */
			/*         msg->msg.fs_request.filename); */
			break;
		case OFS_OPEN:
			/* printk("lwg:%s:oepn:%d:%d:\"%s\"\n", __func__, */
			/*         msg->op,\ */
			/*         msg->msg.fs_request.request,\ */
			/*         msg->msg.fs_request.filename); */
			ofs_open_handler(req);
			break;
		case OFS_READ:
		case OFS_WRITE:
			BUG();
	}
}


int ofs_handle_fs_msg(struct ofs_msg *msg) {
	struct ofs_fs_request *req;
	req = &(msg->msg.fs_request);
	struct task_struct *tsk = kthread_run(ofs_fs_handler, (void *)req, "ofs_fs_handler");
	return 0;
}


