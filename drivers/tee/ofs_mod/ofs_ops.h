#ifndef OFS_OPS_H
#define OFS_OPS_H

/* lwg: define a set of OFS interfaces and MSG format here
 * The interfaces are mainly three types (from sec world's perspective):
 * 1) The FS request from secure world going out
 * 2) The Block request from normal world going in
 * 3) The Page cache request from normal world going in */

/* 1) The FS request from secure world going out */
#define OFS_MKDIR	1
#define OFS_OPEN	2
#define OFS_READ	3
#define OFS_WRITE	4



/* 2) The Block request from normal world going in
 * We will define RW flags later */
#define OFS_MSG_BIO




/* 3) The Page cache request from normal world going in */
#define OFS_MSG_PG_ALLOC
#define OFS_MSG_PG_GET
#define OFS_MSG_PG_WRITE
#define OFS_MSG_PG_READ



#endif
