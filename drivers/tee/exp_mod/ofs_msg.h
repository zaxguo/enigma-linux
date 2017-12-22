#ifndef OFS_MSG_H
#define OFS_MSG_H

#define OFS_MKDIR	0
#define OFS_OPEN	1
#define OFS_READ	2
#define OFS_WRITE	3


/* OFS fs request: sec world --> normal world
 * a fs request contains 3 members:
 * 1: request number: indicating the issued syscall (i.e., open, read, write)
 * 2: filename
 * 3: flag */

struct ofs_fs_request {
	int request;	
	int flag;
	char *filename;
};

/* OFS fs response: normal world --> secure world */
struct ofs_fs_response {
	int rw;
	unsigned long blocknr;
	void *payload;
};


struct ofs_page_request {

};

struct ofs_page_response {

};


struct ofs_msg {
	union {
		struct ofs_fs_request;
		struct ofs_fs_response;
		struct ofs_page_request;
		struct ofs_page_response;
	} msg;
};
#endif 
