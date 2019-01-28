#include <ofs/ofs_msg.h>
#include <ofs/ofs_util.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/fdtable.h>
#include <linux/delay.h>
#include "ofs_syscall.h"
#include "ofs_handler.h"
#include <linux/dma-mapping.h>
#include <linux/mman.h>
#include <linux/cma.h>
#include "../../../mm/cma.h"

#define OFS_FD 0

extern struct cma cma_areas[MAX_CMA_AREAS];
/* TODO: refactor for code reuse */
static inline void ofs_open_response(struct ofs_msg *msg, int fd) {
	ofs_prep_fs_response(msg, OFS_FS_RESPONSE, fd, -1, -1, -1);
	memcpy(saved_msg, msg, sizeof(struct ofs_msg));
	trace_printk("lwg:%s:%d:complete fd = [%d]\n", __func__, __LINE__, msg->msg.fs_response.fd);
}

static inline void ofs_stat_response(struct ofs_msg *msg, unsigned long size) {
	/* blocknr used as size */
	ofs_prep_fs_response(msg, OFS_FS_RESPONSE, -1, size, -1, -1);
	memcpy(saved_msg, msg, sizeof(struct ofs_msg));
}

static inline void ofs_mmap_response(struct ofs_msg *msg, phys_addr_t pa) {
	ofs_prep_fs_response(msg, OFS_FS_RESPONSE, -1, -1, pa, -1);
	memcpy(saved_msg, msg, sizeof(struct ofs_msg));
}

static inline void ofs_fsync_response(struct ofs_msg *msg, int count) {
	ofs_prep_fs_response(msg, OFS_FS_RESPONSE, count, -1, -1, -1);
	memcpy(saved_msg, msg, sizeof(struct ofs_msg));
	trace_printk("lwg:%s:%d:complete count = [%d]\n", __func__, __LINE__, count);
}


int ofs_stat_handler(void *data) {
	char buf[MAX_FILENAME];
	struct ofs_fs_request *req = (struct ofs_fs_request *)data;
	struct kstat st;
	printk("%s:%d:filename = %s\n", __func__, __LINE__, req->filename);
	int rc	= vfs_stat(req->filename, &st);
	strncpy(buf, req->filename, MAX_FILENAME);
	struct ofs_msg *msg = requests_to_msg(req, fs_request);
	ofs_stat_response(msg, st.size);
	ofs_res.a3 = return_thread;
	printk("lwg:%s:%d:[%s] has size %llx\n", __func__, __LINE__, buf, st.size);
	return 0;
}

int ofs_fstat_handler(void *data) {
	struct ofs_fs_request *req = (struct ofs_fs_request *)data;
	struct kstat st;
	int fd =  req->fd;
	struct file *filp = ofs_fget(fd);
	if (filp) {
		printk("%s:%d:fd = %d\n", __func__, __LINE__, fd);
		int rc	= vfs_getattr(&filp->f_path, &st);
		struct ofs_msg *msg = requests_to_msg(req, fs_request);
		ofs_stat_response(msg, st.size);
		ofs_res.a3 = return_thread;
		printk("lwg:%s:%d:[%d] has size %llx\n", __func__, __LINE__, fd, st.size);
	}
	return 0;
}


/* After allocating memories from CMA we do a byte-wise comparison just in case */
static ofs_mmap_sanity_test(void *dst, void *ref, size_t byte) {

}

/* We use CMA to handle MMAP allocation */
int ofs_mmap_handler(void *data) {
	struct ofs_fs_request *req = (struct ofs_fs_request *)data;
	struct page *page;
	unsigned int img_size, pos;
	struct ofs_msg *msg;
	uint8_t buf[PAGE_SIZE];
	int nr_pages, i, pfn, start_pfn, allocated;


	printk("lwg:%s:%d:fd = %d, count = %08x, flag = %08x\n", __func__, __LINE__, req->fd, req->count, req->flag);

	int fd = req->fd;
	unsigned int sz = req->count;
	int flags = req->flag;
	int prot = PROT_READ;
	/* struct file* f = filp_open(fs, O_RDWR, 0600); */
	struct file *f = ofs_fget(fd); /* lwg: should have been opened already */
	if (!f) {
		printk("no file...\n");
		return -1;
	}
	img_size =  sz;
	nr_pages = (img_size >> PAGE_SHIFT) + 1; /* XXX */
	allocated = 0;
	printk("file size: %lx, trying to allocated %d pages...\n", sz, nr_pages);
	for (i = 0; i < MAX_CMA_AREAS; i++) {
		struct cma *cma = &cma_areas[i];
		page = cma_alloc(cma, nr_pages, 8);
		if (page) {
			printk("allocated mem at area %d\n", i);
			allocated = 1;
			break;
		}
		printk("could not alloc at area %d\n", i);
		printk("cma count = %d\n", cma->count);
	}
	if (!allocated) {
		printk("CMA alloc failed! abort...\n");
		return -1;
	}
	pos = 0;
	pfn = page_to_pfn(page);
	start_pfn = page_to_pfn(page);
	printk("%s:starting to read file into cma...\n", __func__);
	int count = 0;
	for (i = 0; i < nr_pages; i++, pfn++) {
		void *addr;
		struct page *tmp = pfn_to_page(pfn);
		count = kernel_read(f, pos, buf, PAGE_SIZE);
		pos += count;
		addr = kmap(tmp);
		memcpy(addr, buf, PAGE_SIZE);
		kunmap(tmp);
	}
	img_pa = start_pfn << PAGE_SHIFT;
	printk("%s:%d loaded into CMA, starting pa = %08x, nr_pages = %d, size = %08x\n",
			__func__,
			fd,
			img_pa,
			nr_pages,
			pos);
	msg = requests_to_msg(req, fs_request);
	ofs_mmap_response(msg, img_pa);
	/* FIXME: dirty fix this */
	ofs_res.a3 = return_thread;
	return 0;
}



int ofs_open_handler(void *data) {
	char buf[15];
	int len, flag, fd;
	struct ofs_msg *msg;
	struct file *file;
	struct ofs_fs_request *req = (struct ofs_fs_request *)data;
	flag = req->flag;
	fd = OFS_FD;
	/* ofs_open does not work properly with kernel space open, fallback to this */
	file = filp_open(req->filename, flag | O_SYNC, 0600);
	set_ofs_file(file);
	/* file = fget(fd); */
	if (!file) {
		printk("lwg:%s:%d:ERROR, no file pointer\n", __func__, __LINE__);
	}
	__fd_install(&ofs_files, fd, file);
	ofs_printk("lwg:%s:%d:fd [%d] installed to ofs_files\n", __func__, __LINE__, fd);
	msg = requests_to_msg(req, fs_request);
	ofs_open_response(msg, fd);
	/* FIXME: dirty fix this */
	ofs_res.a3 = return_thread;
	return 0;
}

static inline int _ofs_fsync(int fd) {
	struct file *filp;
	filp = ofs_fget(fd);
	return vfs_fsync(filp, 0);
}

int ofs_fsync_handler(void *data) {
	int fd, r;
	struct ofs_msg *msg;
	struct ofs_fs_request *req = (struct ofs_fs_request *)data;
	fd = req->fd;
	ofs_printk("lwg:%s:%d:fsync for [%d]\n", __func__, __LINE__, fd);
	/* r = ofs_fsync(fd); */
	r = _ofs_fsync(fd);
	ofs_printk("lwg:%s:%d:ret = %d\n", __func__, __LINE__, r);
	msg = requests_to_msg(req, fs_request);
	ofs_fsync_response(msg, 0);
	ofs_res.a3 = return_thread;
	return 0;
}


