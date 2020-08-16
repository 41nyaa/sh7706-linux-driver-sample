#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include<stdio.h>
#include <unistd.h>

int main()
{
	char buf[1]={ 0 };
	
	int fd = -1;

	fd = open( "/dev/myled", O_RDWR );

	while( 1 )
	{
		/* OFF */
		buf[0] = '0';
		write( fd, buf, 1 );
		printf( "OFF(write) %c", buf[0] );
	
		buf[0] = '9';
		read( fd, buf, 1 );
		printf( "OFF(read)  %c", buf[0] );
		usleep( 1000 * 1000 );//1000msec
		
		/* ON */
		buf[0] = '1';
		write( fd, buf, 1 );
		printf( "ON(write) %c", buf[0] );
		
		buf[0] = '9';
		read( fd, buf, 1 );
		printf( "ON(read)  %c", buf[0] );
		usleep( 1000 * 1000 );//1000msec
	}
	
	close( fd );
}


