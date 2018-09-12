#include <linux/syscalls.h>

int ofs_mkdir(const char *path, int mode) {
	return sys_mkdirat(-100, path, mode);
}
EXPORT_SYMBOL(ofs_mkdir);

int ofs_open(const char *path, int flags) {
	printk("lwg:%s:%d:entered, flags = %x, O_RDWR = %x\n", __func__, __LINE__, flags, O_RDWR);
	return sys_open(path, flags, 0);
}
EXPORT_SYMBOL(ofs_open);


int ofs_read(int fd, char *buf, int count) {
	return sys_read(fd, buf, count);
}
EXPORT_SYMBOL(ofs_read);

int ofs_fsync(int fd) {
	printk("lwg:%s:%d:entered\n", __func__, __LINE__);
	return vfs_fsync(fd, 0);
}
EXPORT_SYMBOL(ofs_fsync);


