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
	struct mutex msg_list_mtx;
	struct mutex pending_list_mtx;
	struct list_head msg_list;
	struct list_head pending_list;
	wait_queue_head_t wait_queue;
	long read_timeout;
	long write_timeout;
	long storage_size;
} session;

typedef struct msg_node {
	char * msg;
	int len;
	struct list_head node;
} msg_node;

typedef struct work_data{
	struct list_head node; //This list does not need to be sequential we cant try llist.h
	struct delayed_work my_work;
	session * sess;
	msg_node * my_msg_node;
} work_data;

struct workqueue_struct * queue;
session sessions[MINORS];

void deffered_write(struct delayed_work *work) {
	work_data * wdata = container_of(work, work_data, my_work);
	session * sess = wdata->sess;
	msg_node * pending_msg = wdata->my_msg_node;
	
	mutex_lock(&(sess->pending_list_mtx));
	mutex_lock(&(sess->msg_list_mtx));
	
	if(pending_msg->len + sess->storage_size > max_storage_size) { //no space left on device
			list_del_init(&pending_msg->node);
			mutex_unlock(&(sess->msg_list_mtx));
			mutex_unlock(&(sess->pending_list_mtx));
			return;	
	}
	
	sess->storage_size += pending_msg->len;
	
	list_move_tail(&pending_msg->node, &sess->msg_list);
	list_del_init(&wdata->node);
	
	mutex_unlock(&(sess->msg_list_mtx));
	mutex_unlock(&(sess->pending_list_mtx));
	
	printk("%s: called deffered write msg : %s \n", MODNAME, pending_msg->msg);

	kfree(wdata); 
}

static void remove_msgs(struct list_head *list) {
	msg_node * pending_data, * temp;
	list_for_each_entry_safe(pending_data, temp, list, node){
		list_del(&pending_data->node);
		kfree(pending_data->msg);
		kfree(pending_data);
	}
}

static void remove_works(struct list_head *list) {
	work_data * w_data, * temp;
	list_for_each_entry_safe(w_data, temp, list, node){
		list_del(&w_data->node);
		cancel_delayed_work(&w_data->my_work);
		kfree(w_data->my_msg_node->msg);
		kfree(w_data->my_msg_node);
		kfree(w_data);
	}
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
	session * sess = sessions + minor;
	msg_node * node;
	work_data * wdata;

	printk("%s: write on dev with [major,minor] number [%d,%d]\n", MODNAME, get_major(filp), get_minor(filp));	

	node = kmalloc(sizeof(msg_node), GFP_KERNEL);
	INIT_LIST_HEAD(&node->node);
	node->msg = kmalloc(len, GFP_KERNEL); 
	
	ret = copy_from_user(node->msg, buff, len);
	node->len = len - ret;
	
	if (node->len > max_msg_size) {
		return -ENOSPC;
	}
	
	if (sess->write_timeout == 0) { //istant write
		mutex_lock(&(sess->msg_list_mtx));
		
		if(node->len + sess->storage_size > max_storage_size) {
			mutex_unlock(&(sess->msg_list_mtx));
			return -ENOSPC;	//no space left on device
		}
		
		list_add_tail(&node->node, &sess->msg_list);
		sess->storage_size += node->len;
		
		mutex_unlock(&(sess->msg_list_mtx));
		
	} else { //deferred write
		
		wdata = kmalloc(sizeof(work_data), GFP_KERNEL);
		wdata->my_msg_node = node;
		wdata->sess = sess;
		
		INIT_DELAYED_WORK(&wdata->my_work, (void *) deffered_write);
		
		//queuing req need lock
		mutex_lock(&(sess->pending_list_mtx));
		list_add(&wdata->node, &sess->pending_list);
		mutex_unlock(&(sess->pending_list_mtx));
		
		queue_delayed_work(queue, &wdata->my_work, sess->write_timeout); //schedule write
	}
	
	*off += (len - ret);
	return len - ret;
}

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off) {
	int minor = get_minor(filp);
	int ret;
	session * sess = sessions + minor;
	msg_node * my_msg_node;

	printk("%s: read on dev with [major,minor] number [%d,%d] ", MODNAME, get_major(filp), get_minor(filp));

	mutex_lock(&(sess->msg_list_mtx));

	if (list_empty(&sess->msg_list)) {
		mutex_unlock(&(sess->msg_list_mtx));
		printk("%s: msg_list empty\n", MODNAME);
		
		if (sess->read_timeout == 0) {
			return 0;
		}

		if (wait_event_timeout(sess->wait_queue, !list_empty(&sess->msg_list), sess->read_timeout) == 0) {
			return 0;
		}
		mutex_lock(&(sess->msg_list_mtx));
	}
	
	//get the msg
	my_msg_node = list_entry(sess->msg_list.next, msg_node, node);
	list_del(sess->msg_list.next);
	sess->storage_size -= my_msg_node->len; //decrease storage size
	
	mutex_unlock(&(sess->msg_list_mtx));
	
	if (len > my_msg_node->len)
		len = my_msg_node->len;
	
	ret = copy_to_user(buff, my_msg_node->msg, len);

	kfree(my_msg_node->msg);
	kfree(my_msg_node);

	return len - ret;
}

static long dev_ioctl(struct file *filp, unsigned int command, unsigned long param) {
	int minor = get_minor(filp);
	int nr = _IOC_NR(command);
	session *sess = sessions + minor;
	
	mutex_lock(&(sess->pending_list_mtx));
	
	switch(nr) {
		case SET_SEND_TIMEOUT: sess->write_timeout = param; break;
		case SET_RECV_TIMEOUT: sess->read_timeout = param; break;
		case REVOKE_DELAYED_MESSAGES: remove_works(&sess->pending_list); break;
		default: return -1;
	}
	mutex_unlock(&(sess->pending_list_mtx));
	
	printk(KERN_INFO "%s: ioctl on dev with [major,minor] number [%d,%d] nr %u param %lu \n", 
			MODNAME, get_major(filp), minor, nr, param);

	return 0;
}

static int dev_flush(struct file *filp, fl_owner_t id) {
	int minor = get_minor(filp);
	session *sess = sessions + minor;
	printk(KERN_INFO "%s: device flush: minor %d\n", MODNAME, minor);
	
	mutex_lock(&(sess->pending_list_mtx));
	if (!list_empty(&sess->pending_list))
		remove_works(&sess->pending_list); //cancel pending write
	mutex_unlock(&(sess->pending_list_mtx));
	
	wake_up(&sess->wait_queue); //wake up reader
	
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
		mutex_init(&(sessions[i].msg_list_mtx));
		mutex_init(&(sessions[i].pending_list_mtx));

		sessions[i].read_timeout = 0;
		sessions[i].write_timeout = 0;
		sessions[i].storage_size = 0;
		INIT_LIST_HEAD(&sessions[i].msg_list);
		INIT_LIST_HEAD(&sessions[i].pending_list);
		init_waitqueue_head(&sessions[i].wait_queue);
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
		remove_works(&sessions[i].pending_list);
		remove_msgs(&sessions[i].msg_list);
	}
	sys_size_exit();
	
	unregister_chrdev(Major, DEVICE_NAME);
	printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n", MODNAME, Major);
	return;
}
