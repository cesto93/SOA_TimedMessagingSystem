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

#define get_major(file)	MAJOR(file->f_inode->i_rdev)
#define get_minor(file)	MINOR(file->f_inode->i_rdev)

typedef struct _instance{ //dev instance data
	spinlock_t msg_list_lock;
	struct list_head msg_list;
	struct list_head session_list;
	wait_queue_head_t wait_queue;
	long storage_size;
	bool wake_up_cond;
} instance;

typedef struct _session{  //session data
	spinlock_t pending_list_lock;
	struct list_head pending_list;
	struct list_head session_node;
	long read_timeout;
	long write_timeout;
} session;

typedef struct msg_node {
	char * msg;
	int len;
	struct list_head node;
} msg_node;

typedef struct work_data{
	struct list_head node; //This list does not need to be sequential we cant try llist.h
	struct delayed_work my_work;
	instance * inst;
	session * sess;
	msg_node * my_msg_node;
} work_data;

static int Major;	/* Major number assigned to broadcast device driver */
struct workqueue_struct * queue;
instance instances[MINORS];

void deffered_write(struct delayed_work *work) {
	work_data * wdata = container_of(work, work_data, my_work);
	instance * inst = wdata->inst;
	session * sess = wdata->sess;
	msg_node * pending_msg = wdata->my_msg_node;
	
	spin_lock(&(sess->pending_list_lock));
	spin_lock(&(inst->msg_list_lock));
	
	if(pending_msg->len + inst->storage_size > max_storage_size) { //no space left on device
			list_del(&wdata->node);
			spin_unlock(&(inst->msg_list_lock));
			spin_unlock(&(sess->pending_list_lock));
			return;	
	}
	
	inst->storage_size += pending_msg->len;
	
	list_add_tail(&pending_msg->node, &inst->msg_list);
	list_del(&wdata->node);
	
	if (!inst->wake_up_cond) {
		inst->wake_up_cond = true;
		wake_up(&inst->wait_queue);
	}
	
	spin_unlock(&(inst->msg_list_lock));
	spin_unlock(&(sess->pending_list_lock));
	
	printk(KERN_INFO "%s: called deffered write msg : %s \n", MODNAME, pending_msg->msg);

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
	session * sess;
	int minor = get_minor(file);
	instance * inst = instances + minor;

	if(minor >= MINORS){
		return -ENODEV;
	}
	
	sess = kmalloc(sizeof(session), GFP_KERNEL);
	sess->read_timeout = 0;
	sess->write_timeout = 0;
	INIT_LIST_HEAD(&sess->pending_list);
	INIT_LIST_HEAD(&sess->session_node);
	spin_lock_init(&sess->pending_list_lock);
	
	spin_lock(&inst->msg_list_lock); //register session in instance object for flush operation
	list_add(&sess->session_node, &inst->session_list);
	spin_unlock(&inst->msg_list_lock);
	
	file->private_data = sess;

	printk("%s: device file successfully opened for object with minor %d\n", MODNAME, minor);
	return 0; 
}

static int dev_release(struct inode *inode, struct file *file) {
	int minor = get_minor(file);
	session * sess = file->private_data;
	instance * inst = instances + minor;
	
	//remove_works(&sess.pending_list); don't needed if we flush before
	
	spin_lock(&inst->msg_list_lock); //register session in instance object for flush operation
	list_del(&sess->session_node);
	spin_unlock(&inst->msg_list_lock);
	
	kfree(sess);
	printk("%s: device file closed with minor: %d\n", MODNAME, minor);
	return 0;
}

static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off) {
	int minor = get_minor(filp);
	int ret;
	instance * inst = instances + minor;
	msg_node * node;
	work_data * wdata;
	session * sess = filp->private_data;

	printk("%s: write on dev with [major,minor] number [%d,%d] timeout %ld", MODNAME, get_major(filp), get_minor(filp), 
			sess->write_timeout);	

	node = kmalloc(sizeof(msg_node), GFP_KERNEL);
	INIT_LIST_HEAD(&node->node);
	node->msg = kmalloc(len, GFP_KERNEL); 
	
	ret = copy_from_user(node->msg, buff, len);
	node->len = len - ret;
	
	if (node->len > max_msg_size) {
		return -ENOSPC;
	}
	
	if (sess->write_timeout == 0) { //istant write
		spin_lock(&(inst->msg_list_lock));
		
		if(node->len + inst->storage_size > max_storage_size) {
			spin_unlock(&(inst->msg_list_lock));
			return -ENOSPC;	//no space left on device
		}
		
		list_add_tail(&node->node, &inst->msg_list);
		inst->storage_size += node->len;
		
		if (!inst->wake_up_cond) {
			inst->wake_up_cond = true;
			wake_up(&inst->wait_queue);
		}
		
		spin_unlock(&(inst->msg_list_lock));
		
	} else { //deferred write
		
		wdata = kmalloc(sizeof(work_data), GFP_KERNEL);
		wdata->my_msg_node = node;
		wdata->inst = inst;
		wdata->sess = sess;
		
		INIT_DELAYED_WORK(&wdata->my_work, (void *) deffered_write);
		
		//queuing req need lock
		spin_lock(&(sess->pending_list_lock));
		list_add(&wdata->node, &sess->pending_list);
		spin_unlock(&(sess->pending_list_lock));
		
		queue_delayed_work(queue, &wdata->my_work, sess->write_timeout); //schedule write
	}
	
	*off += (len - ret);
	return len - ret;
}

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off) {
	int minor = get_minor(filp);
	int ret;
	instance * inst = instances + minor;
	session * sess = filp->private_data;
	msg_node * my_msg_node;

	printk("%s: read on dev with [major,minor] number [%d,%d] ", MODNAME, get_major(filp), get_minor(filp));

	spin_lock(&(inst->msg_list_lock));

	if (list_empty(&inst->msg_list)) {
		inst->wake_up_cond = 0;
		
		spin_unlock(&(inst->msg_list_lock));
		printk(KERN_INFO "%s: msg_list empty\n", MODNAME);
		
		if (sess->read_timeout == 0) { //no wait
			return 0;
		}
		
		if (wait_event_timeout(inst->wait_queue, inst->wake_up_cond, sess->read_timeout) == 0 || list_empty(&inst->msg_list)) {
			return 0;
		}
		spin_lock(&(inst->msg_list_lock));
	}
	
	//get the msg
	my_msg_node = list_entry(inst->msg_list.next, msg_node, node);
	list_del(inst->msg_list.next);
	inst->storage_size -= my_msg_node->len; //decrease storage size
	
	spin_unlock(&(inst->msg_list_lock));
	
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
	session *sess = filp->private_data;
	
	spin_lock(&(sess->pending_list_lock));
	
	switch(nr) {
		case SET_SEND_TIMEOUT: sess->write_timeout = param; break;
		case SET_RECV_TIMEOUT: sess->read_timeout = param; break;
		case REVOKE_DELAYED_MESSAGES: remove_works(&sess->pending_list); break;
		default: return -1;
	}
	spin_unlock(&(sess->pending_list_lock));
	
	printk(KERN_INFO "%s: ioctl on dev with [major,minor] number [%d,%d] nr %u param %lu \n", 
			MODNAME, get_major(filp), minor, nr, param);

	return 0;
}

static int dev_flush(struct file *filp, fl_owner_t id) {
	int minor = get_minor(filp);
	instance *inst = instances + minor;
	session *sess;
	printk("%s: device flush: minor %d\n", MODNAME, minor);
	
	spin_lock(&inst->msg_list_lock);
	list_for_each_entry(sess, &inst->session_list, session_node){
		spin_lock(&(sess->pending_list_lock));
		if (!list_empty(&sess->pending_list))
			remove_works(&sess->pending_list); //cancel pending write 
		spin_unlock(&(sess->pending_list_lock));
	}
	inst->wake_up_cond = true;
	spin_unlock(&inst->msg_list_lock);
	
	wake_up_all(&inst->wait_queue); //wake up reader
	
	
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
		spin_lock_init(&(instances[i].msg_list_lock));

		instances[i].storage_size = 0;
		INIT_LIST_HEAD(&instances[i].msg_list);
		INIT_LIST_HEAD(&instances[i].session_list);
		init_waitqueue_head(&instances[i].wait_queue);
		instances[i].wake_up_cond = false;
	}
	queue = alloc_workqueue("timed-msg-system", WQ_UNBOUND, 0);

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
		remove_msgs(&instances[i].msg_list);
		//TODO maybe we need to remove session if devices aren't closed
	}
	sys_size_exit();
	
	unregister_chrdev(Major, DEVICE_NAME);
	printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n", MODNAME, Major);
	return;
}
