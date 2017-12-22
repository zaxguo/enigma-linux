#include <linux/syscalls.h>

int ofs_mkdir(const char *path, int mode) {
	sys_mkdirat(-100, path, mode);
}
EXPORT_SYMBOL(ofs_mkdir);


