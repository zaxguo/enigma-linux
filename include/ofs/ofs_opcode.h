#ifndef OFS_OPCODE_H
#define OFS_OPCODE_H
/* lwg: define a set of OFS interfaces and MSG format here
 * The interfaces are mainly three types (from sec world's perspective):
 * 1) The FS request from secure world going out
 * 2) The Block request from normal world going in
 * 3) The Page cache request from normal world going in */

/* OPCODE to navigate through differnet requests */
#define OFS_FS_REQUEST		1
#define OFS_BLK_REQUEST		2
#define OFS_PG_REQUEST		3


#define OFS_FS_RESPONSE			OFS_FS_REQUEST

#define OFS_PG_COPY_TO_SEC		0
#define OFS_PG_COPY_FROM_SEC	1

#define OFS_BLK_READ			1
#define OFS_BLK_WRITE	        2


#endif
