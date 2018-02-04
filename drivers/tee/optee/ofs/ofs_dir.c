#include <linux/syscalls.h>

int ofs_mkdir(const char *path, int mode) {
	return sys_mkdirat(-100, path, mode);
}
EXPORT_SYMBOL(ofs_mkdir);

int ofs_open(const char *path, int mode) {
	printk("lwg:%s:%d:entered\n", __func__, __LINE__);
	return sys_open(path, O_RDWR|O_APPEND, S_IRWXU);
}
EXPORT_SYMBOL(ofs_open);


int ofs_read(int fd, char *buf, int count) {
	return sys_read(fd, buf, count);
}
EXPORT_SYMBOL(ofs_read);


