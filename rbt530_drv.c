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
#include "libioctl.h" 
#include "rbt530.h"

#define DEVICE_NAME "rbt530_dev"

DEFINE_MUTEX(kmutex);

/* Object node to be stored in the tree */
typedef struct object_node{
	struct rb_node node;
	rb_object_t object;
}object_node_t;

/* Per device structure */
struct rbt530_dev {
	struct cdev cdev;		/* The cdev structure */
	char name[20];			/* Name of device */
	struct rb_root rbt530;		/* Root node of the rb-tree device*/
	int set_end;			/* Flag set by user to set the end of tree to be read */
	int dump;			/* Flag set by user to dump tree */
} *rbt530_devp;

static dev_t rbt530_dev_number;		/* Allotted device number */
struct class *rbt530_dev_class;		/* Tie with the device model */
static struct device *rbt530_dev_device;
struct dump dp;

/* 
 Open rbt530 driver 
*/
int rbt530_driver_open(struct inode *inode, struct file *file)
{
	struct rbt530_dev *rbt530_devp;

	/* Get the per-device structure that contains this cdev */
	rbt530_devp = container_of(inode->i_cdev, struct rbt530_dev, cdev);

	/* Easy access to devp from rest of the entry points */
	file->private_data = rbt530_devp;

	/* Reset set_end and dump flags when device opened*/
	rbt530_devp->set_end = 0;
	rbt530_devp->dump = 0;

	printk(KERN_INFO "\n%s is opening \n", rbt530_devp->name);
	return 0;
}

/* 
* Release the rbt530 driver
*/
int rbt530_driver_release(struct inode *inode, struct file *file)
{
	struct rbt530_dev *rbt530_devp = file->private_data;
	printk(KERN_INFO "\n%s is closing\n", rbt530_devp->name);
	return 0;
}

/*
*	IOCTL commands called by the user:
* 	SETEND - set a device flag to specify to read first or lst object
*	SETDUMP - To set the dump flag to print all the objects of the rb-tree device
*/
long rbt530_driver_ioctl(struct file *file,unsigned int cmd, unsigned long arg)
{

	struct rbt530_dev *rbt530_devp = file->private_data;
	
	switch(cmd)
	{
		case SETEND:
		{
			rbt530_devp->set_end = arg;
		}
		break;
		case SETDUMP:
		{
			rbt530_devp->dump = arg;
		}
		break;	
		case PRINT:
		{
			printk(KERN_INFO "\n40 OBJECTS WRITTEN. STARTING 100 RANDOM READ/WRITE OPERATIONS\n\n");
		}
		break;
		default:
			printk("Invalid Command\n");
	}
	return 0;
}

/*
* Function to insert objects into the rb-tree device
*/
int rb_insert(struct rb_root *root, object_node_t *objnode)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	volatile int key = 0, data = 0, i=0;
	volatile rb_object_t traceBuf[100];
	// volatile rb_object_t *ptr = 0;

	int index = 0;
	/* Figure out where to put new node */
	while(*new){
		struct object_node *this = container_of(*new, struct object_node, node);
		key = this->object.key;
		data = this->object.data;
		i = 5;
		traceBuf[index].key = key;
		traceBuf[index].data = data;
		parent = *new;
		if((objnode->object).data < (this->object).data)
			new = &((*new)->rb_left);
  		else if ((objnode->object).data > (this->object).data)
  			new = &((*new)->rb_right);
  		else if((objnode->object).data == (this->object).data){
  			printk("Duplicate Insertions not allowed\n");
  			return 0;
  		}
  		else
  			return 0;
  		index++;
	}

	key = objnode->object.key;
	data = objnode->object.data;
	i = 2;
	traceBuf[index].key = key;
	traceBuf[index].data = data;
	index++;
	//This array contains the traversed nodes for kprobe to read
	traceBuf[index].key = 0;
	traceBuf[index].data = 0;
	i = 8;//probe inserted here

	/* link node with parent node */
	rb_link_node(&(objnode->node), parent, new);

	/* rebalance tree */
	rb_insert_color(&(objnode->node), root);

	return 0;
}

/* 
* Write to rbt530 driver
*/
ssize_t rbt530_driver_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	int ret;
	int loc_key;
	int keyFound = 0;
	struct rbt530_dev *rbt530_devp = file->private_data;
	struct rb_node *node = rbt530_devp->rbt530.rb_node;
	struct object_node *tmpnode;
	rb_object_t obj_buf;
	object_node_t *objp_new;
	
	/* copy (key,data) pair from the user to local buffer */
	ret = copy_from_user(&obj_buf, buf, sizeof(rb_object_t));
	if(ret){
		return -1;
	}

	/* allocate memory for object node and initialize it with (key, data) received from user */
	objp_new = (object_node_t*)kmalloc(sizeof(object_node_t), GFP_KERNEL);
	if(!objp_new) {
		printk(KERN_INFO "Bad kmalloc\n");
		return -ENOMEM;
	}

	/* Copy key,data from user to a local object */
	(objp_new->object).data = obj_buf.data;
	(objp_new->object).key = obj_buf.key;

	/* Check whether TREE is empty and object node to be inserted has valid data */
	if((rbt530_devp->rbt530.rb_node == NULL) && (obj_buf.data != 0)){
		printk(KERN_INFO "\nINSERTING FIRST NODE\tk=%d, v=%d\n", (objp_new->object).key, (objp_new->object).data );
		mutex_lock(&kmutex);
		rb_insert(&(rbt530_devp->rbt530), objp_new);
		mutex_unlock(&kmutex);
	}

	/* If TREE not empty, check if an object with the same key already exists in the tree */
	else{
		for(node = rb_first(&rbt530_devp->rbt530); node; node = rb_next(node)){
			loc_key = container_of(node, object_node_t, node)->object.key;
		
			if(loc_key == obj_buf.key){
			keyFound = 1;
			/* if matching key found then break from the loop */
			break;
			}
			else{
			keyFound = 0;
			}
		}

		tmpnode = container_of(node, object_node_t, node);

		/* if object with same key exists and data field of userBuffer is 0, then delete existing object from the tree */
		if(keyFound == 1){
			if(obj_buf.data == 0){
				printk(KERN_INFO "\n ERASING NODE\tk=%d, v=%d\n", (tmpnode->object).key, (tmpnode->object).data );
				mutex_lock(&kmutex);
				rb_erase(&tmpnode->node, &rbt530_devp->rbt530);
				mutex_unlock(&kmutex);
			}
			/* if data is not 0 then replace the existing object with the new one */
			else{
				printk(KERN_INFO "REPLACING NODE\t\tk=%d, v=%d with\tk= %d,  v=%d\n", (tmpnode->object).key, (tmpnode->object).data , (objp_new->object).key, (objp_new->object).data);
				mutex_lock(&kmutex);
				// rb_replace_node(&tmpnode->node, &objp_new->node, &rbt530_devp->rbt530);
				rb_erase(&tmpnode->node, &rbt530_devp->rbt530);
				rb_insert(&(rbt530_devp->rbt530), objp_new);
				mutex_unlock(&kmutex);
			}
		}
		/* if object with same key doesn't exist and data is valid, then insert it into the tree */
		else if(obj_buf.data != 0){
			printk(KERN_INFO "INSERTING NEW NODE\tk=%d, v=%d\n", (objp_new->object).key, (objp_new->object).data );
			mutex_lock(&kmutex);
			rb_insert(&(rbt530_devp->rbt530), objp_new);
			mutex_unlock(&kmutex);
		}
		/* if object with same key doesn't exist and data is not valid (data = zero), free the memory */
		else{
			printk(KERN_INFO "INVALID DATA, NOTHING TO DO ...\n");
			kfree(objp_new);
		}
	}
	return 0;
}


/*
* Read to rbt530 driver
*/
ssize_t rbt530_driver_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct rbt530_dev *rbt530_devp = file->private_data;
	rb_object_t obj_buf;
	int ret, i=0;
	struct rb_node *node = NULL;
	struct object_node *tmpnode;
	volatile int rkey = 0, rdata = 0, j = 0;

	if(rbt530_devp->rbt530.rb_node == NULL){
		return -EINVAL;
	}

	if(rbt530_devp->dump == 1){		//if dump flag is set then print all elements of the tree
		printk(KERN_INFO "\n **** DUMPING TREE **** \n\n");
		for(node = rb_first(&rbt530_devp->rbt530); node; node = rb_next(node)){
				dp.dumparray[i].key = container_of(node, object_node_t, node)->object.key;
				dp.dumparray[i].data = container_of(node, object_node_t, node)->object.data;
				printk(KERN_INFO "node %d:\tk= %d v= %d \n",i+1, dp.dumparray[i].key, dp.dumparray[i].data);
				i++;
			}
		dp.numNodes = i;
		/* Sending tree data to user */
		ret = copy_to_user(buf, &dp, sizeof(struct dump));
	}

	else{	//Read first or last end of the tree

		if(rbt530_devp->set_end == 0){
			node = rb_first(&rbt530_devp->rbt530);
			printk(KERN_INFO "READING FIRST DATA\tk=%d, v=%d\tDELETED NODE\n", container_of(node, object_node_t, node)->object.key, container_of(node, object_node_t, node)->object.data);
			obj_buf.key = container_of(node, object_node_t, node)->object.key;
			obj_buf.data = container_of(node, object_node_t, node)->object.data;
		}
		else if(rbt530_devp->set_end == 1){
			node = rb_last(&rbt530_devp->rbt530);
			printk(KERN_INFO "READING LAST DATA\tk=%d, v=%d\tDELETED NODE\n", container_of(node, object_node_t, node)->object.key, container_of(node, object_node_t, node)->object.data);
			obj_buf.key = container_of(node, object_node_t, node)->object.key;
			obj_buf.data = container_of(node, object_node_t, node)->object.data;
		}
		j = 3;
		rkey = obj_buf.key;
		rdata = obj_buf.data;
		j = 7;
		/* Send the object node found at first/last node to the user */
		ret = copy_to_user(buf, &obj_buf, sizeof(rb_object_t));

		tmpnode = container_of(node, object_node_t, node);
		/* Delete the object node from the tree after reading it */
		rb_erase(&tmpnode->node, &rbt530_devp->rbt530);
	}

	return 0;
}

/* 
* File operations structure. Defined in linux/fs.h
*/
static struct file_operations rbt530_fops = {
    .owner			= THIS_MODULE,			/* Owner */
    .open			= rbt530_driver_open,		/* Open method */
    .read			= rbt530_driver_read,		/* Read method */
    .unlocked_ioctl	= rbt530_driver_ioctl,		/* IOCTL method */
    .write			= rbt530_driver_write,		/* Write method */
    .release		= rbt530_driver_release,	/* Release method */
};


int __init rbt530_driver_init(void)
{
	int ret;

	/* Request dynamic allocation of a device major number */
	if(alloc_chrdev_region(&rbt530_dev_number, 0, 1, DEVICE_NAME)) {
		printk(KERN_INFO "Can't register device\n");
		return -1;
	}

	/* Populate sysfs entries */
	rbt530_dev_class = class_create(THIS_MODULE, DEVICE_NAME);

	/* Allocate memory for the per-device structure */
	rbt530_devp = kmalloc(sizeof(struct rbt530_dev), GFP_KERNEL);

	if(!rbt530_devp) {
		printk(KERN_INFO "Bad kmalloc\n");
		return -ENOMEM;
	}

	/* Request I/O region */
	sprintf(rbt530_devp->name, DEVICE_NAME);

	/* Connect the file operations with the cdev */
	cdev_init(&rbt530_devp->cdev, &rbt530_fops);
	rbt530_devp->cdev.owner = THIS_MODULE;

	/* Connect the major/minor number to the cdev */
	ret = cdev_add(&rbt530_devp->cdev, rbt530_dev_number, 1);

	if(ret) {
		printk(KERN_INFO "Bad cdev\n");
		return ret;
	}

	/* Initialize my tree with an Empty node */
	rbt530_devp->rbt530 = RB_ROOT;
	
	/* Send uevents to udev, so it'll create /dev nodes */
	rbt530_dev_device = device_create(rbt530_dev_class, NULL, MKDEV(MAJOR(rbt530_dev_number), 0), NULL, DEVICE_NAME);

	printk(KERN_INFO "rbt530 driver initialized.\n");
	return 0;

}

void __exit rbt530_driver_exit(void)
{
	/* Release the major number */
	unregister_chrdev_region((rbt530_dev_number), 1);

	/* Destroy device */
	device_destroy(rbt530_dev_class, rbt530_dev_number);
	cdev_del(&rbt530_devp->cdev);

	/* Free all the objects from the device */
	kfree(rbt530_devp);

	/* Destroy driver_class */
	class_destroy(rbt530_dev_class);

	printk(KERN_INFO "Driver removed\n");
}

module_init(rbt530_driver_init);
module_exit(rbt530_driver_exit);

MODULE_AUTHOR("CSE530 - TEAM 27");
MODULE_DESCRIPTION("RB Tree as kernel device");
MODULE_LICENSE("GPL v2");
