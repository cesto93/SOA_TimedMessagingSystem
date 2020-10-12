#include <linux/module.h> 
#include <linux/moduleparam.h> 
#include <linux/printk.h> 
#include <linux/init.h> 
#include <linux/fs.h> 
#include <linux/string.h>
#include <linux/kobject.h>
#include <linux/sysfs.h> 

#include "sys.h"

#define NAME "TimedMessagingSystem_sizes"

static struct kobject *example_kobject;
int max_msg_size = 128;
int max_storage_size = 4096;

module_param(max_msg_size, int, 0660);
module_param(max_storage_size, int, 0660);


static ssize_t max_msg_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
//just one page of storage from buff
	return sprintf(buf, "%d\n", max_msg_size);
}

static ssize_t max_msg_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
//just one page of storage from buff
	sscanf(buf, "%du", &max_msg_size);
	return count;
}

static ssize_t max_storage_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
//just one page of storage from buff
	return sprintf(buf, "%d\n", max_storage_size);
}

static ssize_t max_storage_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
//just one page of storage from buff
	sscanf(buf, "%du", &max_storage_size);
	return count;
}


static struct kobj_attribute msg_attribute = __ATTR(max_msg_size, 0660, max_msg_show, max_msg_store);
static struct kobj_attribute storage_attribute = __ATTR(max_storage_size, 0660, max_storage_show, max_storage_store);

int sys_size_init(void) {
	int error = 0;
	printk("%s: initialization\n", NAME);

	example_kobject = kobject_create_and_add(NAME, kernel_kobj); //the kernel_kobj variable points to /sys/kernel object
	if(!example_kobject)
		return -ENOMEM;

	error = sysfs_create_file(example_kobject, &msg_attribute.attr); //actually creating another kernel object
	if (error) {
		printk("%s: failed to create the target kobj\n", NAME);
		return error;
	}
	error = sysfs_create_file(example_kobject, &storage_attribute.attr); //actually creating another kernel object
	if (error) {
		printk("%s: failed to create the target kobj\n", NAME);
	}
	return error;
}

void sys_size_exit(void) {
	printk("%s: shutdown \n", NAME);
	kobject_put(example_kobject);
}
