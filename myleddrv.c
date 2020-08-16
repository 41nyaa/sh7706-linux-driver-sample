#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/major.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>

#define MYLED_DEV_NAME "myled"
#define MYLED_DEV_NUM  1

#define SCPDR   *(volatile unsigned char*)0xa4000136	/* 8bit SCポートデータレジスタ */

/* staticはこのファイルでしか使わないという意味。 */
static struct cdev  g_myled_cdev;
static spinlock_t   myled_spinlock;
static int          myled_accesscnt = 0;
struct semaphore    myled_sem;

/* -------------------------------------------------------- */
/* プロトタイプ                                             */
/* -------------------------------------------------------- */
static int myled_open( struct inode *inode, struct file *filp );
static int myled_close( struct inode *inode, struct file *filp );
static int myled_write( struct file *filp, const char *buf, size_t count, loff_t *pos );
static int myled_read( struct file* filp, char* buf, size_t count, loff_t* pos );
static int myled_init();
static void myled_exit();

static struct file_operations myled_fops = {
	.owner          = THIS_MODULE,
	.open           = myled_open,
	.write          = myled_write,
	.read           = myled_read,
	.release        = myled_close
};

/* -----------------------------------
 * キャラクタ型ドライバ
 * ファイルオペレーション
 * ----------------------------------- */
static int myled_open( struct inode *inode, struct file *filp )
{
	int minor = MINOR( inode->i_rdev ); /* マイナー番号取得 */

	spin_lock( &myled_spinlock );

	if ( 0 < myled_accesscnt ) 
	{
		spin_unlock( &myled_spinlock );
		return -EBUSY;
	}

	myled_accesscnt++;
	spin_unlock( &myled_spinlock );

	
	if( minor >= MYLED_DEV_NUM )
	{
		printk( KERN_ERR "%s open() error", MYLED_DEV_NAME );
		return -ENODEV;
	}

	/* private_dataに領域確保 */
	filp->private_data = vmalloc( sizeof( char ) );
	
	return 0;
}

static int myled_close( struct inode *inode, struct file *filp )
{
	if ( NULL != filp->private_data )
	{
		vfree( filp->private_data );
	}
	
	spin_lock( &myled_spinlock );

	myled_accesscnt--;
	
	spin_unlock( &myled_spinlock );
	
	return 0;
}

static int myled_write( struct file *filp, const char *buf, size_t count, loff_t *pos )
{
	int ret   = 0;
	
	if ( down_interruptible( &myled_sem ) ) 
	{
		return -ERESTARTSYS;
	}
	
	ret = copy_from_user( filp->private_data, buf, 1 );
	if( ret < 0 )
	{
		printk( KERN_ERR "Error copy_from_user. ret=%d\n", ret );
	}
	else
	{
		if( *((char*)filp->private_data) == '0' )
		{
			/* OFF */
			SCPDR &= ~0x10;
		}
		else if( *((char*)filp->private_data) == '1' )
		{
			/* ON */
			SCPDR |= 0x10;
		}
		else
		{
			printk( KERN_ERR "Error invalid value. ret=%c\n", *((char*)filp->private_data) );
		}
	}
	
	up( &myled_sem );

	return ret;
}

static int myled_read( struct file* filp, char* buf, size_t count, loff_t* pos )
{
	int ret   = 0;

	if ( down_interruptible( &myled_sem ) ) 
	{
		return -ERESTARTSYS;
	}
	
	ret = copy_to_user( buf, ((char*)filp->private_data), 1 );
	if( ret < 0 )
	{
		printk( KERN_ERR "%s:Error copy_from_user. ret=%d\n", MYLED_DEV_NAME, ret );
		ret = -EFAULT;
	}
	
	up( &myled_sem );

	return ret;
}

/* -------------------------------------------------------- */
/* driver module load / unload */
/* -------------------------------------------------------- */
static int myled_init()
{
	int   ret   = 0;
	dev_t devno = 0;	/* デバイス番号(メジャー番号+マイナー番号) */
	
	/* メジャー番号の確保 */
	ret = alloc_chrdev_region( &devno, 0, MYLED_DEV_NUM, MYLED_DEV_NAME );	/* ベースマイナー番号、個数、デバイス名 */
	if ( 0 > ret )
	{
		printk( KERN_ERR "%s:Error Alloc DevNo. ret=%d\n", MYLED_DEV_NAME, ret );
		goto ERROR;
	}
	
	/* fopsの登録 */
	cdev_init( &g_myled_cdev, &myled_fops );

	/* カーネルへ登録 */
	g_myled_cdev.owner = THIS_MODULE;
	g_myled_cdev.ops   = &myled_fops;
	ret = cdev_add( &g_myled_cdev, devno, MYLED_DEV_NUM );
	if ( 0 > ret )
	{
		printk( KERN_ERR "%s:Error Add Device. ret=%d\n", MYLED_DEV_NAME, ret );
		goto UNREGISTER_CHDEV;
	}
	
	sema_init( &myled_sem, 1 );
	
	return 0;
	
UNREGISTER_CHDEV:
	unregister_chrdev_region( devno, MYLED_DEV_NUM );
ERROR:
	return ret;
}

static void myled_exit()
{
	dev_t devno = g_myled_cdev.dev;

	cdev_del( &g_myled_cdev );
	unregister_chrdev_region( devno, MYLED_DEV_NUM );
}

module_init( myled_init );
module_exit( myled_exit );

MODULE_LICENSE("GPL");


