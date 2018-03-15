#ifndef OFS_SYSCALLS_H
#define OFS_SYSCALLS_H

#include <linux/syscalls.h>


/* note the function is defined in optee/ofs/ofs_dir.c */
extern int ofs_mkdir(const char *, int);
extern int ofs_open(const char *, int);
extern int ofs_read(int, char *, int);



#endif

