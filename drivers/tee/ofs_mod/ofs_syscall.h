#ifndef OFS_SYSCALLS_H
#define OFS_SYSCALLS_H

#include <linux/syscalls.h>
#include <linux/file.h>

#define OFS_MAX_SYSCALLS		10

extern struct files_struct ofs_files;

/* note the function is defined in optee/ofs/ofs_dir.c */
extern int ofs_mkdir(const char *, int);
/* wrapper for syscalls */
extern int ofs_open(const char *, int);
extern int ofs_read(int, char *, int);
extern int ofs_fsync(int);



#endif

