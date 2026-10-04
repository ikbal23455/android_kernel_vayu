#ifndef _UAPI_LINUX_NTSYNC_H
#define _UAPI_LINUX_NTSYNC_H

#include <linux/types.h>
#include <linux/ioctl.h>

struct ntsync_sem_args {
	__u32 count;
	__u32 max;
	__u32 sem;
};

struct ntsync_mutex_args {
	__u32 owner;
	__u32 count;
	__u32 mutex;
};

struct ntsync_event_args {
	__u32 manual;
	__u32 signaled;
	__u32 event;
};

struct ntsync_wait_args {
	__u64 timeout;
	__u64 objs;
	__u32 count;
	__u32 index;
	__u32 alert;
	__u32 pad;
};

#define NTSYNC_IOC_CREATE_SEM		_IOWR('N', 0x80, struct ntsync_sem_args)
#define NTSYNC_IOC_CREATE_MUTEX		_IOWR('N', 0x81, struct ntsync_mutex_args)
#define NTSYNC_IOC_CREATE_EVENT		_IOWR('N', 0x82, struct ntsync_event_args)
#define NTSYNC_IOC_WAIT_ANY		_IOWR('N', 0x83, struct ntsync_wait_args)
#define NTSYNC_IOC_WAIT_ALL		_IOWR('N', 0x84, struct ntsync_wait_args)

#endif /* _UAPI_LINUX_NTSYNC_H */
