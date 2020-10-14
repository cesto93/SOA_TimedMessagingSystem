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

typedef msg_node pending_data_node; // PENDING WRITE DATA

session msg_buffers[MINORS];

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
	pending_data_node * pending_data;
	msg_node * new;
	
	mutex_lock(&(sess->operation_synchronizer));
	
	req_node = llist_del_first(pending_list); //first node in req
	pending_data = llist_entry(req_node, pending_data_node, node);

	new = kmalloc(sizeof(msg_node), GFP_KERNEL);
	new->msg = pending_data->msg;
	new->len = pending_data->len; 

	llist_add(&new->node, &sess->msg_list);
	
	mutex_unlock(&(sess->operation_synchronizer));

	kfree(pending_data);
	kfree(wdata);
}

static void remove_pending_msg(session *sess) {
	pending_data_node * pending_data;
	llist_for_each_entry(pending_data, sess->pending_list.first, node) {
		kfree(pending_data->msg);
		kfree(pending_data);
	}
	llist_del_all(&sess->pending_list);
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
	session * my_msg_buffer = msg_buffers + minor;
	pending_data_node * pending_node;
	work_data * wdata;

	printk("%s: somebody called a write on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));

	mutex_lock(&(my_msg_buffer->operation_synchronizer));

	if(*off >= max_storage_size) {	//offset too large
		mutex_unlock(&(my_msg_buffer->operation_synchronizer));
		return -ENOSPC;	//no space left on device
	} 

	if((max_storage_size - *off) < len) 
		len = max_storage_size - *off;

	if (len > max_msg_size)
		len = max_msg_size;		

	//work_queue
	if (my_msg_buffer->write_timeout == 0) {
		msg_node * node = kmalloc(sizeof(msg_node), GFP_KERNEL);
		node->msg = kmalloc(len, GFP_KERNEL); 
		ret = copy_from_user(node->msg, buff, len);	
		node->len = len - ret;

		llist_add(&node->node, &my_msg_buffer->msg_list);
		
	} else {
		pending_node = kmalloc(sizeof(pending_data_node), GFP_KERNEL);
		pending_node->msg = kmalloc(len, GFP_KERNEL);
		ret = copy_from_user(pending_node->msg, buff, len);
		pending_node->len = ret;
		llist_add(&pending_node->node, &my_msg_buffer->pending_list);

		wdata = kmalloc(sizeof(pending_data_node), GFP_KERNEL);
		wdata->pending_list = &my_msg_buffer->pending_list;
		INIT_DELAYED_WORK(&wdata->my_work, (void *) deffered_write);
		queue_delayed_work(queue, &wdata->my_work, my_msg_buffer->write_timeout);
	}

  
	*off += (len - ret);
	mutex_unlock(&(my_msg_buffer->operation_synchronizer));

	return len - ret;
}

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off) {
	int minor = get_minor(filp);
	int ret;
	session * msg_buffer = msg_buffers + minor;
	struct llist_node * my_node;
	msg_node * my_msg_node;

	printk("%s: somebody called a read on dev with [major,minor] number [%d,%d]\n", MODNAME, get_major(filp), get_minor(filp));

	mutex_lock(&(msg_buffer->operation_synchronizer));

	if (llist_empty(&msg_buffer->msg_list)) {
		mutex_unlock(&(msg_buffer->operation_synchronizer));
		return 0;
	}

	//get the msg
	my_node = llist_del_first(&msg_buffer->msg_list);
	my_msg_node = llist_entry(my_node, msg_node, node);
	if (len > my_msg_node->len)
		len = my_msg_node->len;
	
	ret = copy_to_user(buff, my_msg_node->msg, len);

	kfree(my_msg_node->msg);
	kfree(my_msg_node);
  
	//*off += (len - ret);
	mutex_unlock(&(msg_buffer->operation_synchronizer));

	return len - ret;
}

static long dev_ioctl(struct file *filp, unsigned int command, unsigned long param) {
	int minor = get_minor(filp);
	int nr = _IOC_NR(command);
	session *msg_buffer = msg_buffers + minor;
	
	mutex_lock(&(msg_buffer->operation_synchronizer));
	
	switch(nr) {
		case SET_SEND_TIMEOUT: msg_buffer->write_timeout = param; break;
		case SET_RECV_TIMEOUT: msg_buffer->read_timeout = param; break;
		case REVOKE_DELAYED_MESSAGES: remove_pending_msg(msg_buffers); break;
		default: return -1;
	}
	mutex_unlock(&(msg_buffer->operation_synchronizer));
	
	printk("%s: ioctl on dev with [major,minor] number [%d,%d] and nr %u \n", 
			MODNAME, get_major(filp), minor, nr);

	return 0;
}

static int dev_flush(struct file *filp, fl_owner_t id) {
	int minor = get_minor(filp);
	session *sess = msg_buffers + minor;
	printk("somedoby called the flush\n");
	
	remove_pending_msg(sess);
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
		mutex_init(&(msg_buffers[i].operation_synchronizer));

		msg_buffers[i].read_timeout = 0;
		msg_buffers[i].write_timeout = 0;
		init_llist_head(&msg_buffers[i].msg_list);
		init_llist_head(&msg_buffers[i].pending_list);


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
	msg_node * msg_node;

	for(i = 0; i < MINORS; i++) {
		remove_pending_msg(&msg_buffers[i]);
		llist_for_each_entry(msg_node, msg_buffers[i].msg_list.first, node) {
			kfree(msg_node->msg);
			kfree(msg_node);
		}

	}
	sys_size_exit();
	
	unregister_chrdev(Major, DEVICE_NAME);
	printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n", MODNAME, Major);
	return;
}
