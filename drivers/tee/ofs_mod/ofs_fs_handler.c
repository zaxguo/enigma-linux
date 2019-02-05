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
#include "ofs_obfuscation.h"

/* index corresponds to its opcode */
static const char *ofs_syscalls[OFS_MAX_SYSCALLS] = {
	"X",
	"ofs_mkdirat",	 /* ofs_mkdir 1 */
	"ofs_open",		 /* ofs_open  2 */
	"ofs_read",		 /* ofs_read  3 */
	"ofs_write",	 /* ofs_write 4 */
	"ofs_fsync",	 /* ofs_fsync 5 */
	"ofs_stat",		 /* ofs_stat  6	*/
	"ofs_fstat",	 /* ofs_fstat 7	*/
	"ofs_mmap",		 /* ofs_mmap  8	*/
	"X",
};

extern struct socket *conn_socket; /* send msg to server */
struct ofs_msg *saved_msg;

/* Note this should be used BEFORE any handler since handler will
 * repurpose this msg (i.e., change it to response) */

static inline void dump_ofs_fs_request(struct ofs_fs_request *req) {
	ofs_printk("lwg:%s:[%s]\n", __func__, ofs_syscalls[req->request]);
}

static void *restore_ofs_msg(void *msg, void *saved, int size) {
	return memcpy(msg, saved, size);
}

static int ofs_fs_handler(void *data) {
	char *filename;
	int request, ret;
	struct ofs_fs_request *req;
	struct timespec start,end,diff;
	struct ofs_fs_request *saved = kmalloc(sizeof(*req), GFP_KERNEL);
	/* struct ofs_fs_request *req = (struct ofs_fs_request *)data; */
	/* pointer to data will be modified by subsequent fs calls */
	getnstimeofday(&start);
	req = data;
	request = req->request;
	filename = req->filename;
	/* ofs_printk("lwg:%s:%s:count = %lx\n", __func__, ofs_syscalls[req->request], req->count); */
	/* dump_ofs_fs_request(data); */
#if 1
	memcpy(saved, data, sizeof(struct ofs_fs_request));
	/* this will mess up the shared mem */
	/* ofs_obfuscate(request); */
	restore_ofs_msg(data, saved, sizeof(struct ofs_fs_request));
	smp_mb();
#endif
	/* dump_ofs_fs_request(data); */
	/* micro benchmarks... no net */
#if 1
	if (conn_socket) {
		ofs_fs_send(req);
	}
#endif
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
		case OFS_STAT:
			ofs_stat_handler(req);
			break;
		case OFS_FSTAT:
			ofs_fstat_handler(req);
			break;
		case OFS_MMAP:
			ofs_mmap_handler(req);
			break;
		default:
			BUG();
	}
	/* Up to this point, all bio should be consumed already,
	 * if not, destroy the bio list */
	if (!list_empty(&ofs_cloud_bio_list)) {
		ofs_printk("warning:%s:%d:why not consumed??\n", __func__, __LINE__);
		ofs_cloud_bio_del_all();
	}
	kfree(saved);
	struct ofs_msg *msg = requests_to_msg(req, fs_request);
	/* dirty */
#if 0
	/* memcpy(saved, data, sizeof(struct ofs_fs_request)); */
	/* this will mess up the shared mem */
	ofs_obfuscate(request);
	/* restore_ofs_msg(data, saved, sizeof(struct ofs_fs_request)); */
	smp_mb();
#endif

	if (msg->op != OFS_FS_RESPONSE) {
		printk("%s:dirty fixing msg before returning...\n", __func__);
		memcpy(msg, saved_msg, sizeof(struct ofs_msg));
	}
	getnstimeofday(&end);
	diff = timespec_sub(end, start);
	printk("req [%d] handling time = %ld s, %ld ns\n", request, diff.tv_sec, diff.tv_nsec);
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


