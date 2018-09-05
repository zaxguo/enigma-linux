#include <ofs/ofs_msg.h>
#include <ofs/ofs_opcode.h>
#include "ofs_syscall.h"
#include "ofs_handler.h"
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <ofs/ofs_util.h>
#include <ofs/ofs_net.h>
#include <linux/socket.h>

/* index corresponds to its opcode */
static const char *ofs_syscalls[OFS_MAX_SYSCALLS] = {
	"X",
	"ofs_mkdirat",	 /* ofs_mkdir 1 */
	"ofs_open",		 /* ofs_open  2 */
	"ofs_read",		 /* ofs_read  3 */
	"ofs_write",	 /* ofs_write 4 */
	"ofs_fsync",	 /* ofs_fsync 5 */
	"X",
};

extern struct socket *conn_socket; /* send msg to server */

/* Note this should be used BEFORE any handler since handler will
 * repurpose this msg (i.e., change it to response) */

static inline void dump_ofs_fs_request(struct ofs_fs_request *req) {
	printk("lwg:%s:%s:[%s]\n", __func__, ofs_syscalls[req->request]);
}

static int ofs_fs_handler(void *data) {
	char *filename;
	int request, ret;
	struct ofs_fs_request *req= (struct ofs_fs_request *)data;
	request = req->request;
	filename = req->filename;
	printk("lwg:%s:%s:[%s]\n", __func__, ofs_syscalls[req->request], filename);
	if (conn_socket) {
		ofs_fs_send(req);
	}
	switch (request) {
		case OFS_MKDIR:
			ofs_mkdir(filename, 0777);
			break;
		case OFS_OPEN:
			/* dump_ofs_fs_request(req); */
			ofs_open_handler(req);
			break;
		case OFS_READ:
			ofs_read_handler(req);
			break;
		case OFS_WRITE:
			ofs_write_handler(req);
			break;
		case OFS_FSYNC:
			ofs_fsync_handler(req);
			break;
		default:
			BUG();
	}
	ofs_switch_resume(&ofs_res);
	return 0;
}

int ofs_handle_fs_msg(struct ofs_msg *msg) {
	struct ofs_fs_request *req;
	req = &(msg->msg.fs_request);
	/* struct task_struct *tsk = kthread_run(ofs_fs_handler, (void *)req, "ofs_fs_handler"); */
	ofs_fs_handler(req);
	return 0;
}


