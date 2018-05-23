#include <ofs/ofs_msg.h>
#include <ofs/ofs_opcode.h>
#include "ofs_syscall.h"
#include "ofs_handler.h"
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <ofs/ofs_util.h>

/* index corresponds to its opcode */
static const char *ofs_syscalls[OFS_MAX_SYSCALLS] = {
	"X",
	"ofs_mkdirat",	 /* ofs_mkdir 1 */
	"ofs_open",		 /* ofs_open 2 */
	"ofs_read",		 /* ofs_read 3 */
	"ofs_write",	 /* ofs_write 4 */
	"X",
};

/* Note this should be used BEFORE any handler since handler will
 * repurpose this msg (i.e., change it to response) */
static inline void dump_ofs_fs_request(struct ofs_fs_request *req) {
	printk("lwg:%s:%s:[%s]\n", __func__, ofs_syscalls[req->request], req->filename);
}

static int ofs_fs_handler(void *data) {
	char *filename;
	int request;
	struct ofs_fs_request *req= (struct ofs_fs_request *)data;
	request = req->request;
	filename = req->filename;
	switch (request) {
		case OFS_MKDIR:
			ofs_mkdir(filename, 0777);
			break;
		case OFS_OPEN:
			dump_ofs_fs_request(req);
			ofs_open_handler(req);
			break;
		case OFS_READ:
		case OFS_WRITE:
			BUG();
	}
	ofs_switch_resume(&ofs_res);
}


int ofs_handle_fs_msg(struct ofs_msg *msg) {
	struct ofs_fs_request *req;
	req = &(msg->msg.fs_request);
	/* struct task_struct *tsk = kthread_run(ofs_fs_handler, (void *)req, "ofs_fs_handler"); */
	ofs_fs_handler(req);
	return 0;
}


