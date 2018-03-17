#include <linux/file.h>
#include <linux/fs.h>
#include <ofs/ofs_msg.h>
#include <linux/errno.h>
#include "ofs_handler.h"

static inline struct file *ofs_fget(int fd) {
	return fcheck_files(&ofs_files, fd);
}


/* TODO: this read DOES not take the POS in fd into consideration */
int _ofs_read(struct file* filp, loff_t offset, char *buf, int count) {
	int len = 0;
	if (!IS_ERR(filp)) {
		/* This fires up the fs logic */
		len = kernel_read(filp, offset, buf, count);
		/* TODO: update the POS in fd */
		if (len < 0) goto err;
		buf[len] = '\0';
		printk("lwg:%s:%d:read file ino = [%lu]\n", __func__, __LINE__, filp->f_inode->i_ino);
		printk("lwg:%s:%d:read out %d bytes [%s]\n", __func__, __LINE__, len, buf);
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
	if (len > 0) {
		/* read successful update pos */
		filp->f_pos = pos + len;
		return len;
	}
	return -EFAULT;
}

