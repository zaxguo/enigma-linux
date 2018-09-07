#include <linux/net.h>
#include <net/sock.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <asm/uaccess.h>
#include <linux/socket.h>
#include <ofs/ofs_msg.h>
#include <linux/list.h>

struct ofs_cloud_bio {
	int blk;
	struct list_head list;
};

extern struct list_head ofs_cloud_bio_list;
int ofs_fs_send(struct ofs_fs_request *req);

