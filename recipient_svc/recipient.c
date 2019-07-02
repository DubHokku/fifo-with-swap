#include <iostream>
#include <cstring>
#include <cstdlib>
#include <ctime>

#include <unistd.h>
#include <signal.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#define PACKAGESIZE 65536
#define OFFSET 1000000000

int heap = 0, file = 0, buf_size = 0, stored = 0;
int child_thread = 0;
static char out_filename[32];
unsigned short file_postfix = 0;
static void *exchange_buf = malloc( PACKAGESIZE );
static struct timespec current_stamp;
 

void save_data()
{
    clock_gettime( CLOCK_REALTIME, &current_stamp );
    memset( out_filename, 0, sizeof( out_filename ));
    sprintf( out_filename, "%u_%u_%u", current_stamp.tv_sec, current_stamp.tv_nsec, file_postfix );
        
    file = open( out_filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH );
    if( file < 1 )
        std::cout << "can't open " << out_filename << std::endl;
    stored = write( file, exchange_buf, buf_size );
    if( stored < 1 )
        std::cout << "can't write " << out_filename << std::endl;
    close( file );
    file = 0;

    std::cout << "file " << out_filename << " heap buf_size " << buf_size << std::endl;
    file_postfix++;
    buf_size = 0;
    stored = 0;
}
    
void handle_signal( int sig )
{
    if( sig == SIGTERM )
    {
        if( heap > 0 )
            close( heap );
        if( file > 0 )
            close( file );
        
        if( buf_size > stored )
            save_data();
        
        free( exchange_buf );
        exit( 0 );
    }
}

static void make_daemon()
{    
    pid_t pid = 0;
    int fd;

    pid = fork();       /* Fork off the parent process */
    
    if( pid < 0 )       /* An error occurred */
        exit( EXIT_FAILURE );

    if( pid > 0 )       /* Success: Let the parent terminate */
        exit( EXIT_SUCCESS );

    if( setsid() < 0 )  /* On success: The child process becomes session leader */
        exit( EXIT_FAILURE );

    signal( SIGCHLD, SIG_IGN );     /* Ignore signal sent from child to parent process */
    pid = fork();                   /* Fork off for the second time*/

    if( pid < 0 )                   /* An error occurred */
        exit( EXIT_FAILURE );
        
    if( pid > 0 )                   /* Success: Let the parent terminate */
        exit( EXIT_SUCCESS );

    child_thread++;
    
    for( fd = sysconf( _SC_OPEN_MAX ); fd > 0; fd-- ) //* Close all open file descriptors *
        close( fd );

    stdin = fopen( "/dev/null", "r" );  //* Reopen stdin (fd = 0), stdout (fd = 1), stderr (fd = 2) *
    stdout = fopen( "/dev/null", "w+" );
    stderr = fopen( "/dev/null", "w+" );
}

int main( int ac, char** av )
{
    struct timespec delay;
    unsigned long long delay_value = 0;

    if( ac > 1 )
        delay_value = std::strtoul( av[1], NULL, 10 );
    delay_value = delay_value * ( OFFSET / 1000 );
    
    if( delay_value >= OFFSET )
    {    
        delay.tv_sec = delay_value / OFFSET;
        delay.tv_nsec = delay_value % OFFSET;
    }
    else
    {
        delay.tv_sec = 0;
        delay.tv_nsec = delay_value;
    }
    signal( SIGTERM, handle_signal );
    
    if( !child_thread )
        make_daemon();
    
    while( true )
    {   
        memset( exchange_buf, 0, PACKAGESIZE );
        if(( heap = open( "/dev/heap", O_RDONLY )) < 0 )
            continue;
        buf_size = read( heap, exchange_buf, PACKAGESIZE );
        close( heap );
        heap = 0;
        
        if( buf_size > 0 )
            save_data();
        
        nanosleep( &delay, NULL );
    }
    
    return 0;
}
