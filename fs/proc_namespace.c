/*
 * fs/proc_namespace.c - handling of /proc/<pid>/{mounts,mountinfo,mountstats}
 *
 * In fact, that's a piece of procfs; it's *almost* isolated from
 * the rest of fs/proc, but has rather close relationships with
 * fs/namespace.c, thus here instead of fs/proc
 *
 */
#include <linux/mnt_namespace.h>
#include <linux/nsproxy.h>
#include <linux/security.h>
#include <linux/fs_struct.h>
#include <linux/random.h>
#include <linux/fdtable.h>
#include "proc/internal.h" /* only for get_proc_task() in ->open() */

#include "pnode.h"
#include "internal.h"
#include "obfuscate.h"

static unsigned mounts_poll(struct file *file, poll_table *wait)
{
	struct seq_file *m = file->private_data;
	struct proc_mounts *p = m->private;
	struct mnt_namespace *ns = p->ns;
	unsigned res = POLLIN | POLLRDNORM;
	int event;

	poll_wait(file, &p->ns->poll, wait);

	event = ACCESS_ONCE(ns->event);
	if (m->poll_event != event) {
		m->poll_event = event;
		res |= POLLERR | POLLPRI;
	}

	return res;
}

struct proc_fs_info {
	int flag;
	const char *str;
};

static int show_sb_opts(struct seq_file *m, struct super_block *sb)
{
	static const struct proc_fs_info fs_info[] = {
		{ MS_SYNCHRONOUS, ",sync" },
		{ MS_DIRSYNC, ",dirsync" },
		{ MS_MANDLOCK, ",mand" },
		{ MS_LAZYTIME, ",lazytime" },
		{ 0, NULL }
	};
	const struct proc_fs_info *fs_infop;

	for (fs_infop = fs_info; fs_infop->flag; fs_infop++) {
		if (sb->s_flags & fs_infop->flag)
			seq_puts(m, fs_infop->str);
	}

	return security_sb_show_options(m, sb);
}

static void show_mnt_opts(struct seq_file *m, struct vfsmount *mnt)
{
	static const struct proc_fs_info mnt_info[] = {
		{ MNT_NOSUID, ",nosuid" },
		{ MNT_NODEV, ",nodev" },
		{ MNT_NOEXEC, ",noexec" },
		{ MNT_NOATIME, ",noatime" },
		{ MNT_NODIRATIME, ",nodiratime" },
		{ MNT_RELATIME, ",relatime" },
		{ 0, NULL }
	};
	const struct proc_fs_info *fs_infop;

	for (fs_infop = mnt_info; fs_infop->flag; fs_infop++) {
		if (mnt->mnt_flags & fs_infop->flag)
			seq_puts(m, fs_infop->str);
	}
}

static inline void mangle(struct seq_file *m, const char *s)
{
	seq_escape(m, s, " \t\n\\");
}

static void show_type(struct seq_file *m, struct super_block *sb)
{
	mangle(m, sb->s_type->name);
	if (sb->s_subtype && sb->s_subtype[0]) {
		seq_putc(m, '.');
		mangle(m, sb->s_subtype);
	}
}

int enigma_k = 0;

static int show_vfsmnt(struct seq_file *m, struct vfsmount *mnt)
{
	struct proc_mounts *p = m->private;
	struct mount *r = real_mount(mnt);
	struct path mnt_path = { .dentry = mnt->mnt_root, .mnt = mnt };
	struct super_block *sb = mnt_path.dentry->d_sb;
	int err;

	printk("lwg:curr enigma K = %d\n", enigma_k);
	if (sb->s_op->show_devname) {
		err = sb->s_op->show_devname(m, mnt_path.dentry);
		if (err)
			goto out;
	} else {
		mangle(m, r->mnt_devname ? r->mnt_devname : "none");
	}
	seq_putc(m, ' ');
	/* mountpoints outside of chroot jail will give SEQ_SKIP on this */
	err = seq_path_root(m, &mnt_path, &p->root, " \t\n\\");
	if (err)
		goto out;
	seq_putc(m, ' ');
	show_type(m, sb);
	seq_puts(m, __mnt_is_readonly(mnt) ? " ro" : " rw");
	err = show_sb_opts(m, sb);
	if (err)
		goto out;
	show_mnt_opts(m, mnt);
	if (sb->s_op->show_options)
		err = sb->s_op->show_options(m, mnt_path.dentry);
	seq_puts(m, " 0 0\n");
out:
	return err;
}

static int show_mountinfo(struct seq_file *m, struct vfsmount *mnt)
{
	struct proc_mounts *p = m->private;
	struct mount *r = real_mount(mnt);
	struct super_block *sb = mnt->mnt_sb;
	struct path mnt_path = { .dentry = mnt->mnt_root, .mnt = mnt };
	int err;

	seq_printf(m, "%i %i %u:%u ", r->mnt_id, r->mnt_parent->mnt_id,
		   MAJOR(sb->s_dev), MINOR(sb->s_dev));
	if (sb->s_op->show_path) {
		err = sb->s_op->show_path(m, mnt->mnt_root);
		if (err)
			goto out;
	} else {
		seq_dentry(m, mnt->mnt_root, " \t\n\\");
	}
	seq_putc(m, ' ');

	/* mountpoints outside of chroot jail will give SEQ_SKIP on this */
	err = seq_path_root(m, &mnt_path, &p->root, " \t\n\\");
	if (err)
		goto out;

	seq_puts(m, mnt->mnt_flags & MNT_READONLY ? " ro" : " rw");
	show_mnt_opts(m, mnt);

	/* Tagged fields ("foo:X" or "bar") */
	if (IS_MNT_SHARED(r))
		seq_printf(m, " shared:%i", r->mnt_group_id);
	if (IS_MNT_SLAVE(r)) {
		int master = r->mnt_master->mnt_group_id;
		int dom = get_dominating_id(r, &p->root);
		seq_printf(m, " master:%i", master);
		if (dom && dom != master)
			seq_printf(m, " propagate_from:%i", dom);
	}
	if (IS_MNT_UNBINDABLE(r))
		seq_puts(m, " unbindable");

	/* Filesystem specific data */
	seq_puts(m, " - ");
	show_type(m, sb);
	seq_putc(m, ' ');
	if (sb->s_op->show_devname) {
		err = sb->s_op->show_devname(m, mnt->mnt_root);
		if (err)
			goto out;
	} else {
		mangle(m, r->mnt_devname ? r->mnt_devname : "none");
	}
	seq_puts(m, sb->s_flags & MS_RDONLY ? " ro" : " rw");
	err = show_sb_opts(m, sb);
	if (err)
		goto out;
	if (sb->s_op->show_options)
		err = sb->s_op->show_options(m, mnt->mnt_root);
	seq_putc(m, '\n');
out:
	return err;
}

static int show_vfsstat(struct seq_file *m, struct vfsmount *mnt)
{
	struct proc_mounts *p = m->private;
	struct mount *r = real_mount(mnt);
	struct path mnt_path = { .dentry = mnt->mnt_root, .mnt = mnt };
	struct super_block *sb = mnt_path.dentry->d_sb;
	int err;

	/* device */
	if (sb->s_op->show_devname) {
		seq_puts(m, "device ");
		err = sb->s_op->show_devname(m, mnt_path.dentry);
		if (err)
			goto out;
	} else {
		if (r->mnt_devname) {
			seq_puts(m, "device ");
			mangle(m, r->mnt_devname);
		} else
			seq_puts(m, "no device");
	}

	/* mount point */
	seq_puts(m, " mounted on ");
	/* mountpoints outside of chroot jail will give SEQ_SKIP on this */
	err = seq_path_root(m, &mnt_path, &p->root, " \t\n\\");
	if (err)
		goto out;
	seq_putc(m, ' ');

	/* file system type */
	seq_puts(m, "with fstype ");
	show_type(m, sb);

	/* optional statistics */
	if (sb->s_op->show_stats) {
		seq_putc(m, ' ');
		err = sb->s_op->show_stats(m, mnt_path.dentry);
	}

	seq_putc(m, '\n');
out:
	return err;
}


static int mounts_open_common(struct inode *inode, struct file *file,
			      int (*show)(struct seq_file *, struct vfsmount *))
{
	struct task_struct *task = get_proc_task(inode);
	struct nsproxy *nsp;
	struct mnt_namespace *ns = NULL;
	struct path root;
	struct proc_mounts *p;
	struct seq_file *m;
	int ret = -EINVAL;

	if (!task)
		goto err;

	task_lock(task);
	nsp = task->nsproxy;
	if (!nsp || !nsp->mnt_ns) {
		task_unlock(task);
		put_task_struct(task);
		goto err;
	}
	ns = nsp->mnt_ns;
	get_mnt_ns(ns);
	if (!task->fs) {
		task_unlock(task);
		put_task_struct(task);
		ret = -ENOENT;
		goto err_put_ns;
	}
	get_fs_root(task->fs, &root);
	task_unlock(task);
	put_task_struct(task);

	ret = seq_open_private(file, &mounts_op, sizeof(struct proc_mounts));
	if (ret)
		goto err_put_path;

	m = file->private_data;
	m->poll_event = ns->event;

	p = m->private;
	p->ns = ns;
	p->root = root;
	p->show = show;
	p->cached_event = ~0ULL;

	return 0;

 err_put_path:
	path_put(&root);
 err_put_ns:
	put_mnt_ns(ns);
 err:
	return ret;
}

static int mounts_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	struct proc_mounts *p = m->private;
	path_put(&p->root);
	put_mnt_ns(p->ns);
	return seq_release_private(inode, file);
}

static int mounts_open(struct inode *inode, struct file *file)
{
	return mounts_open_common(inode, file, show_vfsmnt);
}

static int mountinfo_open(struct inode *inode, struct file *file)
{
	return mounts_open_common(inode, file, show_mountinfo);
}

static int mountstats_open(struct inode *inode, struct file *file)
{
	return mounts_open_common(inode, file, show_vfsstat);
}


static ssize_t mounts_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
	int k= 0;
	char tmp[128];
	if (copy_from_user(tmp, buf, count)) {
		return -EFAULT;
	}
	sscanf(tmp, "%d\n", &k);
	enigma_k = k;
	printk("lwg:%s:Adjusting K to %d...\n", __func__, enigma_k);
	return count;
}


static inline unsigned long get_lblk_cnt(struct inode *inode) {
	return (i_size_read(inode) + (1 << inode->i_blkbits) - 1)
							>> inode->i_blkbits;
}

/* lwg: from lib/sort.c*/
static void u64_swap(void *a, void *b)
{
	u64 t = *(u64 *)a;
	*(u64 *)a = *(u64 *)b;
	*(u64 *)b = t;
}

/* Fisher-Yate shuffling algo */
static int shuffle_bmap(unsigned long *arr, unsigned long cnt) {
	unsigned long i;
	for (i = cnt - 1; i > 0; i--) {
		unsigned long j = get_random_long() % (i + 1);
		u64_swap(&arr[i], &arr[j]);
	}
	return 0;
}

int sanity_check_bmap(unsigned long *arr, unsigned long cnt) {
	unsigned long i, result, comp;
	result	= 0;
	comp = 0;
	for (i = cnt - 1; i > cnt - 10 ; i--) {
			printk("[%d]:%lu ", i, arr[i]);
	}
	printk("\n");
	for (i = 0; i < cnt; i++) {
		result += i;
		comp += arr[i];
	}
	return (comp == result);
}


/* SRC + bmap must produce DST */
int sanity_check_shuffled_blocks(struct file *src, unsigned long *bmap, struct file *dst, unsigned long start, unsigned long cnt, unsigned int blkbits) {
	unsigned long i;
	ssize_t ret = -EBADF;
	loff_t pos, pos_shuffled;
	char *tmp = kmalloc(1 << blkbits, GFP_KERNEL);
	char *tmp2 = kmalloc(1 << blkbits, GFP_KERNEL);
	/* lwg: this should never be called within vfs_read() in case of recursion!!!!! */
	for (i = start; i < start + cnt; i++) {
		pos = i << blkbits;
		pos_shuffled = bmap[i] << blkbits;
		ret = vfs_read(src, tmp, 1 << blkbits, 	&pos);
		ret = vfs_read(dst, tmp2, 1 << blkbits, &pos_shuffled);
		if (memcmp(tmp, tmp2, 1 << blkbits) != 0) {
			printk(KERN_ERR"lwg:%s:%d:bmap does not recover at [%ld] lblk of the original file!!!\n", __func__, __LINE__, i);
			return -1;
		}
	}
	kfree(tmp);
	kfree(tmp2);
	printk("lwg:%s:%d:sanity checked passed for lblk %lu + %lud\n", __func__, __LINE__, start, cnt);
	return 0;
}


/* must be done _after_ shuffling */
static int write_shuffled_data_blocks(struct file *src, struct file *dst, unsigned long *bmap, unsigned long cnt, unsigned int blkbits) {
	unsigned long i;
	char *tmp = kmalloc(1 << blkbits, GFP_KERNEL);
	ssize_t ret = -EBADF;
	/* lets assume file is allocated to at least two logical block */
	WARN_ON(cnt == 0);
	for (i = 0; i < cnt; i++) {
		loff_t pos = i << blkbits;
		loff_t dst_pos = bmap[i] << blkbits;
		size_t count = 1 << blkbits;
		ret = vfs_read(src, tmp,  count, &pos);
		if (ret <= 0) {
			printk("lwg:%s:%d:reading data block goes wrong! i = %ld, count = %ld, RET = %ld, bits = %d\n", __func__, __LINE__, i, count, ret, blkbits);
			goto complete;
		}
		ret = vfs_write(dst, tmp, count, &dst_pos);
		if (ret <= 0) {
			printk("lwg:%s:%d:writing sybil file goes wrong...\n", __func__, __LINE__);
			goto complete;
		}
	}
complete:
	/* Should I write back to the original file? */
	kfree(tmp);
	return ret;

}


static int match_enigma_file(const void *p, struct file *f, unsigned n) {
	char *to_match = (char *)p;
	char *file_name = f->f_path.dentry->d_name.name;
	to_match = strrchr(to_match, '/');
	to_match += 1;
	printk("lwg:%s:%d:trying to match %s and %s\n", __func__, __LINE__, to_match, file_name);
	if (!strcmp(to_match, file_name)) {
		return n;
	}
	return 0;
}

static struct file *get_current_matching_file(char *filename) {
	int ret;
	struct files_struct *files = get_files_struct(current);
	struct file *f = NULL;
	ret = iterate_fd(files, 0, match_enigma_file, filename);
	if (ret) {
		f = fcheck(ret);
	}
	put_files_struct(files);
	return f;
}


static int enigma_data_block_shuffling(char *filename) {
	/* printk("lwg:%s;%d:caller %s\n", __func__, __LINE__, current->comm); */
	/* struct file *f = filp_open(filename, O_RDWR, 0); */
	struct file *f = get_current_matching_file(filename);
	if (!f) {
		printk("lwg:%s:%d:fail to open the file %s...\n", __func__, __LINE__, filename);
		return 0;
	}
	/* just to be  careful */
	vfs_fsync(f, 0);
	printk("lwg:%s:%d:caller %s, found matching file...\n", __func__, __LINE__, current->comm);
	struct inode *ino = file_inode(f);
	if (!ino) {
		printk("lwg:%s:%d:%s does not have inode??...\n", __func__, __LINE__, filename);
		return 0;
	}
	/* lwg: data shuffling performed on VFS level */
	/* get logical block number */
	unsigned long lblks = get_lblk_cnt(ino);

	if (lblks > 0) {
		/* lwg:XXX: let's hope the stack is big enough */
		unsigned long *bmap;
		/* unsigned long *bmap[lblks]; */
		unsigned long i;
		int ret;
		struct file *new_f;
		char *new_fname;
		/* init the array */
		bmap = (unsigned long *)kmalloc(lblks * sizeof(unsigned long), GFP_KERNEL);
		for (i = 0; i < lblks; i++) {
			bmap[i] = i;
		}
		/* we donot want to shuffle the last block */
		ret = shuffle_bmap(bmap, lblks - 1);
		printk("lwg:%s:%d:[%s] has %lu logical blocks, bits = %d, size = %ld\n", __func__, __LINE__, filename, lblks, ino->i_blkbits, ino->i_size);
		if (!sanity_check_bmap(bmap, lblks)) {
			printk("lwg:%s:%d:warning!!! bmap may go wrong!\n", __func__, __LINE__);
		}
		/* shuffle ALL but the last logical block */
		new_fname = strcat(filename, "-shuffled");
		new_f = filp_open(new_fname, O_RDWR | O_CREAT, 0);
		if (!IS_ERR(new_f)) {
			/* make sure read from most recent version */
			ret = invalidate_mapping_pages(f->f_mapping, 0, -1);
			printk("lwg:%s:%d:cleared %d pages of orig file pagecache!\n", __func__, __LINE__, ret);
			write_shuffled_data_blocks(f, new_f, bmap, lblks, ino->i_blkbits);
			vfs_fsync(new_f, 0);
			ret = sanity_check_shuffled_blocks(f, bmap, new_f, 0, lblks, ino->i_blkbits);
			/* setting up shuffled f for current process */
			f->s.bmap = bmap;
			f->s._f	  = new_f;
			printk("lwg:%s:%d:shuffled file and bmap created!\n", __func__, __LINE__);
			ret = invalidate_mapping_pages(new_f->f_mapping, 0, -1);
			printk("lwg:%s:%d:cleared %d pages of shuffled file pagecache!\n", __func__, __LINE__, ret);
		} else {
			printk("lwg:%s:%d:unable to create shuffled file!\n", __func__, __LINE__);
		}

	} else {
		printk("lwg:%s:%d:whats wrong????", __func__, __LINE__);
	}
	return 0;
}

static unsigned long enigma_clear_file_cache(char *filename) {
	unsigned long ret;
	struct file *f = get_current_matching_file(filename);
	return invalidate_mapping_pages(f->f_mapping, 0, -1);
}

static ssize_t enigma_ctrl_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
	int ret;
	char tmp[128];
	char str[64];
	int k= 0;
	struct timespec start, end;
	if (copy_from_user(tmp, buf, count)) {
		return -EFAULT;
	}
	ret = sscanf(tmp, "%d %s\n", &k, str);
	if (ret == 2) { /* matched two, data shuffling */
		if (k == 0) {
			printk("lwg:%s:shuffling data blocks for %s...\n", __func__, str);
			/* data block shuffling .... */
			getnstimeofday(&start);
			enigma_data_block_shuffling(str);
			getnstimeofday(&end);
			printk("lwg:%s:%d:shuffling takes %ld ms\n", __func__, __LINE__, (end.tv_sec - start.tv_sec)*1000 + (end.tv_nsec - start.tv_nsec)/1000000);
		} else if (k == 1) {
			printk("lwg:%s:clear page cache for %s...\n", __func__, str);
			enigma_clear_file_cache(str);
		}
	} else if (ret == 1) {
		enigma_k = k;
		printk("lwg:%s:Adjusting K to %d...\n", __func__, enigma_k);
	}
	return count;
}


static int enigma_ctrl_show(struct seq_file *m, void *v) {

	seq_printf(m, "----- Helper of Enigma Ctrl -----\n");
	seq_printf(m, "0 [filename]: shuffling file data blocks of [filename]\n");
	seq_printf(m, "1 [CURR_K]  : adjusting curr K to CURR_K\n");
	return 0;
}


static int enigma_ctrl_open(struct inode *inode, struct file *file) {
	return single_open(file, enigma_ctrl_show, NULL);
}



const struct file_operations proc_mounts_operations = {
	.open		= mounts_open,
	.read		= seq_read,
	.write		= mounts_write, /* lwg: XXX used to adjust K */
	.llseek		= seq_lseek,
	.release	= mounts_release,
	.poll		= mounts_poll,
};


const struct file_operations proc_enigma_ctrl_operations = {
	.open		= enigma_ctrl_open,
	.write		= enigma_ctrl_write, /* lwg: XXX used to adjust K */
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};



const struct file_operations proc_mountinfo_operations = {
	.open		= mountinfo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= mounts_release,
	.poll		= mounts_poll,
};

const struct file_operations proc_mountstats_operations = {
	.open		= mountstats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= mounts_release,
};
