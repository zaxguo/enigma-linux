#include <linux/net.h>
#include <net/sock.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <asm/uaccess.h>
#include <linux/socket.h>
#include <ofs/ofs_msg.h>


int ofs_fs_send(struct ofs_fs_request *req);
