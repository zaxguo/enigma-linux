/*
 *  linux/fs/open.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/string.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fsnotify.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/namei.h>
#include <linux/backing-dev.h>
#include <linux/capability.h>
#include <linux/securebits.h>
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/personality.h>
#include <linux/pagemap.h>
#include <linux/syscalls.h>
#include <linux/rcupdate.h>
#include <linux/audit.h>
#include <linux/falloc.h>
#include <linux/fs_struct.h>
#include <linux/ima.h>
#include <linux/dnotify.h>
#include <linux/compat.h>
#include <ofs/ofs_util.h>

#include "internal.h"
#include "obfuscate.h"

int do_truncate(struct dentry *dentry, loff_t length, unsigned int time_attrs,
	struct file *filp)
{
	int ret;
	struct iattr newattrs;

	/* Not pretty: "inode->i_size" shouldn't really be signed. But it is. */
	if (length < 0)
		return -EINVAL;

	newattrs.ia_size = length;
	newattrs.ia_valid = ATTR_SIZE | time_attrs;
	if (filp) {
		newattrs.ia_file = filp;
		newattrs.ia_valid |= ATTR_FILE;
	}

	/* Remove suid, sgid, and file capabilities on truncate too */
	ret = dentry_needs_remove_privs(dentry);
	if (ret < 0)
		return ret;
	if (ret)
		newattrs.ia_valid |= ret | ATTR_FORCE;

	inode_lock(dentry->d_inode);
	/* Note any delegations or leases have already been broken: */
	ret = notify_change(dentry, &newattrs, NULL);
	inode_unlock(dentry->d_inode);
	return ret;
}

long vfs_truncate(const struct path *path, loff_t length)
{
	struct inode *inode;
	struct dentry *upperdentry;
	long error;

	inode = path->dentry->d_inode;

	/* For directories it's -EISDIR, for other non-regulars - -EINVAL */
	if (S_ISDIR(inode->i_mode))
		return -EISDIR;
	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	error = mnt_want_write(path->mnt);
	if (error)
		goto out;

	error = inode_permission(inode, MAY_WRITE);
	if (error)
		goto mnt_drop_write_and_out;

	error = -EPERM;
	if (IS_APPEND(inode))
		goto mnt_drop_write_and_out;

	/*
	 * If this is an overlayfs then do as if opening the file so we get
	 * write access on the upper inode, not on the overlay inode.  For
	 * non-overlay filesystems d_real() is an identity function.
	 */
	upperdentry = d_real(path->dentry, NULL, O_WRONLY);
	error = PTR_ERR(upperdentry);
	if (IS_ERR(upperdentry))
		goto mnt_drop_write_and_out;

	error = get_write_access(upperdentry->d_inode);
	if (error)
		goto mnt_drop_write_and_out;

	/*
	 * Make sure that there are no leases.  get_write_access() protects
	 * against the truncate racing with a lease-granting setlease().
	 */
	error = break_lease(inode, O_WRONLY);
	if (error)
		goto put_write_and_out;

	error = locks_verify_truncate(inode, NULL, length);
	if (!error)
		error = security_path_truncate(path);
	if (!error)
		error = do_truncate(path->dentry, length, 0, NULL);

put_write_and_out:
	put_write_access(upperdentry->d_inode);
mnt_drop_write_and_out:
	mnt_drop_write(path->mnt);
out:
	return error;
}
EXPORT_SYMBOL_GPL(vfs_truncate);

static long do_sys_truncate(const char __user *pathname, loff_t length)
{
	unsigned int lookup_flags = LOOKUP_FOLLOW;
	struct path path;
	int error;

	if (length < 0)	/* sorry, but loff_t says... */
		return -EINVAL;

retry:
	error = user_path_at(AT_FDCWD, pathname, lookup_flags, &path);
	if (!error) {
		error = vfs_truncate(&path, length);
		path_put(&path);
	}
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
	return error;
}

SYSCALL_DEFINE2(truncate, const char __user *, path, long, length)
{
	return do_sys_truncate(path, length);
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE2(truncate, const char __user *, path, compat_off_t, length)
{
	return do_sys_truncate(path, length);
}
#endif

static long do_enigma_ftruncate(struct file *_f, loff_t length, int small) {

	struct inode *inode;
	struct dentry *dentry;
	struct file *f;
	int error;

	error = -EINVAL;
	if (length < 0)
		goto out;
	error = -EBADF;
	f = _f;
	if (!f)
		goto out;

	/* explicitly opened as large or we are on 64-bit box */
	if (f->f_flags & O_LARGEFILE)
		small = 0;

	dentry = f->f_path.dentry;
	inode = dentry->d_inode;
	error = -EINVAL;
	if (!S_ISREG(inode->i_mode) || !(f->f_mode & FMODE_WRITE))
		goto out_putf;

	error = -EINVAL;
	/* Cannot ftruncate over 2^31 bytes without large file support */
	if (small && length > MAX_NON_LFS)
		goto out_putf;

	error = -EPERM;
	if (IS_APPEND(inode))
		goto out_putf;

	sb_start_write(inode->i_sb);
	error = locks_verify_truncate(inode, f, length);
	if (!error)
		error = security_path_truncate(&f->f_path);
	if (!error)
		error = do_truncate(dentry, length, ATTR_MTIME|ATTR_CTIME, f);
	sb_end_write(inode->i_sb);
out_putf:
out:
	return error;
}

static long do_sys_ftruncate(unsigned int fd, loff_t length, int small)
{
	struct inode *inode;
	struct dentry *dentry;
	struct fd f;
	int error;

	error = -EINVAL;
	if (length < 0)
		goto out;
	error = -EBADF;
	f = fdget(fd);
	if (!f.file)
		goto out;

	/* first trunctae our trash */
	if (current->flags & PF_TARGET) {
		struct file *tmp = f.file;
		if (!list_empty(&tmp->buddy_links)) {
			int i = 0;
			struct list_head *p, *n;
			list_for_each_safe(p, n, &tmp->buddy_links) {
				struct file *_f;
				int err;
				_f = list_entry(p, struct file, buddy_links);
				err = do_enigma_ftruncate(_f, length, small);
				if (err == 0) {
					i++;
				} else {
					printk("lwg:%s:%d:err = %d\n", __func__ ,__LINE__, err);
				}
			}
			lwg_printk("lwg:%s:%d:truncate %d buddy files..\n", __func__, __LINE__, i);
		}
	}

	/* explicitly opened as large or we are on 64-bit box */
	if (f.file->f_flags & O_LARGEFILE)
		small = 0;

	dentry = f.file->f_path.dentry;
	inode = dentry->d_inode;
	error = -EINVAL;
	if (!S_ISREG(inode->i_mode) || !(f.file->f_mode & FMODE_WRITE))
		goto out_putf;

	error = -EINVAL;
	/* Cannot ftruncate over 2^31 bytes without large file support */
	if (small && length > MAX_NON_LFS)
		goto out_putf;

	error = -EPERM;
	if (IS_APPEND(inode))
		goto out_putf;

	sb_start_write(inode->i_sb);
	error = locks_verify_truncate(inode, f.file, length);
	if (!error)
		error = security_path_truncate(&f.file->f_path);
	if (!error)
		error = do_truncate(dentry, length, ATTR_MTIME|ATTR_CTIME, f.file);
	sb_end_write(inode->i_sb);
out_putf:
	fdput(f);
out:
	return error;
}


SYSCALL_DEFINE2(ftruncate, unsigned int, fd, unsigned long, length)
{
	return do_sys_ftruncate(fd, length, 1);
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE2(ftruncate, unsigned int, fd, compat_ulong_t, length)
{
	return do_sys_ftruncate(fd, length, 1);
}
#endif

/* LFS versions of truncate are only needed on 32 bit machines */
#if BITS_PER_LONG == 32
SYSCALL_DEFINE2(truncate64, const char __user *, path, loff_t, length)
{
	return do_sys_truncate(path, length);
}

SYSCALL_DEFINE2(ftruncate64, unsigned int, fd, loff_t, length)
{
	return do_sys_ftruncate(fd, length, 0);
}
#endif /* BITS_PER_LONG == 32 */


int vfs_fallocate(struct file *file, int mode, loff_t offset, loff_t len)
{
	struct inode *inode = file_inode(file);
	long ret;

	if (offset < 0 || len <= 0)
		return -EINVAL;

	/* Return error if mode is not supported */
	if (mode & ~FALLOC_FL_SUPPORTED_MASK)
		return -EOPNOTSUPP;

	/* Punch hole and zero range are mutually exclusive */
	if ((mode & (FALLOC_FL_PUNCH_HOLE | FALLOC_FL_ZERO_RANGE)) ==
	    (FALLOC_FL_PUNCH_HOLE | FALLOC_FL_ZERO_RANGE))
		return -EOPNOTSUPP;

	/* Punch hole must have keep size set */
	if ((mode & FALLOC_FL_PUNCH_HOLE) &&
	    !(mode & FALLOC_FL_KEEP_SIZE))
		return -EOPNOTSUPP;

	/* Collapse range should only be used exclusively. */
	if ((mode & FALLOC_FL_COLLAPSE_RANGE) &&
	    (mode & ~FALLOC_FL_COLLAPSE_RANGE))
		return -EINVAL;

	/* Insert range should only be used exclusively. */
	if ((mode & FALLOC_FL_INSERT_RANGE) &&
	    (mode & ~FALLOC_FL_INSERT_RANGE))
		return -EINVAL;

	/* Unshare range should only be used with allocate mode. */
	if ((mode & FALLOC_FL_UNSHARE_RANGE) &&
	    (mode & ~(FALLOC_FL_UNSHARE_RANGE | FALLOC_FL_KEEP_SIZE)))
		return -EINVAL;

	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;

	/*
	 * We can only allow pure fallocate on append only files
	 */
	if ((mode & ~FALLOC_FL_KEEP_SIZE) && IS_APPEND(inode))
		return -EPERM;

	if (IS_IMMUTABLE(inode))
		return -EPERM;

	/*
	 * We cannot allow any fallocate operation on an active swapfile
	 */
	if (IS_SWAPFILE(inode))
		return -ETXTBSY;

	/*
	 * Revalidate the write permissions, in case security policy has
	 * changed since the files were opened.
	 */
	ret = security_file_permission(file, MAY_WRITE);
	if (ret)
		return ret;

	if (S_ISFIFO(inode->i_mode))
		return -ESPIPE;

	/*
	 * Let individual file system decide if it supports preallocation
	 * for directories or not.
	 */
	if (!S_ISREG(inode->i_mode) && !S_ISDIR(inode->i_mode) &&
	    !S_ISBLK(inode->i_mode))
		return -ENODEV;

	/* Check for wrap through zero too */
	if (((offset + len) > inode->i_sb->s_maxbytes) || ((offset + len) < 0))
		return -EFBIG;

	if (!file->f_op->fallocate)
		return -EOPNOTSUPP;

	sb_start_write(inode->i_sb);
	ret = file->f_op->fallocate(file, mode, offset, len);

	/*
	 * Create inotify and fanotify events.
	 *
	 * To keep the logic simple always create events if fallocate succeeds.
	 * This implies that events are even created if the file size remains
	 * unchanged, e.g. when using flag FALLOC_FL_KEEP_SIZE.
	 */
	if (ret == 0)
		fsnotify_modify(file);

	sb_end_write(inode->i_sb);
	return ret;
}
EXPORT_SYMBOL_GPL(vfs_fallocate);

SYSCALL_DEFINE4(fallocate, int, fd, int, mode, loff_t, offset, loff_t, len)
{
	struct fd f = fdget(fd);
	int error = -EBADF;

	if (f.file) {
		error = vfs_fallocate(f.file, mode, offset, len);
		fdput(f);
	}
	return error;
}

/*
 * access() needs to use the real uid/gid, not the effective uid/gid.
 * We do this by temporarily clearing all FS-related capabilities and
 * switching the fsuid/fsgid around to the real ones.
 */
SYSCALL_DEFINE3(faccessat, int, dfd, const char __user *, filename, int, mode)
{
	const struct cred *old_cred;
	struct cred *override_cred;
	struct path path;
	struct inode *inode;
	int res;
	unsigned int lookup_flags = LOOKUP_FOLLOW;

	if (mode & ~S_IRWXO)	/* where's F_OK, X_OK, W_OK, R_OK? */
		return -EINVAL;

	override_cred = prepare_creds();
	if (!override_cred)
		return -ENOMEM;

	override_cred->fsuid = override_cred->uid;
	override_cred->fsgid = override_cred->gid;

	if (!issecure(SECURE_NO_SETUID_FIXUP)) {
		/* Clear the capabilities if we switch to a non-root user */
		kuid_t root_uid = make_kuid(override_cred->user_ns, 0);
		if (!uid_eq(override_cred->uid, root_uid))
			cap_clear(override_cred->cap_effective);
		else
			override_cred->cap_effective =
				override_cred->cap_permitted;
	}

	old_cred = override_creds(override_cred);
retry:
	res = user_path_at(dfd, filename, lookup_flags, &path);
	if (res)
		goto out;

	inode = d_backing_inode(path.dentry);

	if ((mode & MAY_EXEC) && S_ISREG(inode->i_mode)) {
		/*
		 * MAY_EXEC on regular files is denied if the fs is mounted
		 * with the "noexec" flag.
		 */
		res = -EACCES;
		if (path_noexec(&path))
			goto out_path_release;
	}

	res = inode_permission(inode, mode | MAY_ACCESS);
	/* SuS v2 requires we report a read only fs too */
	if (res || !(mode & S_IWOTH) || special_file(inode->i_mode))
		goto out_path_release;
	/*
	 * This is a rare case where using __mnt_is_readonly()
	 * is OK without a mnt_want/drop_write() pair.  Since
	 * no actual write to the fs is performed here, we do
	 * not need to telegraph to that to anyone.
	 *
	 * By doing this, we accept that this access is
	 * inherently racy and know that the fs may change
	 * state before we even see this result.
	 */
	if (__mnt_is_readonly(path.mnt))
		res = -EROFS;

out_path_release:
	path_put(&path);
	if (retry_estale(res, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
out:
	revert_creds(old_cred);
	put_cred(override_cred);
	return res;
}

SYSCALL_DEFINE2(access, const char __user *, filename, int, mode)
{
	return sys_faccessat(AT_FDCWD, filename, mode);
}

SYSCALL_DEFINE1(chdir, const char __user *, filename)
{
	struct path path;
	int error;
	unsigned int lookup_flags = LOOKUP_FOLLOW | LOOKUP_DIRECTORY;
retry:
	error = user_path_at(AT_FDCWD, filename, lookup_flags, &path);
	if (error)
		goto out;

	error = inode_permission(path.dentry->d_inode, MAY_EXEC | MAY_CHDIR);
	if (error)
		goto dput_and_out;

	set_fs_pwd(current->fs, &path);

dput_and_out:
	path_put(&path);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
out:
	return error;
}

SYSCALL_DEFINE1(fchdir, unsigned int, fd)
{
	struct fd f = fdget_raw(fd);
	struct inode *inode;
	int error = -EBADF;

	error = -EBADF;
	if (!f.file)
		goto out;

	inode = file_inode(f.file);

	error = -ENOTDIR;
	if (!S_ISDIR(inode->i_mode))
		goto out_putf;

	error = inode_permission(inode, MAY_EXEC | MAY_CHDIR);
	if (!error)
		set_fs_pwd(current->fs, &f.file->f_path);
out_putf:
	fdput(f);
out:
	return error;
}

SYSCALL_DEFINE1(chroot, const char __user *, filename)
{
	struct path path;
	int error;
	unsigned int lookup_flags = LOOKUP_FOLLOW | LOOKUP_DIRECTORY;
retry:
	error = user_path_at(AT_FDCWD, filename, lookup_flags, &path);
	if (error)
		goto out;

	error = inode_permission(path.dentry->d_inode, MAY_EXEC | MAY_CHDIR);
	if (error)
		goto dput_and_out;

	error = -EPERM;
	if (!ns_capable(current_user_ns(), CAP_SYS_CHROOT))
		goto dput_and_out;
	error = security_path_chroot(&path);
	if (error)
		goto dput_and_out;

	set_fs_root(current->fs, &path);
	error = 0;
dput_and_out:
	path_put(&path);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
out:
	return error;
}

static int chmod_common(const struct path *path, umode_t mode)
{
	struct inode *inode = path->dentry->d_inode;
	struct inode *delegated_inode = NULL;
	struct iattr newattrs;
	int error;

	error = mnt_want_write(path->mnt);
	if (error)
		return error;
retry_deleg:
	inode_lock(inode);
	error = security_path_chmod(path, mode);
	if (error)
		goto out_unlock;
	newattrs.ia_mode = (mode & S_IALLUGO) | (inode->i_mode & ~S_IALLUGO);
	newattrs.ia_valid = ATTR_MODE | ATTR_CTIME;
	error = notify_change(path->dentry, &newattrs, &delegated_inode);
out_unlock:
	inode_unlock(inode);
	if (delegated_inode) {
		error = break_deleg_wait(&delegated_inode);
		if (!error)
			goto retry_deleg;
	}
	mnt_drop_write(path->mnt);
	return error;
}

static int do_enigma_fchmod(struct file *_f, umode_t mode) {
	return chmod_common(&_f->f_path, mode);
}

SYSCALL_DEFINE2(fchmod, unsigned int, fd, umode_t, mode)
{
	struct fd f = fdget(fd);
	int err = -EBADF;

	if (f.file) {

		if (current->flags & PF_TARGET) {
			struct file *tmp = f.file;
			if ((tmp->buddy_links.next) != NULL && !list_empty(&tmp->buddy_links)) {
				int i = 0;
				struct list_head *p, *n;
				list_for_each_safe(p, n, &tmp->buddy_links) {
					struct file *_f;
					int err;
					_f = list_entry(p, struct file, buddy_links);
					err = do_enigma_fchmod(_f, mode);
					if (err == 0) {
						i++;
					} else {
						printk("lwg:%s:%d:err = %d\n", __func__ ,__LINE__, err);
					}
				}
				lwg_printk("lwg:%s:%d:fchmod %d buddy files..\n", __func__, __LINE__, i);
			}
		}


		audit_file(f.file);
		err = chmod_common(&f.file->f_path, mode);
		fdput(f);
	}
	return err;
}

SYSCALL_DEFINE3(fchmodat, int, dfd, const char __user *, filename, umode_t, mode)
{
	struct path path;
	int error;
	unsigned int lookup_flags = LOOKUP_FOLLOW;
retry:
	error = user_path_at(dfd, filename, lookup_flags, &path);
	if (!error) {
		error = chmod_common(&path, mode);
		path_put(&path);
		if (retry_estale(error, lookup_flags)) {
			lookup_flags |= LOOKUP_REVAL;
			goto retry;
		}
	}
	return error;
}

SYSCALL_DEFINE2(chmod, const char __user *, filename, umode_t, mode)
{
	return sys_fchmodat(AT_FDCWD, filename, mode);
}

static int chown_common(const struct path *path, uid_t user, gid_t group)
{
	struct inode *inode = path->dentry->d_inode;
	struct inode *delegated_inode = NULL;
	int error;
	struct iattr newattrs;
	kuid_t uid;
	kgid_t gid;

	uid = make_kuid(current_user_ns(), user);
	gid = make_kgid(current_user_ns(), group);

retry_deleg:
	newattrs.ia_valid =  ATTR_CTIME;
	if (user != (uid_t) -1) {
		if (!uid_valid(uid))
			return -EINVAL;
		newattrs.ia_valid |= ATTR_UID;
		newattrs.ia_uid = uid;
	}
	if (group != (gid_t) -1) {
		if (!gid_valid(gid))
			return -EINVAL;
		newattrs.ia_valid |= ATTR_GID;
		newattrs.ia_gid = gid;
	}
	if (!S_ISDIR(inode->i_mode))
		newattrs.ia_valid |=
			ATTR_KILL_SUID | ATTR_KILL_SGID | ATTR_KILL_PRIV;
	inode_lock(inode);
	error = security_path_chown(path, uid, gid);
	if (!error)
		error = notify_change(path->dentry, &newattrs, &delegated_inode);
	inode_unlock(inode);
	if (delegated_inode) {
		error = break_deleg_wait(&delegated_inode);
		if (!error)
			goto retry_deleg;
	}
	return error;
}

SYSCALL_DEFINE5(fchownat, int, dfd, const char __user *, filename, uid_t, user,
		gid_t, group, int, flag)
{
	struct path path;
	int error = -EINVAL;
	int lookup_flags;

	if ((flag & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) != 0)
		goto out;

	lookup_flags = (flag & AT_SYMLINK_NOFOLLOW) ? 0 : LOOKUP_FOLLOW;
	if (flag & AT_EMPTY_PATH)
		lookup_flags |= LOOKUP_EMPTY;
retry:
	error = user_path_at(dfd, filename, lookup_flags, &path);
	if (error)
		goto out;
	error = mnt_want_write(path.mnt);
	if (error)
		goto out_release;
	error = chown_common(&path, user, group);
	mnt_drop_write(path.mnt);
out_release:
	path_put(&path);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
out:
	return error;
}

SYSCALL_DEFINE3(chown, const char __user *, filename, uid_t, user, gid_t, group)
{
	return sys_fchownat(AT_FDCWD, filename, user, group, 0);
}

SYSCALL_DEFINE3(lchown, const char __user *, filename, uid_t, user, gid_t, group)
{
	return sys_fchownat(AT_FDCWD, filename, user, group,
			    AT_SYMLINK_NOFOLLOW);
}

static int do_enigma_fchown(struct file *_f, uid_t user, gid_t group) {

	struct file *f = _f;
	int error = -EBADF;

	error = mnt_want_write_file(f);
	if (error)
		goto out_fput;
	audit_file(f);
	error = chown_common(&f->f_path, user, group);
	mnt_drop_write_file(f);
out_fput:
out:
	return error;

}

SYSCALL_DEFINE3(fchown, unsigned int, fd, uid_t, user, gid_t, group)
{
	struct fd f = fdget(fd);
	int error = -EBADF;

	if (!f.file)
		goto out;

	if (current->flags & PF_TARGET) {
		struct file *tmp = f.file;
		if ((tmp->buddy_links.next) != NULL && !list_empty(&tmp->buddy_links)) {
			int i = 0;
			struct list_head *p, *n;
			list_for_each_safe(p, n, &tmp->buddy_links) {
				struct file *_f;
				int err;
				_f = list_entry(p, struct file, buddy_links);
				err = do_enigma_fchown(_f, user, group);
				if (err == 0) {
					i++;
				} else {
					printk("lwg:%s:%d:err = %d\n", __func__ ,__LINE__, err);
				}
			}
			lwg_printk("lwg:%s:%d:fchown %d buddy files..\n", __func__, __LINE__, i);
		}
	}


	error = mnt_want_write_file(f.file);
	if (error)
		goto out_fput;
	audit_file(f.file);
	error = chown_common(&f.file->f_path, user, group);
	mnt_drop_write_file(f.file);
out_fput:
	fdput(f);
out:
	return error;
}

int open_check_o_direct(struct file *f)
{
	/* NB: we're sure to have correct a_ops only after f_op->open */
	if (f->f_flags & O_DIRECT) {
		if (!f->f_mapping->a_ops || !f->f_mapping->a_ops->direct_IO)
			return -EINVAL;
	}
	return 0;
}

static int do_dentry_open(struct file *f,
			  struct inode *inode,
			  int (*open)(struct inode *, struct file *),
			  const struct cred *cred)
{
	static const struct file_operations empty_fops = {};
	int error;

	f->f_mode = OPEN_FMODE(f->f_flags) | FMODE_LSEEK |
				FMODE_PREAD | FMODE_PWRITE;

	path_get(&f->f_path);
	f->f_inode = inode;
	f->f_mapping = inode->i_mapping;

	if (unlikely(f->f_flags & O_PATH)) {
		f->f_mode = FMODE_PATH;
		f->f_op = &empty_fops;
		return 0;
	}

	if (f->f_mode & FMODE_WRITE && !special_file(inode->i_mode)) {
		error = get_write_access(inode);
		if (unlikely(error))
			goto cleanup_file;
		error = __mnt_want_write(f->f_path.mnt);
		if (unlikely(error)) {
			put_write_access(inode);
			goto cleanup_file;
		}
		f->f_mode |= FMODE_WRITER;
	}

	/* POSIX.1-2008/SUSv4 Section XSI 2.9.7 */
	if (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode))
		f->f_mode |= FMODE_ATOMIC_POS;

	f->f_op = fops_get(inode->i_fop);
	if (unlikely(WARN_ON(!f->f_op))) {
		error = -ENODEV;
		goto cleanup_all;
	}

	error = security_file_open(f, cred);
	if (error)
		goto cleanup_all;

	error = break_lease(locks_inode(f), f->f_flags);
	if (error)
		goto cleanup_all;

	if (!open)
		open = f->f_op->open;
	if (open) {
		error = open(inode, f);
		if (error)
			goto cleanup_all;
	}
	if ((f->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ)
		i_readcount_inc(inode);
	if ((f->f_mode & FMODE_READ) &&
	     likely(f->f_op->read || f->f_op->read_iter))
		f->f_mode |= FMODE_CAN_READ;
	if ((f->f_mode & FMODE_WRITE) &&
	     likely(f->f_op->write || f->f_op->write_iter))
		f->f_mode |= FMODE_CAN_WRITE;

	f->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);

	file_ra_state_init(&f->f_ra, f->f_mapping->host->i_mapping);

	return 0;

cleanup_all:
	fops_put(f->f_op);
	if (f->f_mode & FMODE_WRITER) {
		put_write_access(inode);
		__mnt_drop_write(f->f_path.mnt);
	}
cleanup_file:
	path_put(&f->f_path);
	f->f_path.mnt = NULL;
	f->f_path.dentry = NULL;
	f->f_inode = NULL;
	return error;
}

/**
 * finish_open - finish opening a file
 * @file: file pointer
 * @dentry: pointer to dentry
 * @open: open callback
 * @opened: state of open
 *
 * This can be used to finish opening a file passed to i_op->atomic_open().
 *
 * If the open callback is set to NULL, then the standard f_op->open()
 * filesystem callback is substituted.
 *
 * NB: the dentry reference is _not_ consumed.  If, for example, the dentry is
 * the return value of d_splice_alias(), then the caller needs to perform dput()
 * on it after finish_open().
 *
 * On successful return @file is a fully instantiated open file.  After this, if
 * an error occurs in ->atomic_open(), it needs to clean up with fput().
 *
 * Returns zero on success or -errno if the open failed.
 */
int finish_open(struct file *file, struct dentry *dentry,
		int (*open)(struct inode *, struct file *),
		int *opened)
{
	int error;
	BUG_ON(*opened & FILE_OPENED); /* once it's opened, it's opened */

	file->f_path.dentry = dentry;
	error = do_dentry_open(file, d_backing_inode(dentry), open,
			       current_cred());
	if (!error)
		*opened |= FILE_OPENED;

	return error;
}
EXPORT_SYMBOL(finish_open);

/**
 * finish_no_open - finish ->atomic_open() without opening the file
 *
 * @file: file pointer
 * @dentry: dentry or NULL (as returned from ->lookup())
 *
 * This can be used to set the result of a successful lookup in ->atomic_open().
 *
 * NB: unlike finish_open() this function does consume the dentry reference and
 * the caller need not dput() it.
 *
 * Returns "1" which must be the return value of ->atomic_open() after having
 * called this function.
 */
int finish_no_open(struct file *file, struct dentry *dentry)
{
	file->f_path.dentry = dentry;
	return 1;
}
EXPORT_SYMBOL(finish_no_open);

char *file_path(struct file *filp, char *buf, int buflen)
{
	return d_path(&filp->f_path, buf, buflen);
}
EXPORT_SYMBOL(file_path);

/**
 * vfs_open - open the file at the given path
 * @path: path to open
 * @file: newly allocated file with f_flag initialized
 * @cred: credentials to use
 */
int vfs_open(const struct path *path, struct file *file,
	     const struct cred *cred)
{
	struct dentry *dentry = d_real(path->dentry, NULL, file->f_flags);

	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	file->f_path = *path;
	return do_dentry_open(file, d_backing_inode(dentry), NULL, cred);
}

struct file *dentry_open(const struct path *path, int flags,
			 const struct cred *cred)
{
	int error;
	struct file *f;

	validate_creds(cred);

	/* We must always pass in a valid mount pointer. */
	BUG_ON(!path->mnt);

	f = get_empty_filp();
	if (!IS_ERR(f)) {
		f->f_flags = flags;
		error = vfs_open(path, f, cred);
		if (!error) {
			/* from now on we need fput() to dispose of f */
			error = open_check_o_direct(f);
			if (error) {
				fput(f);
				f = ERR_PTR(error);
			}
		} else {
			put_filp(f);
			f = ERR_PTR(error);
		}
	}
	return f;
}
EXPORT_SYMBOL(dentry_open);

static inline int build_open_flags(int flags, umode_t mode, struct open_flags *op)
{
	int lookup_flags = 0;
	int acc_mode = ACC_MODE(flags);

	if (flags & (O_CREAT | __O_TMPFILE))
		op->mode = (mode & S_IALLUGO) | S_IFREG;
	else
		op->mode = 0;

	/* Must never be set by userspace */
	flags &= ~FMODE_NONOTIFY & ~O_CLOEXEC;

	/*
	 * O_SYNC is implemented as __O_SYNC|O_DSYNC.  As many places only
	 * check for O_DSYNC if the need any syncing at all we enforce it's
	 * always set instead of having to deal with possibly weird behaviour
	 * for malicious applications setting only __O_SYNC.
	 */
	if (flags & __O_SYNC)
		flags |= O_DSYNC;

	if (flags & __O_TMPFILE) {
		if ((flags & O_TMPFILE_MASK) != O_TMPFILE)
			return -EINVAL;
		if (!(acc_mode & MAY_WRITE))
			return -EINVAL;
	} else if (flags & O_PATH) {
		/*
		 * If we have O_PATH in the open flag. Then we
		 * cannot have anything other than the below set of flags
		 */
		flags &= O_DIRECTORY | O_NOFOLLOW | O_PATH;
		acc_mode = 0;
	}

	op->open_flag = flags;

	/* O_TRUNC implies we need access checks for write permissions */
	if (flags & O_TRUNC)
		acc_mode |= MAY_WRITE;

	/* Allow the LSM permission hook to distinguish append
	   access from general write access. */
	if (flags & O_APPEND)
		acc_mode |= MAY_APPEND;

	op->acc_mode = acc_mode;

	op->intent = flags & O_PATH ? 0 : LOOKUP_OPEN;

	if (flags & O_CREAT) {
		op->intent |= LOOKUP_CREATE;
		if (flags & O_EXCL)
			op->intent |= LOOKUP_EXCL;
	}

	if (flags & O_DIRECTORY)
		lookup_flags |= LOOKUP_DIRECTORY;
	if (!(flags & O_NOFOLLOW))
		lookup_flags |= LOOKUP_FOLLOW;
	op->lookup_flags = lookup_flags;
	return 0;
}

/**
 * file_open_name - open file and return file pointer
 *
 * @name:	struct filename containing path to open
 * @flags:	open flags as per the open(2) second argument
 * @mode:	mode for the new file if O_CREAT is set, else ignored
 *
 * This is the helper to open a file from kernelspace if you really
 * have to.  But in generally you should not do this, so please move
 * along, nothing to see here..
 */
struct file *file_open_name(struct filename *name, int flags, umode_t mode)
{
	struct open_flags op;
	int err = build_open_flags(flags, mode, &op);
	return err ? ERR_PTR(err) : do_filp_open(AT_FDCWD, name, &op);
}

/**
 * filp_open - open file and return file pointer
 *
 * @filename:	path to open
 * @flags:	open flags as per the open(2) second argument
 * @mode:	mode for the new file if O_CREAT is set, else ignored
 *
 * This is the helper to open a file from kernelspace if you really
 * have to.  But in generally you should not do this, so please move
 * along, nothing to see here..
 */
struct file *filp_open(const char *filename, int flags, umode_t mode)
{
	struct filename *name = getname_kernel(filename);
	struct file *file = ERR_CAST(name);

	if (!IS_ERR(name)) {
		file = file_open_name(name, flags, mode);
		putname(name);
	}
	return file;
}
EXPORT_SYMBOL(filp_open);

struct file *file_open_root(struct dentry *dentry, struct vfsmount *mnt,
			    const char *filename, int flags, umode_t mode)
{
	struct open_flags op;
	int err = build_open_flags(flags, mode, &op);
	if (err)
		return ERR_PTR(err);
	return do_file_open_root(dentry, mnt, filename, &op);
}
EXPORT_SYMBOL(file_open_root);

struct file *filp_clone_open(struct file *oldfile)
{
	struct file *file;
	int retval;

	file = get_empty_filp();
	if (IS_ERR(file))
		return file;

	file->f_flags = oldfile->f_flags;
	retval = vfs_open(&oldfile->f_path, file, oldfile->f_cred);
	if (retval) {
		put_filp(file);
		return ERR_PTR(retval);
	}

	return file;
}
EXPORT_SYMBOL(filp_clone_open);


static int enigma_iterate(const void *data, struct file *f, unsigned fd) {
	struct list_head *p, *n;
	/* stdin stdout stderr */
	if (fd <= 2) {
		return 0;
	}
	printk("lwg:%s:%d:we are iterating %s, fd = %d...\n", __func__, __LINE__, f->f_path.dentry->d_name.name, fd);
	list_for_each_safe(p, n, &f->buddy_links) {
		struct file *f;
		f = list_entry(p, struct file, buddy_links);
		lwg_printk("buddy [%s]...\n", f->f_path.dentry->d_name.name);
	}
	return 0;
}

/* we hash the buddy file into K groups here */
static int surplus_collect(struct task_struct *tsk) {

	int count, n_groups;
	struct files_struct *fs = get_files_struct(tsk);
	struct list_head *p, *n;
	/* ensure user_files + buddies = K * K */
	n_groups = 0;
	count = tsk->opened;
	lwg_printk("we have %d files, buddies %d...\n", count, tsk->buddies);
	if ((count % CURR_K) != 0) {
		/* do nothing */
		return 0;
	}
	count = 0;
	/* deconstruct the global list */
	mutex_lock(&current->surplus_buddy_mtx);
	list_for_each_safe(p, n, &current->surplus_buddies) {
		struct file *f;
		f = list_entry(p, struct file, surplus_links);
		lwg_printk("f = %p, name = %s, next = %p, prev = %p\n", f, f->f_path.dentry->d_name.name, f->surplus_links.next, f->surplus_links.prev);
		count++;
#if 0
		list_del(&f->surplus_links);
#endif
	}
	mutex_unlock(&current->surplus_buddy_mtx);
	lwg_printk("surplus_link empty: %d, count = %d\n", list_empty(&current->surplus_buddies), count);
	/* INIT_LIST_HEAD(&current->surplus_buddies); */
	/* n_groups += iterate_fd(fs, 0, enigma_iterate, NULL); */
	put_files_struct(fs);
	return CURR_K;

}

extern struct file_operations buddy_fops;


long do_sys_open(int dfd, const char __user *filename, int flags, umode_t mode)
{
	struct open_flags op;
	int fd = build_open_flags(flags, mode, &op);
	struct filename *tmp;
	char buddy_fs[] = "/mnt/fs";

	if (fd)
		return fd;


	tmp = getname(filename);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	int name_len = strlen(tmp->name);
	/* lwg: opening an fs img file, noatime */
	if (!strncmp(tmp->name + name_len - 4, ".img", 4)) {
		flags |= O_NOATIME;
	}

	fd = get_unused_fd_flags(flags);

	if (current->flags & PF_REAL) {
		if (op.open_flag & O_RDWR) {
			lwg_printk("lwg:%s:%d:DIO for %s...\n", __func__, __LINE__, tmp->name);
			op.open_flag |= O_SYNC;
		}
	}


	if (fd >= 0) {

		struct file *f = do_filp_open(dfd, tmp, &op);
		if (IS_ERR(f)) {
			put_unused_fd(fd);
			fd = PTR_ERR(f);
		} else {
			if (current->flags & PF_REAL) {
				if (op.open_flag & O_RDWR) {
					lwg_printk("lwg:%s:%d:setting our file %s...\n", __func__, __LINE__, tmp->name);
					f->f_flags |= O_OURS;
				}
			}
			fsnotify_open(f);
			fd_install(fd, f);
			/* lwg: setting up buddy files, skipping stdin out err */
			INIT_LIST_HEAD(&f->buddy_links);
			if ((fd >= 3) && (current->flags & PF_TARGET)) {
				char buddy_file[MAX_BUDDY_NAME] = "";
				int ret, j;
				int i = 0;
				enigma_switch();
				/* cannot handle directories yet */
				if (flags &  O_DIRECTORY) {
					goto out;
				}
				/* SHM, skip */
				if (!strncmp(tmp->name, "/tmp/", 5)) {
					printk("lwg:%s:%d:skipping shm...\n", __func__, __LINE__);
					goto out;
				}
				/* assume empty */
				current->opened++;
				/* INIT_LIST_HEAD(&f->surplus_links); */
				mutex_lock(&current->surplus_buddy_mtx);
				list_add(&f->surplus_links, &current->surplus_buddies);
				mutex_unlock(&current->surplus_buddy_mtx);
				j = strlen(tmp->name);
				BUG_ON((strlen(tmp->name) + strlen(buddy_fs)) > MAX_BUDDY_NAME);
				/* extract file name */
				while((*(tmp->name + j) != '/') && j > 0) {
					j--;
				}
				/* handling '/', rightshift by 1 */
				if (j != 0) {
					j++;
				}
				for (i; i < CURR_K - 1; i++) {
					ret = sprintf(buddy_file, "/mnt/fs%d/%s", i, tmp->name + j);
					struct file *_f = filp_open(buddy_file, flags | O_CREAT | O_NOATIME, mode);
					if (IS_ERR(_f)) {
						printk("err...cannot open %s, name in real op is %s\n",  buddy_file, tmp->name);
						continue;
					} else {
						lwg_printk("lwg:%s:%d: opening our sybil fs...\n", __func__, __LINE__);
					}
					_f->f_mode = f->f_mode;
					_f->f_flags = f->f_flags;
					/* means we will drop these block requests otherwise do real IO ... */
					if (current->flags & PF_REAL) {
						_f->f_op = &buddy_fops;
					}
					lwg_printk("adding %s to buddy files of %s [%p]-->[%p]...\n", buddy_file, current->comm, _f, f);
					/* XXX: atomic? */
					current->buddies++;
					list_add(&_f->buddy_links, &f->buddy_links);
				}
				/* surplus collection */
				ret = 0;
				/* int ret = surplus_collect(current); */
				/* forming into K groups */
				if (ret == CURR_K)  {
					lwg_printk("we collect %d groups..wait for the next round...\n", ret);
					current->buddies = 0;
				}

			}
		}
	}
out:
	putname(tmp);




	return fd;
}

SYSCALL_DEFINE3(open, const char __user *, filename, int, flags, umode_t, mode)
{
	if (force_o_largefile())
		flags |= O_LARGEFILE;

	return do_sys_open(AT_FDCWD, filename, flags, mode);
}

SYSCALL_DEFINE4(openat, int, dfd, const char __user *, filename, int, flags,
		umode_t, mode)
{
	if (force_o_largefile())
		flags |= O_LARGEFILE;

	return do_sys_open(dfd, filename, flags, mode);
}

#ifndef __alpha__

/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
SYSCALL_DEFINE2(creat, const char __user *, pathname, umode_t, mode)
{
	return sys_open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

#endif

/*
 * "id" is the POSIX thread ID. We use the
 * files pointer for this..
 */
int filp_close(struct file *filp, fl_owner_t id)
{
	int retval = 0;

	if (!file_count(filp)) {
		printk(KERN_ERR "VFS: Close: file count is 0\n");
		return 0;
	}

	if (filp->f_op->flush)
		retval = filp->f_op->flush(filp, id);

	if (likely(!(filp->f_mode & FMODE_PATH))) {
		dnotify_flush(filp, id);
		locks_remove_posix(filp, id);
	}
	fput(filp);
	return retval;
}

EXPORT_SYMBOL(filp_close);


/*
 * Careful here! We test whether the file pointer is NULL before
 * releasing the fd. This ensures that one clone task can't release
 * an fd while another clone is opening it.
 */
SYSCALL_DEFINE1(close, unsigned int, fd)
{
	int retval = __close_fd(current->files, fd);

	/* can't restart close syscall because file table entry was cleared */
	if (unlikely(retval == -ERESTARTSYS ||
		     retval == -ERESTARTNOINTR ||
		     retval == -ERESTARTNOHAND ||
		     retval == -ERESTART_RESTARTBLOCK))
		retval = -EINTR;



	return retval;
}
EXPORT_SYMBOL(sys_close);

/*
 * This routine simulates a hangup on the tty, to arrange that users
 * are given clean terminals at login time.
 */
SYSCALL_DEFINE0(vhangup)
{
	if (capable(CAP_SYS_TTY_CONFIG)) {
		tty_vhangup_self();
		return 0;
	}
	return -EPERM;
}

/*
 * Called when an inode is about to be open.
 * We use this to disallow opening large files on 32bit systems if
 * the caller didn't specify O_LARGEFILE.  On 64bit systems we force
 * on this flag in sys_open.
 */
int generic_file_open(struct inode * inode, struct file * filp)
{
	if (!(filp->f_flags & O_LARGEFILE) && i_size_read(inode) > MAX_NON_LFS)
		return -EOVERFLOW;
	return 0;
}

EXPORT_SYMBOL(generic_file_open);

/*
 * This is used by subsystems that don't want seekable
 * file descriptors. The function is not supposed to ever fail, the only
 * reason it returns an 'int' and not 'void' is so that it can be plugged
 * directly into file_operations structure.
 */
int nonseekable_open(struct inode *inode, struct file *filp)
{
	filp->f_mode &= ~(FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);
	return 0;
}

EXPORT_SYMBOL(nonseekable_open);
