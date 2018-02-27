#ifndef OFS_MSG_H
#define OFS_MSG_H
#include <linux/types.h>

/* the FS syscalls issued from secure world */
#define OFS_MKDIR	1
#define OFS_OPEN	2
#define OFS_READ	3
#define OFS_WRITE	4


#define MAX_FILENAME 99

/* OFS fs request: sec world --> normal world
 * a fs request contains 3 members:
 * 1: request number: indicating the issued syscall (i.e., open, read, write)
 * 2: filename
 * 3: flag */

struct ofs_fs_request {
	int request;	
	int flag;
	char filename[MAX_FILENAME];
};

/* OFS fs response: normal world --> secure world */
struct ofs_fs_response {
	int rw;
	unsigned long blocknr;
	void *payload;
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
int ofs_fs_request_alloc(struct ofs_fs_request *);
struct ofs_msg *ofs_msg_alloc(void);






#endif 
