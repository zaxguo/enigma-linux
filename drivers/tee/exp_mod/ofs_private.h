#ifndef OFS_UTIL_H
#define OFS_UTIL_H 

#include "../tee_private.h" /* struct tee_shm */
inline struct ofs_msg *recv_ofs_msg(struct tee_shm *shm) {
	return (struct ofs_msg *)shm->kaddr;
}	

#endif
