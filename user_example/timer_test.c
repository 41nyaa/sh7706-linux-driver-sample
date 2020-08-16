#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "mytimerdrv/mytimerdrv.h"

static int status   = 0;

void sighandler( int signo )
{
	if ( SIGUSR1 == signo )
	{
		int fd = -1;
		char buf[1] = {0};
		
		fd = open( "/dev/myled", O_RDWR );
		
		if ( 0 == status )
		{
			/* OFF->ON */
			buf[0] = '1';
			write( fd, buf, 1 );
			printf("LED ON.\n");
			status = 1;
		}
		else
		{
			/* ON->OFF */
			buf[0] = '0';
			write( fd, buf, 1 );
			printf("LED OFF.\n");
			status = 0;
		}
		close( fd );
	}
}

int main()
{
	int fd = -1;
	struct sigaction sa;
	
	fd = open( "/dev/mytimer", O_RDWR );

	memset( &sa, 0, sizeof( sa ) );
	sa.sa_handler = &sighandler;
	sa.sa_flags = SA_RESTART;

	if ( 0 > sigaction( SIGUSR1, &sa, NULL ) ) 
	{
		perror("sigaction");
	}
	else
	{
		int signo = SIGUSR1;
		int ret = ioctl( fd, IOCTL_MYTIMER_SET );
	
		while( 1 )
		{
			usleep( 10 * 1000 * 1000 );
		}
	}
	close( fd );
}


