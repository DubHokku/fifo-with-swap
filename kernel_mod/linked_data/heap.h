#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/atomic.h>
#include <linux/time.h>
#include <linux/list.h>

MODULE_LICENSE( "GPL" );
MODULE_AUTHOR( "Dub Hokku <hokku@x081.spb.ru>" );
MODULE_VERSION( "0.1" );

#define PACKAGESIZE 65536
#define HEAPSIZE    1024

struct entry_t
{
    unsigned size;
    char data[PACKAGESIZE];
    
    struct entry_t* next;
};

static struct entry_t rootEntry;
struct entry_t *currentEntry, *tmpEntry;
struct entry_t *ptrEntry = &rootEntry;
static struct task_struct *thread;

static atomic_t device_open = ATOMIC_INIT( 0 );
const char swap_directory[] = "/var/log/heap";
static unsigned short swap_postfix = 0;
struct swap_entry_t
{
    unsigned size;
    char file[64];
    struct list_head link;
};
    
LIST_HEAD( list );
struct list_head *ptr_list;
static struct file *swap_file;
struct swap_entry_t *swap_entry;
static struct timespec timestamp;

static int async_cache( void *data )
{
    int make_move = ( int )data;
    int count_entries = 0;
    loff_t pos = 0;

    if( make_move > 0 )
    {   // make_swap();
        
        for( currentEntry = ptrEntry; currentEntry != &rootEntry; currentEntry = currentEntry->next )
            count_entries++;
        
        if( make_move >= count_entries )
            make_move = count_entries - 1;
        
        if( count_entries == 0 )
            make_move = count_entries;
        
        currentEntry = ptrEntry;

        while( make_move )
        {
            pos = 0;
            swap_entry = vmalloc( sizeof( struct swap_entry_t ));
            memset( swap_entry->file, 0, 64 );
            
            timestamp = current_kernel_time();
            sprintf( swap_entry->file, "%s/%li_%li_%i", swap_directory, timestamp.tv_sec, timestamp.tv_nsec, make_move );
            swap_file = filp_open( swap_entry->file, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR | S_IWOTH );
            
            if(( swap_entry->size = kernel_write( swap_file, currentEntry->data, currentEntry->size, &pos )) != currentEntry->size )
            { 
                printk( "/dev/heap: kernel_write() ret. %i byte from %i \n", swap_entry->size, currentEntry->size ); 
                return -EIO; 
            }
            
            filp_close( swap_file, NULL );
            
            list_add( &( swap_entry->link ), &list );
            
            tmpEntry = currentEntry->next;
            vfree( currentEntry );
            currentEntry = tmpEntry;
            ptrEntry = tmpEntry;
            
            make_move--;
        }
        printk( "/dev/heap: make_swap() complete move entries %d \n", make_move );
    }
    
    if( make_move < 0 )
    {   // make_load();
        
        int swap_entries = 0;
        list_for_each( ptr_list, &list )
            swap_entries++;
            
        if(( swap_entries + make_move ) < 0 )
            make_move = -swap_entries;
        
        while( make_move )
        {
            pos = 0;
            swap_entry = list_first_entry( &list, struct swap_entry_t, link );
            swap_file = filp_open( swap_entry->file, O_RDONLY, 0 );
            
            if( IS_ERR( swap_file )) 
            { 
                printk( "/dev/heap: open failed: %s \n", swap_entry->file );
                atomic_dec( &device_open );
                return -ENOENT; 
            }
            
            tmpEntry = vmalloc( sizeof( struct entry_t ));
            if(( tmpEntry->size = kernel_read( swap_file, tmpEntry->data, swap_entry->size, &pos )) != swap_entry->size )
            {
                printk( "/dev/heap: kernel_read() ret. %i byte from %i \n", tmpEntry->size, swap_entry->size ); 
                atomic_dec( &device_open );
                return -EIO;
            }
            
            vfs_unlink( swap_file->f_path.dentry->d_parent->d_inode, swap_file->f_path.dentry, NULL );
            
            list_del( list.next );
            vfree( swap_entry );
            
            tmpEntry->next = ptrEntry;
            ptrEntry = tmpEntry;
            
            make_move++;
        }
        printk( "/dev/heap: make_load() complete move entries %d \n", make_move );
    }
    atomic_dec( &device_open );
    
    return 0;
}

static ssize_t get_info( struct file *file, char *buf, size_t count, loff_t *ppos ) 
{
    if( *ppos != 0 )
        return 0;
    if( atomic_read( &device_open ))
        return -EBUSY;
    atomic_inc( &device_open );
    
    int count_entries = 0;
    for( currentEntry = ptrEntry; currentEntry != &rootEntry; currentEntry = currentEntry->next )
        count_entries++;
    
    int swap_entries = 0;
    list_for_each( ptr_list, &list )
        swap_entries++;
    
    char user_message[64];
    memset( user_message, '\0', 64 );    
    sprintf( user_message, "HEAPSIZE %d, heap entries %d, swap %d \n", HEAPSIZE, count_entries, swap_entries );
    
    if( copy_to_user( buf, user_message, strlen( user_message )))
        goto out_info;
        
    *ppos = strlen( user_message );
    atomic_dec( &device_open );

    return *ppos;
    
    out_info:
        printk( "/proc/heap_node: error IO \n" );
        atomic_dec( &device_open );
        return EIO;
}

static ssize_t set_swap( struct file *file, const char *buf, size_t count, loff_t *ppos ) 
{    
    if( count < 1 )
        return EINVAL;
    if( atomic_read( &device_open ))
        return -EBUSY;
    atomic_inc( &device_open );

    int value;
    char msg[count];
    
    if( copy_from_user( msg, ( void* )buf, count ))
        goto out_data;
        
    strreplace( msg, '\n', '\0' );
    
    if( kstrtoint(( char* )msg, 10, &value ))
        goto out_value;
        
    thread = kthread_run( async_cache, ( void* )value, "async_cache" );
    if( NULL == thread )
        goto out_thread;
    
    return count;
    
    out_data:
        printk( "/proc/heap_node: error IO \n" );
        atomic_dec( &device_open );
        return EIO;
        
    out_value:
        printk( "/proc/heap_node: bad value %s \n", msg );
        atomic_dec( &device_open );
        return EINVAL;
    
    out_thread:
        printk( KERN_ERR "/dev/heap: thread create fault \n" );
        atomic_dec( &device_open );
        return -ENOMEM;
}

static ssize_t pop_front( struct file *file, char *buf, size_t count, loff_t *ppos ) 
{
    if( *ppos != 0 )
        return 0;
    loff_t rpos = 0;

    int tryread = 0;
    for( currentEntry = ptrEntry; currentEntry != &rootEntry; currentEntry = currentEntry->next )
    {
        if( currentEntry->next == &rootEntry )
        {
            if( copy_to_user( buf, currentEntry->data, currentEntry->size )) 
                return -EINVAL;
            
            *ppos = currentEntry->size;
            vfree( currentEntry );
            
            if( tryread > 0 )
                tmpEntry->next = &rootEntry;
            else
                ptrEntry = &rootEntry;
            
            // printk( "/dev/heap: pop_front() %i byte \n", *ppos );
            
            if( !list_empty( &list ))
            {
                swap_entry = list_first_entry( &list, struct swap_entry_t, link );
                
                swap_file = filp_open( swap_entry->file, O_RDONLY, 0 );
                if( IS_ERR( swap_file )) 
                { 
                    printk( "/dev/heap: open failed: %s \n", swap_entry->file ); 
                    return -ENOENT; 
                }

                tmpEntry = vmalloc( sizeof( struct entry_t ));
                if(( tmpEntry->size = kernel_read( swap_file, tmpEntry->data, swap_entry->size, &rpos )) != swap_entry->size )
                {
                    printk( "/dev/heap: kernel_read() ret. %i byte from %i \n", tmpEntry->size, swap_entry->size ); 
                    return -EIO;
                }

                vfs_unlink( swap_file->f_path.dentry->d_parent->d_inode, swap_file->f_path.dentry, NULL );
                
                // printk( "/dev/heap: pop_front() load %i byte \n", swap_entry->size );
                
                list_del( list.next );
                vfree( swap_entry );
                
                tmpEntry->next = ptrEntry;
                ptrEntry = tmpEntry;
            }
            
            return *ppos;
        }

        tmpEntry = currentEntry;
        tryread++;
    }
    
    return 0;
}

static ssize_t push_back( struct file *file, const char __user *buf, size_t count, loff_t *ppos )
{
    if( count < 1 )
        return -EINVAL;
    if( *ppos != 0 ) 
        return 0;
    
    int count_entries = 0;
    for( currentEntry = ptrEntry; currentEntry != &rootEntry; currentEntry = currentEntry->next )
        count_entries++;
    
    if( count_entries < HEAPSIZE )
    {
        if( !list_empty( &list ))
            goto make_cache;
        
        tmpEntry = vmalloc( sizeof( struct entry_t ));
        if( copy_from_user(( char* )tmpEntry->data, buf, count ))
            return -EINVAL;
        tmpEntry->size = count;
    
        tmpEntry->next = ptrEntry;
        ptrEntry = tmpEntry;
    
        // printk( "/dev/heap: push_back() %i byte \n", count );
        
        return count;
    }

    make_cache:    
    swap_entry = vmalloc( sizeof( struct swap_entry_t ));
    memset( swap_entry->file, 0, 64 );
    
    timestamp = current_kernel_time();
    sprintf( swap_entry->file, "%s/%li_%li_%u", swap_directory, timestamp.tv_sec, timestamp.tv_nsec, swap_postfix );
    swap_file = filp_open( swap_entry->file, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR | S_IWOTH ); 
    
    if(( swap_entry->size = kernel_write( swap_file, buf, count, ppos )) != count )
    { 
        printk( "/dev/heap: kernel_write() ret. %i byte from %i \n", swap_entry->size, count ); 
        return -EIO; 
    }
    
    filp_close( swap_file, NULL );
    
    if( list_empty( &list ))
        list_add( &( swap_entry->link ), &list );
    else
        list_add_tail( &( swap_entry->link ), &list );
    
    // printk( "/dev/heap: push_back() swap %i byte \n", swap_entry->size );
    swap_postfix++;
    
    return swap_entry->size;
}
