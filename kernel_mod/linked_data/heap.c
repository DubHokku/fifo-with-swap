#include <linux/module.h>
#include <linux/kthread.h>

#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/stat.h>

#include <linux/uaccess.h>
#include <asm/uaccess.h>

#include "./heap.h"

#define EOK 0

static int heap_open( struct inode *n, struct file *f ) 
{
    if( atomic_read( &device_open ))
        return -EBUSY;
    atomic_inc( &device_open );
    return EOK;
}
static int heap_release( struct inode *n, struct file *f ) 
{
    atomic_dec( &device_open );
    return EOK;
}

static const struct file_operations heap_fops = {
    .owner = THIS_MODULE,
    .open = heap_open,
    .release = heap_release,
    .read = pop_front,
    .write = push_back,
};

static struct miscdevice heap_dev = {
    MISC_DYNAMIC_MINOR, "heap", &heap_fops
};

#define NAME_NODE "heap_node"

static const struct file_operations node_fops = {
    .owner = THIS_MODULE,
    .read = get_info,
    .write = set_swap,
};

static int __init heap_init( void ) 
{
    int ret = misc_register( &heap_dev );
    if( ret ) 
        goto out_dev;

    struct proc_dir_entry *own_proc_node;
    own_proc_node = proc_create( NAME_NODE, S_IFREG | S_IRUGO | S_IWUGO | S_IROTH | S_IWOTH, NULL, &node_fops );
    if( NULL == own_proc_node ) 
        goto out_proc;
    
    return 0;
    
    out_dev:
        printk( KERN_ERR "/dev/heap: register misc.device fault \n" );
        return ret;
    out_proc:
        printk( KERN_ERR "/dev/heap: create /proc/%s fault \n", NAME_NODE );
        return -ENOMEM;
}

static void __exit heap_exit( void ) 
{
    remove_proc_entry( NAME_NODE, NULL );
    misc_deregister( &heap_dev );
}

module_init( heap_init );
module_exit( heap_exit );
