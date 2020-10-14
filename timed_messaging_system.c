#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>	
#include <linux/pid.h>		/* For pid types */
#include <linux/tty.h>		/* For the tty declarations */
#include <linux/version.h>	/* For LINUX_VERSION_CODE */

#include <linux/workqueue.h>
#include <linux/slab.h>

#include "timed_messaging_system.h"
#include "sys.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pier Francesco Contino");

static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

static int Major;	/* Major number assigned to broadcast device driver */

#define get_major(session)	MAJOR(session->f_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_inode->i_rdev)

typedef struct _session{
	struct mutex operation_synchronizer;
	struct llist_head msg_list;
	struct llist_head pending_list;
	long read_timeout;
	long write_timeout;
} session;

typedef struct msg_node {
	char * msg;
	int len;
	struct llist_node node;
} msg_node;

session sessions[MINORS];

typedef struct work_data{
	struct llist_head * pending_list;
	struct delayed_work my_work;
	session * msg_storage;
} work_data;

struct workqueue_struct * queue;

void deffered_write(struct delayed_work *work) {
	work_data * wdata = container_of(work, work_data, my_work);
	session * sess = wdata->msg_storage;
	struct llist_head * pending_list = wdata->pending_list;
	struct llist_node * req_node;
	msg_node * pending;
	
	printk("%s: called deffered write\n", MODNAME);
	
	mutex_lock(&(sess->operation_synchronizer));
	
	req_node = llist_del_first(pending_list); //first node in req
	pending = llist_entry(req_node, msg_node, node);
	
	llist_add(&pending->node, &sess->msg_list);
	
	mutex_unlock(&(sess->operation_synchronizer));

	kfree(wdata); 
}

static void remove_msgs(struct llist_head list) {
	msg_node * pending_data;
	llist_for_each_entry(pending_data, list.first, node) {
		kfree(pending_data->msg);
		kfree(pending_data);
	}
	llist_del_all(&list);
}

static int dev_open(struct inode *inode, struct file *file) {
	int minor = get_minor(file);

	if(minor >= MINORS){
		return -ENODEV;
	}

	printk("%s: device file successfully opened for object with minor %d\n", MODNAME, minor);
	//device opened by a default nop
	return 0; 
}

static int dev_release(struct inode *inode, struct file *file) {
	int minor = get_minor(file);
	printk("%s: device file closed with minor: %d\n", MODNAME, minor);
	//device closed by default nop
	return 0;
}

static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off) {
	int minor = get_minor(filp);
	int ret;
	session * my_session = sessions + minor;
	msg_node * pending_node;
	work_data * wdata;

	printk("%s: somebody called a write on dev with [major,minor] number [%d,%d]\n", MODNAME, get_major(filp), get_minor(filp));

	mutex_lock(&(my_session->operation_synchronizer));

	if(*off >= max_storage_size) {	//offset too large
		mutex_unlock(&(my_session->operation_synchronizer));
		return -ENOSPC;	//no space left on device
	} 

	if((max_storage_size - *off) < len) 
		len = max_storage_size - *off;

	if (len > max_msg_size)
		len = max_msg_size;		

	//work_queue
	if (my_session->write_timeout == 0) {
		msg_node * node = kmalloc(sizeof(msg_node), GFP_KERNEL);
		node->msg = kmalloc(len, GFP_KERNEL); 
		ret = copy_from_user(node->msg, buff, len);	
		node->len = len - ret;

		llist_add(&node->node, &my_session->msg_list);
		
	} else {
		pending_node = kmalloc(sizeof(msg_node), GFP_KERNEL);
		pending_node->msg = kmalloc(len, GFP_KERNEL);
		ret = copy_from_user(pending_node->msg, buff, len);
		pending_node->len = len - ret;
		
		llist_add(&pending_node->node, &my_session->pending_list);

		wdata = kmalloc(sizeof(work_data), GFP_KERNEL);
		wdata->pending_list = &my_session->pending_list;
		wdata->msg_storage = my_session;
		INIT_DELAYED_WORK(&wdata->my_work, (void *) deffered_write);
		queue_delayed_work(queue, &wdata->my_work, my_session->write_timeout);
	}
  
	*off += (len - ret);
	mutex_unlock(&(my_session->operation_synchronizer));

	return len - ret;
}

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off) {
	int minor = get_minor(filp);
	int ret;
	session * my_session = sessions + minor;
	struct llist_node * my_node;
	msg_node * my_msg_node;

	printk("%s: somebody called a read on dev with [major,minor] number [%d,%d] ", MODNAME, get_major(filp), get_minor(filp));

	mutex_lock(&(my_session->operation_synchronizer));

	if (llist_empty(&my_session->msg_list)) {
		printk("%s: msg_list empty\n", MODNAME);
		
		if (my_session->read_timeout == 0) {
			mutex_unlock(&(my_session->operation_synchronizer));
			return 0;
		}
		//TODO implement waiting read
	}
	
	//get the msg
	my_node = llist_del_first(&my_session->msg_list);
	my_msg_node = llist_entry(my_node, msg_node, node);
	if (len > my_msg_node->len)
		len = my_msg_node->len;
	
	ret = copy_to_user(buff, my_msg_node->msg, len);

	kfree(my_msg_node->msg);
	kfree(my_msg_node);
  
	//*off += (len - ret);
	mutex_unlock(&(my_session->operation_synchronizer));

	return len - ret;
}

static long dev_ioctl(struct file *filp, unsigned int command, unsigned long param) {
	int minor = get_minor(filp);
	int nr = _IOC_NR(command);
	session *my_session = sessions + minor;
	
	mutex_lock(&(my_session->operation_synchronizer));
	
	switch(nr) {
		case SET_SEND_TIMEOUT: my_session->write_timeout = param; break;
		case SET_RECV_TIMEOUT: my_session->read_timeout = param; break;
		case REVOKE_DELAYED_MESSAGES: remove_msgs(my_session->pending_list); break;
		default: return -1;
	}
	mutex_unlock(&(my_session->operation_synchronizer));
	
	printk("%s: ioctl on dev with [major,minor] number [%d,%d] and nr %u and param %lu \n", 
			MODNAME, get_major(filp), minor, nr, param);

	return 0;
}

static int dev_flush(struct file *filp, fl_owner_t id) {
	int minor = get_minor(filp);
	session *sess = sessions + minor;
	printk("somedoby called the flush\n");
	
	remove_msgs(sess->pending_list);
	//TODO awake waiting threads
	return 0;
}

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.write = dev_write,
	.read = dev_read,
	.open =  dev_open,
	.release = dev_release,
	.flush = dev_flush,
	.unlocked_ioctl = dev_ioctl
};

int init_module(void) {
	int i;

	//initialize the drive internal state
	for(i = 0; i < MINORS; i++) {
		mutex_init(&(sessions[i].operation_synchronizer));

		sessions[i].read_timeout = 0;
		sessions[i].write_timeout = 0;
		init_llist_head(&sessions[i].msg_list);
		init_llist_head(&sessions[i].pending_list);

	}
	queue = alloc_workqueue("timed-msg-system", WQ_UNBOUND, 32);

	Major = __register_chrdev(0, 0, MINORS, DEVICE_NAME, &fops); //allowed minors are directly controlled within this driver

	if (Major < 0) {
	  printk("%s: registering device failed\n", MODNAME);
	  return Major;
	}

	printk(KERN_INFO "%s: new device registered, it is assigned major number %d\n", MODNAME, Major);

	return sys_size_init();	
}

void cleanup_module(void) {
	int i;
	for(i = 0; i < MINORS; i++) {
		remove_msgs(sessions[i].pending_list);
		remove_msgs(sessions[i].msg_list);
	}
	sys_size_exit();
	
	unregister_chrdev(Major, DEVICE_NAME);
	printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n", MODNAME, Major);
	return;
}
