#ifndef OFS_MSG_H
#define OFS_MSG_H
#include <linux/types.h>
#include <linux/kernel.h>


// #define OFS_BENCH	1

#ifndef OFS_BENCH
#define ofs_printk(fmt, ...) printk("OFS:CPU[%d]:%s:%d " fmt, smp_processor_id(), __func__, __LINE__, ##__VA_ARGS__)
#else
#define ofs_printk(...) (void)0
#endif

extern unsigned long return_thread;
/* the FS syscalls issued from secure world */
#define OFS_MKDIR	1
#define OFS_OPEN	2
#define OFS_READ	3
#define OFS_WRITE	4
#define OFS_FSYNC	5
#define OFS_STAT	6
#define OFS_FSTAT	7
#define OFS_MMAP	8
#define OFS_MUNMAP	9

#define MAX_FILENAME 256

/* OFS fs request: sec world --> normal world
 * a fs request contains 3 members:
 * 1: request number: indicating the issued syscall (i.e., open, read, write)
 * 2: filename
 * 3: flag */

struct ofs_fs_request {
	int request;
	int flag;
	char filename[MAX_FILENAME];
	/* really ugly... */
	int fd;
	unsigned int count;
};

/* OFS fs response: normal world --> secure world */
struct ofs_fs_response {
	int rw;
	unsigned long blocknr;
	phys_addr_t pa;
	int fd;
};


struct ofs_page_request {
	int request;
	int flag;
	pgoff_t index;
	phys_addr_t pa;
};

struct ofs_page_response {
	phys_addr_t pa;
};


struct ofs_msg {
	int op;
	union {
		struct ofs_fs_request fs_request;
		struct ofs_fs_response fs_response;
		struct ofs_page_request page_request;
		struct ofs_page_response page_response;
	} msg; /* C99 doens't support anonymous union */
};


/* helper function */
#define requests_to_msg(req, name) \
	container_of(req, struct ofs_msg, msg.name)

static inline int serialize_ofs_fs_ops(struct ofs_fs_request *req, char *buf) {
	/* only open has filename */
	if (req->request == OFS_OPEN) {
		return sprintf(buf, "{\"req\":%d,\"flag\":%d,\"name\":\"%s\",\"fd\":%d,\"count\":%d}",
				req->request,req->flag, req->filename, req->fd, req->count);
	}
	return sprintf(buf, "{\"req\":%d,\"flag\":%d,\"name\":\"\",\"fd\":%d,\"count\":%d}",
				req->request,req->flag, req->fd, req->count);
}



int ofs_fs_request_alloc(struct ofs_fs_request *);
struct ofs_msg *ofs_msg_alloc(void);






#endif
