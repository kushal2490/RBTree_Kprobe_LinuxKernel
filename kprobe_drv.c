#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/pci.h>
#include <linux/param.h>
#include <linux/list.h>
#include <linux/semaphore.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/rbtree.h>
#include <linux/mutex.h>
#include <linux/kprobes.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include "libioctl.h" 
#include "rbt530.h"
#include "kprobe530.h"

#define DEVICE_NAME "rbprobe_dev"

typedef struct object_node{
	struct rb_node node;
	rb_object_t object;
}object_node_t;


/* Per device structure */
struct rbprobe_dev {
	struct cdev cdev;		/* The cdev structure */
	char name[20];			/* Name of device */
	struct kprobe kp;
} *rbprobe_devp;

static dev_t rbprobe_dev_number;		/* Allotted device number */
struct class *rbprobe_dev_class;		/* Tie with the device model */
static struct device *rbprobe_dev_device;
spinlock_t lock_kprobe;
unsigned long flags;
kp_buf_t buffer;

/* Time Stamp Counter */
uint64_t tsc(void)                     
{
     uint32_t lo, hi;
     asm volatile("rdtsc" : "=a" (lo), "=d" (hi));

	return (( (uint64_t)lo)|( (uint64_t)hi)<<32 );
}

/* pre_handler: this is called just before the probed instruction is
  * executed.
  */
int handler_pre(struct kprobe *p, struct pt_regs *regs) {
    // printk("pre_handler: p->addr=0x%p, eflags=0x\n",p->addr);
    return 0;
}

 
 /* post_handler: this is called after the probed instruction is executed
  *     (provided no exception is generated).
  */
void handler_post(struct kprobe *p, struct pt_regs *regs, unsigned long flags) {
 
	int key, data, i;
	unsigned long index = 0;

	//Array variables
	unsigned long bpoffset1 = 0x32c;
	unsigned long bpoffset2 = 0x328;

	//base address of key and data in the array
	unsigned long kbaseaddr = ((regs->bp) - bpoffset1);
	unsigned long dbaseaddr = ((regs->bp) - bpoffset2);

	//disabling preemption
 	spin_lock_irqsave(&lock_kprobe,flags);

 	data = *((int*)(dbaseaddr));
 	i = 0;
 	while(data != 0)
 	{	
 		key = *((int*)(kbaseaddr + index));
 		data = *((int*)(dbaseaddr + index));
 		buffer.trace_obj[i].key = key;
 		buffer.trace_obj[i].data = data;
 		i++;
 		index += 0x8;//accessing array elements key, data from the stack
 		data = *((int*)(dbaseaddr + index));
 	}

 	buffer.kp_addr = (unsigned long)p->addr;
 	buffer.pid = current->pid;
 	buffer.tsc = tsc();

 	spin_unlock_irqrestore(&lock_kprobe,flags);
}

 
 /* fault_handler: this is called if an exception is generated for any
  * instruction within the fault-handler, or when Kprobes
  * single-steps the probed instruction.
  */
int handler_fault(struct kprobe *p, struct pt_regs *regs, int trapnr) {
    printk("fault_handler:p->addr=0x%p, eflags=0x\n", p->addr);
    return 0;
}


/* 
 Open rbprobe driver 
*/
int kprobe_driver_open(struct inode *inode, struct file *file)
{
	struct rbprobe_dev *rbprobe_devp;

	/* Get the per-device structure that contains this cdev */
	rbprobe_devp = container_of(inode->i_cdev, struct rbprobe_dev, cdev);

	/* Easy access to devp from rest of the entry points */
	file->private_data = rbprobe_devp;

	printk(KERN_INFO "\n%s is opening \n", rbprobe_devp->name);
	return 0;
}

/* 
* Release the rbprobe driver
*/
int kprobe_driver_release(struct inode *inode, struct file *file)
{
	struct rbprobe_dev *rbprobe_devp = file->private_data;
	printk(KERN_INFO "\n%s is closing\n", rbprobe_devp->name);
	return 0;
}

ssize_t kprobe_driver_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct rbprobe_dev *rbprobe_devp = file->private_data;
	int ret;
	long offset;

	struct kprobe *kp = &(rbprobe_devp->kp);
	kprobe_obj_t kernval;

	ret = copy_from_user(&kernval, buf, sizeof(kprobe_obj_t));
	if(ret){
		return -1;
	}

	offset = kernval.offset;

	if(kernval.flag==1)	
	{
		/* specify pre_handler address */
	 	kp->pre_handler=handler_pre;
		/* specify post_handler address */
	   	kp->post_handler=handler_post;

		/* specify fault_handler address */
	  	kp->fault_handler=handler_fault;

		/* specify the address/offset where you want to insert probe. */
		/* You can get the address using one of the methods described above. */
	  	kp->addr = (kprobe_opcode_t *) kallsyms_lookup_name("rb_insert");
	  	kp->addr += offset;

		/* check if the kallsyms_lookup_name() returned the correct value. */
    	if (kp->addr == NULL) {
 	    	printk("kallsyms_lookup_name could not find address for the specified symbol name\n");
	    	return 1;
		}


		/* All set to register with Kprobes */
		ret = register_kprobe(kp);
		if (ret < 0) {
			printk(KERN_INFO "register_kprobe failed at %p, returned %d\n", kp->addr, ret);
			return ret;
		}
		printk(KERN_INFO "Planted kprobe at %p\n", kp->addr);
		return 0;
	}

	else if(kernval.flag == 0)
	{
		unregister_kprobe(kp);
		printk(KERN_INFO "kprobe at %p unregistered\n", kp->addr);
		return 0;
	}

	return 0;
}


ssize_t kprobe_driver_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	int i, ret;

	spin_lock_irqsave(&lock_kprobe,flags);
	i = 0;
	if(buffer.trace_obj[i].data != 0)
	{
		ret = copy_to_user(buf, &buffer, sizeof(kp_buf_t));
	}
	else
	{
		spin_unlock_irqrestore(&lock_kprobe,flags);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&lock_kprobe,flags);

	return 0;
}

/* 
* File operations structure. Defined in linux/fs.h
*/
static struct file_operations kprobe_fops = {
    .owner			= THIS_MODULE,			/* Owner */
    .open			= kprobe_driver_open,		/* Open method */
    .read			= kprobe_driver_read,		/* Read method */
    .write			= kprobe_driver_write,		/* Write method */
    .release			= kprobe_driver_release,	/* Release method */
};



static int __init kprobe_init(void)
{
	int ret;

	/* Request dynamic allocation of a device major number */
	if(alloc_chrdev_region(&rbprobe_dev_number, 0, 1, DEVICE_NAME)) {
		printk(KERN_INFO "Can't register device\n");
		return -1;
	}

	/* Populate sysfs entries */
	rbprobe_dev_class = class_create(THIS_MODULE, DEVICE_NAME);

	/* Allocate memory for the per-device structure */
	rbprobe_devp = kmalloc(sizeof(struct rbprobe_dev), GFP_KERNEL);

	if(!rbprobe_devp) {
		printk(KERN_INFO "Bad kmalloc\n");
		return -ENOMEM;
	}

	/* Request I/O region */
	sprintf(rbprobe_devp->name, DEVICE_NAME);

	/* Connect the file operations with the cdev */
	cdev_init(&rbprobe_devp->cdev, &kprobe_fops);
	rbprobe_devp->cdev.owner = THIS_MODULE;

	/* Connect the major/minor number to the cdev */
	ret = cdev_add(&rbprobe_devp->cdev, rbprobe_dev_number, 1);

	if(ret) {
		printk(KERN_INFO "Bad cdev\n");
		return ret;
	}

	/* Send uevents to udev, so it'll create /dev nodes */
	rbprobe_dev_device = device_create(rbprobe_dev_class, NULL, MKDEV(MAJOR(rbprobe_dev_number), 0), NULL, DEVICE_NAME);

	spin_lock_init(&lock_kprobe);

	printk(KERN_INFO "rbprobe driver initialized.\n");
	return 0;
}

static void __exit kprobe_exit(void)
{
	/* Release the major number */
	unregister_chrdev_region((rbprobe_dev_number), 1);

	/* Destroy device */
	device_destroy(rbprobe_dev_class, rbprobe_dev_number);
	cdev_del(&rbprobe_devp->cdev);

	/* Free all the objects from the device */
	kfree(rbprobe_devp);

	/* Destroy driver_class */
	class_destroy(rbprobe_dev_class);

	printk(KERN_INFO "Driver removed\n");
}

module_init(kprobe_init);
module_exit(kprobe_exit);
MODULE_LICENSE("GPL v2");

