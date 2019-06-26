#include <linux/file.h>
#include <linux/fs.h>
#include <ofs/ofs_msg.h>
#include <linux/errno.h>
#include "ofs_handler.h"
#include "ofs_syscall.h"
#include <ofs/ofs_util.h>

extern struct page* write_buf;

static inline void ofs_read_response(struct ofs_msg *msg, int count) {
	ofs_prep_fs_response(msg, OFS_FS_RESPONSE, count, -1, -1, -1);
	memcpy(saved_msg, msg, sizeof(struct ofs_msg));
	trace_printk("lwg:%s:%d:complete count = [%d]\n", __func__, __LINE__, count);
}

static inline void ofs_write_response(struct ofs_msg *msg, int count) {
	ofs_prep_fs_response(msg, OFS_FS_RESPONSE, count, -1, -1, -1);
	memcpy(saved_msg, msg, sizeof(struct ofs_msg));
	trace_printk("lwg:%s:%d:complete count = [%d]\n", __func__, __LINE__, count);
}

/* TODO: this read DOES not take the POS in fd into consideration */
static int _ofs_read(struct file* filp, loff_t offset, char *buf, int count) {
	int len = 0;
	if (!IS_ERR(filp)) {
		/* This fires up the fs logic */
		len = kernel_read(filp, offset, buf, count);
		/* TODO: update the POS in fd */
		if (len < 0) goto err;
		buf[len] = '\0';
		ofs_printk("lwg:%s:%d:read file ino = [%lu]\n", __func__, __LINE__, filp->f_inode->i_ino);
		ofs_printk("lwg:%s:%d:read out %d bytes [%s]\n", __func__, __LINE__, len, buf);
		/* update POS */
		return len;
	}
err:
	printk("lwg:%s:%d:READ FAULT err code = [%d]\n", __func__, __LINE__, len);
	return -EFAULT;
}

/* preserve normal sys_read syntax */
int ofs_read(int fd, char *buf, int count) {
	int len;
	struct file *filp = ofs_fget(fd);
	loff_t pos = filp->f_pos;
	len = _ofs_read(filp, pos, buf, count);
	if (len) {
		/* read successful update pos */
		filp->f_pos = pos + len;
		return len;
	}
	return -EFAULT;
}

static int _ofs_write(struct file* filp, char *buf, int count, loff_t pos) {
	int len = 0;
	if (!IS_ERR(filp)) {
		len = kernel_write(filp, buf, count, pos);
		if (len < 0) goto err;
		/* printk("lwg:%s:%d:write file ino = [%lu], [%d] bytes\n", __func__, __LINE__, filp->f_inode->i_ino, len); */
		return len;
	}
err:
	printk("lwg:%s:%d:WRITE FAULT err code = [%d]\n", __func__, __LINE__, len);
	printk("lwg:%s:%d:File mode = %08x\n", __func__, __LINE__, filp->f_mode);
	return -EFAULT;
}

int ofs_write(int fd, char *buf, int count) {
	int len;
	struct file *filp = ofs_fget(fd);
	loff_t pos = filp->f_pos;
	len = _ofs_write(filp, buf, count, pos);
	if (len) {
		/* read successful update pos */
		filp->f_pos = pos + len;
		return len;
	}
	return -EFAULT;
}

int ofs_read_handler(void *data) {
	int fd, count;
	char buf[4100]; /* slightly larger than 4k */
	struct ofs_msg *msg;
	struct ofs_fs_request *req = (struct ofs_fs_request *)data;
	fd = req->fd;
	count = req->count;
	count = ofs_read(fd, buf, count);
	ofs_printk("lwg:%s:%d:read [%d] ==> [%s]\n", __func__, __LINE__, fd, buf);
	msg = requests_to_msg(req, fs_request);
	ofs_read_response(msg, count >= 0 ? count : 0);
	ofs_res.a3 = return_thread;
	return count;
}

int ofs_write_handler(void *data) {
#if 0
	if (in_atomic()) {
		WARN_ON(1);
		return 0;
	}
#endif
	int fd, count;
	void *buf;
	struct ofs_msg *msg;
	uint8_t *b;
	struct ofs_fs_request *req = (struct ofs_fs_request *)data;
	/* set to be non-preemptible, while ext2_get_block in ofs_write will sleep ! */
	/* buf = kmap(write_buf); */
	fd = req->fd;
	count = req->count;
	buf = kmalloc(count, GFP_KERNEL);
	ofs_printk("lwg:%s:%d:write [%d] bytes to [%d]\n", __func__, __LINE__, count, fd);
	memset(buf, 0x63, count); /* dummy data */
	count = ofs_write(fd, buf, count);
	msg = requests_to_msg(req, fs_request);
	ofs_write_response(msg, count);
	ofs_res.a3 = return_thread;
	kfree(buf);
	return count;
}


