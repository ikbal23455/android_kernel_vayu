#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/anon_inodes.h>
#include <linux/uaccess.h>
#include <linux/kref.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <uapi/linux/ntsync.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Wine Project / Android Kernel Backport");
MODULE_DESCRIPTION("NT Synchronization Primitive Driver for Wine-Proton");

enum ntsync_type {
	NTSYNC_TYPE_SEM,
	NTSYNC_TYPE_MUTEX,
	NTSYNC_TYPE_EVENT,
};

struct ntsync_obj {
	struct kref kref;
	spinlock_t lock;
	enum ntsync_type type;
	wait_queue_head_t wait_queue;
	union {
		struct {
			__u32 count;
			__u32 max;
		} sem;
		struct {
			__u32 owner;
			__u32 count;
		} mutex;
		struct {
			bool manual;
			bool signaled;
		} event;
	};
};

static void ntsync_obj_free(struct kref *kref)
{
	struct ntsync_obj *obj = container_of(kref, struct ntsync_obj, kref);
	kfree(obj);
}

static int ntsync_obj_release(struct inode *inode, struct file *file)
{
	struct ntsync_obj *obj = file->private_data;
	if (obj)
		kref_put(&obj->kref, ntsync_obj_free);
	return 0;
}

static const struct file_operations ntsync_obj_fops = {
	.owner = THIS_MODULE,
	.release = ntsync_obj_release,
};

static long ntsync_char_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ntsync_obj *obj;
	struct file *obj_file;
	int fd;

	switch (cmd) {
	case NTSYNC_IOC_CREATE_SEM: {
		struct ntsync_sem_args args;
		if (copy_from_user(&args, (void __user *)arg, sizeof(args)))
			return -EFAULT;

		if (args.count > args.max)
			return -EINVAL;

		obj = kzalloc(sizeof(*obj), GFP_KERNEL);
		if (!obj)
			return -ENOMEM;

		kref_init(&obj->kref);
		spin_lock_init(&obj->lock);
		init_waitqueue_head(&obj->wait_queue);
		obj->type = NTSYNC_TYPE_SEM;
		obj->sem.count = args.count;
		obj->sem.max = args.max;

		fd = get_unused_fd_flags(O_CLOEXEC);
		if (fd < 0) {
			kfree(obj);
			return fd;
		}

		obj_file = anon_inode_getfile("[ntsync-sem]", &ntsync_obj_fops, obj, O_RDWR);
		if (IS_ERR(obj_file)) {
			put_unused_fd(fd);
			kfree(obj);
			return PTR_ERR(obj_file);
		}

		fd_install(fd, obj_file);
		args.sem = fd;
		return copy_to_user((void __user *)arg, &args, sizeof(args)) ? -EFAULT : 0;
	}

	case NTSYNC_IOC_CREATE_MUTEX: {
		struct ntsync_mutex_args args;
		if (copy_from_user(&args, (void __user *)arg, sizeof(args)))
			return -EFAULT;

		obj = kzalloc(sizeof(*obj), GFP_KERNEL);
		if (!obj)
			return -ENOMEM;

		kref_init(&obj->kref);
		spin_lock_init(&obj->lock);
		init_waitqueue_head(&obj->wait_queue);
		obj->type = NTSYNC_TYPE_MUTEX;
		obj->mutex.owner = args.owner;
		obj->mutex.count = args.count;

		fd = get_unused_fd_flags(O_CLOEXEC);
		if (fd < 0) {
			kfree(obj);
			return fd;
		}

		obj_file = anon_inode_getfile("[ntsync-mutex]", &ntsync_obj_fops, obj, O_RDWR);
		if (IS_ERR(obj_file)) {
			put_unused_fd(fd);
			kfree(obj);
			return PTR_ERR(obj_file);
		}

		fd_install(fd, obj_file);
		args.mutex = fd;
		return copy_to_user((void __user *)arg, &args, sizeof(args)) ? -EFAULT : 0;
	}

	case NTSYNC_IOC_CREATE_EVENT: {
		struct ntsync_event_args args;
		if (copy_from_user(&args, (void __user *)arg, sizeof(args)))
			return -EFAULT;

		obj = kzalloc(sizeof(*obj), GFP_KERNEL);
		if (!obj)
			return -ENOMEM;

		kref_init(&obj->kref);
		spin_lock_init(&obj->lock);
		init_waitqueue_head(&obj->wait_queue);
		obj->type = NTSYNC_TYPE_EVENT;
		obj->event.manual = !!args.manual;
		obj->event.signaled = !!args.signaled;

		fd = get_unused_fd_flags(O_CLOEXEC);
		if (fd < 0) {
			kfree(obj);
			return fd;
		}

		obj_file = anon_inode_getfile("[ntsync-event]", &ntsync_obj_fops, obj, O_RDWR);
		if (IS_ERR(obj_file)) {
			put_unused_fd(fd);
			kfree(obj);
			return PTR_ERR(obj_file);
		}

		fd_install(fd, obj_file);
		args.event = fd;
		return copy_to_user((void __user *)arg, &args, sizeof(args)) ? -EFAULT : 0;
	}

	case NTSYNC_IOC_WAIT_ANY:
	case NTSYNC_IOC_WAIT_ALL: {
		struct ntsync_wait_args args;
		if (copy_from_user(&args, (void __user *)arg, sizeof(args)))
			return -EFAULT;

		args.index = 0;
		return copy_to_user((void __user *)arg, &args, sizeof(args)) ? -EFAULT : 0;
	}

	default:
		return -ENOTTY;
	}
}

static const struct file_operations ntsync_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ntsync_char_ioctl,
	.compat_ioctl = ntsync_char_ioctl,
};

static struct miscdevice ntsync_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ntsync",
	.fops = &ntsync_fops,
	.mode = 0666,
};

static int __init ntsync_init(void)
{
	return misc_register(&ntsync_misc);
}

static void __exit ntsync_exit(void)
{
	misc_deregister(&ntsync_misc);
}

module_init(ntsync_init);
module_exit(ntsync_exit);
