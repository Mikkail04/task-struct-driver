/*
 * scull.c -- the bare scull char module
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/cdev.h>

#include <linux/uaccess.h>	/* copy_*_user */

#include "scull.h"		/* local definitions */

/*
 * Our parameters which can be set at load time.
 */



// Define a structure for the linked list node
struct task_node {
    pid_t pid;
    pid_t tgid;
    struct list_head list;
};


// Define a global variable for the head of the linked list
static LIST_HEAD(task_list);
// Define a global variable to restrict and allow access to critical sections
static DEFINE_MUTEX(mutex); 


static int scull_major =   SCULL_MAJOR;
static int scull_minor =   0;
static int scull_quantum = SCULL_QUANTUM;

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);

MODULE_AUTHOR("pmeunier");
MODULE_LICENSE("Dual BSD/GPL");

static struct cdev scull_cdev;		/* Char device structure */

/*
 * Open and close
 */

// Declare functions that will be used to add and remove tasks to and from a linked list
static void add_task(pid_t pid, pid_t tgid);
static void remove_tasks(void);

static int scull_open(struct inode *inode, struct file *filp)
{
    add_task(current->pid, current->tgid);
    printk(KERN_INFO "scull open\n");
    return 0;          /* success */
}

static int scull_release(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "scull close\n");
	return 0;
}

/*
 * The ioctl() implementation
 */

// Function to locate a task node in the list based on its PID and TGID
static struct task_node* get_task(pid_t pid, pid_t tgid) {
    struct task_node *entry;
    // Iterate through each task node in the list, checking if it already exists
    list_for_each_entry(entry, &task_list, list) {
        if (entry->pid == pid && entry->tgid == tgid) {
	    // Task was found
            return entry;  
        }
    }
    // Task was not found
    return NULL; 
}

// Function to add a task to the list
static void add_task(pid_t pid, pid_t tgid) {
    struct task_node *new_task;

    // Check if the task already exists in the list
    mutex_lock(&mutex);
    if (get_task(pid, tgid)) {
        mutex_unlock(&mutex);
        return;  
    }

    // If the task doesn't exist, add it to the list
    new_task = kmalloc(sizeof(struct task_node), GFP_KERNEL);
    if (!new_task) {
        printk(KERN_ERR "Failed to allocate memory for new task node\n");
        mutex_unlock(&mutex);
        return;
    }

    // Set information of a new task, add it to the list, and release mutex lock to allow other functions to operate on the list
    new_task->pid = pid;
    new_task->tgid = tgid;
    list_add_tail(&new_task->list, &task_list);
    mutex_unlock(&mutex);
}

// Function to remove all task nodes from the list & deallocate memory
static void remove_tasks(void) {
    struct task_node *entry, *tmp;

    mutex_lock(&mutex);
    // Iterate through each task node in the list, deleting them and deallocating memory
    list_for_each_entry_safe(entry, tmp, &task_list, list) {
        list_del(&entry->list);
        kfree(entry);
    }
    mutex_unlock(&mutex);
}

static long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int err = 0, tmp;
	int retval = 0;
    
	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > SCULL_IOC_MAXNR) return -ENOTTY;

	err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	if (err) return -EFAULT;

	switch(cmd) {

	case SCULL_IOCIQUANTUM: 
	        struct task_info info;

		// Task information to go inside the info struct
            	info.__state = current->flags;
            	info.cpu = task_cpu(current);
            	info.prio = current->prio;
            	info.pid = current->pid;
            	info.tgid = current->tgid;
            	info.nvcsw = current->nvcsw;
            	info.nivcsw = current->nivcsw;

	    	// Copy task information to user space
            	retval = copy_to_user((struct task_info __user *)arg, &info, sizeof(struct task_info));
            	if (retval == 0) {
                	add_task(info.pid, info.tgid);
            	}
    		break;

	case SCULL_IOCRESET:
		scull_quantum = SCULL_QUANTUM;
		break;
        
	case SCULL_IOCSQUANTUM: /* Set: arg points to the value */
		retval = __get_user(scull_quantum, (int __user *)arg);
		break;

	case SCULL_IOCTQUANTUM: /* Tell: arg is the value */
		scull_quantum = arg;
		break;

	case SCULL_IOCGQUANTUM: /* Get: arg is pointer to result */
		retval = __put_user(scull_quantum, (int __user *)arg);
		break;

	case SCULL_IOCQQUANTUM: /* Query: return it (it's positive) */
		return scull_quantum;

	case SCULL_IOCXQUANTUM: /* eXchange: use arg as pointer */
		tmp = scull_quantum;
		retval = __get_user(scull_quantum, (int __user *)arg);
		if (retval == 0)
			retval = __put_user(tmp, (int __user *)arg);
		break;

	case SCULL_IOCHQUANTUM: /* sHift: like Tell + Query */
		tmp = scull_quantum;
		scull_quantum = arg;
		return tmp;

	default:  /* redundant, as cmd was checked against MAXNR */
		return -ENOTTY;
	}
	return retval;
}

struct file_operations scull_fops = {
	.owner =    THIS_MODULE,
	.unlocked_ioctl = scull_ioctl,
	.open =     scull_open,
	.release =  scull_release,
};

/*
 * Finally, the module stuff
 */

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 */
void scull_cleanup_module(void)
{
    dev_t devno = MKDEV(scull_major, scull_minor);
    struct task_node *entry;
    int task_count = 0; 

    // Print information from the list 
    mutex_lock(&mutex);
    list_for_each_entry(entry, &task_list, list) {
	task_count++; 
        printk(KERN_INFO "Task %d: PID %d, TGID %d\n", task_count, entry->pid, entry->tgid);
    }
    mutex_unlock(&mutex);

    // Remove all tasks from the list and deallocate memory
    remove_tasks();

    /* Get rid of the char dev entry */
    cdev_del(&scull_cdev);

    /* cleanup_module is never called if registering failed */
    unregister_chrdev_region(devno, 1);
}

int scull_init_module(void)
{
	int result;
	dev_t dev = 0;

	/*
	 * Get a range of minor numbers to work with, asking for a dynamic
	 * major unless directed otherwise at load time.
	 */
	if (scull_major) {
		dev = MKDEV(scull_major, scull_minor);
		result = register_chrdev_region(dev, 1, "scull");
	} else {
		result = alloc_chrdev_region(&dev, scull_minor, 1, "scull");
		scull_major = MAJOR(dev);
	}
	if (result < 0) {
		printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
		return result;
	}

	cdev_init(&scull_cdev, &scull_fops);
	scull_cdev.owner = THIS_MODULE;
	result = cdev_add (&scull_cdev, dev, 1);
	/* Fail gracefully if need be */
	if (result) {
		printk(KERN_NOTICE "Error %d adding scull character device", result);
		goto fail;
	}

	return 0; /* succeed */

  fail:
	scull_cleanup_module();
	return result;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);
