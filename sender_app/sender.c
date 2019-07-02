#include <iostream>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define PACKAGESIZE 65536

int main( int ac, char** av )
{    
    unsigned long messages;
    if( ac > 1 )
        messages = std::strtoul( av[1], NULL, 10 );
    else
        messages = 1;
    
    char symbol;
    int heap, packagesize;

    struct timespec current_stamp;
    void *msg = malloc( PACKAGESIZE );
    
    while( messages )
    {
        if(( heap = open( "/dev/heap", O_WRONLY )) < 0 )
            continue;
        
        std::cout << "try open /dev/heap, desc " << heap << std::endl;
        clock_gettime( CLOCK_REALTIME, &current_stamp );
        
        std::srand( current_stamp.tv_nsec );
        packagesize = 3 + std::rand() % ( PACKAGESIZE - 3 );
        
        
        for( int i = 0; i < packagesize; i++ )
        {
            clock_gettime( CLOCK_REALTIME, &current_stamp );
            std::srand( current_stamp.tv_nsec );
            symbol = 47 + std::rand() % ( 89 - 47 );
            
            if( i == ( packagesize - 2 ))
                symbol = '\0';
            if( i == ( packagesize - 1 ))
                symbol = '\n';

            std::memset(( char* )msg + i, symbol, sizeof( char ));
        }
    
        std::cout << ( char* )msg << std::endl;

        write( heap, msg, packagesize );
        close( heap );
        messages--;
    }
    free( msg );

    return 0;
}
