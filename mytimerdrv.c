#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/major.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/sched.h>	/* signal */
#include <linux/unistd.h>
#include <linux/uaccess.h>	/* copy_from_user */
#include <linux/ioctl.h>

#include "mytimerdrv.h"

#define MYTIMER_DEV_NAME "mytimer"
#define MYTIMER_DEV_NUM  1
#define MYTIMER_IRQ      17	/* TMU0=16, TMU1=17..., 16はカーネルが使用中 */

#define IPRA   *(volatile unsigned short*)0xfffffee2	/* 16bit 割り込みコントロールレジスタ */
#define TSTR   *(volatile unsigned char*)0xfffffe92	/* 8bit タイマースタートレジスタ */
#define TCOR_1 *(volatile unsigned int*)0xfffffea0	/* 32bit タイマー0コンスタントレジスタ */
#define TCNT_1 *(volatile unsigned int*)0xfffffea4	/* 32bit タイマー0カウントレジスタ */
#define TCR_1  *(volatile unsigned short*)0xfffffea8	/* 16bit タイマー0コントロールレジスタ */


typedef struct SMytimerUseInfo
{
//	pid_t pid;
	struct pid* pid;
	int         signo;
} SMytimerUseInfo;

/* staticはこのファイルでしか使わないという意味。 */
static struct cdev  g_mytimer_cdev;
//static struct class *mytimer_class = NULL;	/* udev対応 */

static struct SMytimerUseInfo g_mytimer_useinfo = {NULL, 0};
static char* g_mytimer_intr_id = "mytimer_intr";

/* -------------------------------------------------------- */
/* プロトタイプ                                             */
/* -------------------------------------------------------- */
static void mytimer_set( void );
static void mytimer_unset( void );
static irqreturn_t mytimer_intr( int irq, void *dev );
static int mytimer_open( struct inode *inode, struct file *filp );
static int mytimer_ioctl( struct file *filep, unsigned int cmd, unsigned long arg );
static int mytimer_close( struct inode *inode, struct file *filp );
static int mytimer_init( void );	/* 引数なしはvoidを入れないとプロトタイプ宣言にならない */
static void mytimer_exit( void );

static struct file_operations mytimer_fops = {
	.owner          = THIS_MODULE,
	.open           = mytimer_open,
	.unlocked_ioctl = mytimer_ioctl,
	.release        = mytimer_close
};

static void mytimer_set()
{
	/* 0.125us * 8000000 = 1sec */
	/* カウンタクロックの選択(4分周、割り込み許可) */
	//ctrl_outw(0x0020(UNIE) | 0x0000(UP_EDGE) | 0x0000(CLK4), TMU1_TCR); // 0.125uS(8MHz)
	TCR_1 = 0x0020;
	
	/* タイマーコンスタントレジスタ(タイマーカウンタにセットしなおす値) */
	//ctrl_outl(8000000, TMU1_TCOR);
	TCOR_1 = 8000000;
	
	/* タイマーカウンタの初期化(回) */
	//ctrl_outl(8000000, TMU1_TCNT);
	TCNT_1 = 8000000;
	
	/* タイマーTMUチャネル1を起動 */
	//ctrl_outb(ctrl_inb(TMU_012_TSTR) | 0x02(TSTR_TMU1), TMU_012_TSTR);
	TSTR |= 0x02;
}

static void mytimer_unset()
{
	/* UNIE1を落とす */
	TCR_1 &= ~0x0020;
	
	/* タイマーTMUチャネル1を停止 */
	TSTR &= ~0x02;
}

/* -------------------------------------------------------- */
/* 割り込みルーチン                                         */
/* -------------------------------------------------------- */
static irqreturn_t mytimer_intr( int irq, void *dev )
{
	irqreturn_t ret  = IRQ_HANDLED;
	
	/* 割り込みステータスの確認 */
	if ( 0x0100 == ( TCR_1 & 0x0100 ) )
	{
		/* 割り込み要因のクリア */
		TCR_1 &= ~0x0100;	/* UNFビット */

		/* 処理 */
			
		if ( 0 != g_mytimer_useinfo.signo )
		{
			kill_pid( g_mytimer_useinfo.pid, g_mytimer_useinfo.signo, 1 );
		}		
	}
	else
	{
		ret = IRQ_NONE;
	}
	
  	return ret;
}


/* -----------------------------------
 * キャラクタ型ドライバ
 * ファイルオペレーション
 * ----------------------------------- */
static int mytimer_open( struct inode *inode, struct file *filp )
{
	int minor = MINOR( inode->i_rdev ); /* マイナー番号取得 */

	if( minor >= MYTIMER_DEV_NUM )
	{
		printk( KERN_ERR "%s open() error\n", MYTIMER_DEV_NAME );
		return -ENODEV;
	}
	
	return 0;
}

static int mytimer_ioctl( struct file *filep, unsigned int cmd, unsigned long arg )
{
	int ret = 0;
	
	if ( _IOC_TYPE( cmd ) != IOC_MYTIMER_MAGIC ) 
	{
        printk( KERN_ERR "%s: MAGIC NUM error. cmd=%x type=%x, mine=%x\n", 
        	MYTIMER_DEV_NAME, cmd, _IOC_TYPE( cmd ), IOC_MYTIMER_MAGIC );
        return -ENOTTY;
    }
	
    /** コマンドナンバーチェック */
    if ( _IOC_NR( cmd ) > IOCTL_MYTIMER_MAX ) 
	{
        printk( KERN_ERR "%s: COMMAND NUM error\n", MYTIMER_DEV_NAME );
        return -ENOTTY;
    }
	
	switch ( cmd )
	{
	case IOCTL_MYTIMER_SET:
		/* currentで現在の実行中のtask_sturctのプロセスを示す */
		g_mytimer_useinfo.pid   = task_pid( current );
		g_mytimer_useinfo.signo = SIGUSR1;
//		if ( copy_from_user( &g_mytimer_useinfo.signo, (int __user *)arg, sizeof( g_mytimer_useinfo.signo ) ) )
//		{
//			printk( KERN_ERR "%s: copy_from_user error", MYTIMER_DEV_NAME );
//			return -EFAULT;
//		}
		mytimer_set();
		printk( KERN_INFO "%s: ioctl cmd=%x pid=%x, signo=%d\n", 
			MYTIMER_DEV_NAME, cmd, g_mytimer_useinfo.pid, g_mytimer_useinfo.signo );
		break;
		
	default:
		printk( KERN_ERR "%s: ioctl error. cmd=%x\n", MYTIMER_DEV_NAME, cmd );
		break;
	}
	return ret;
}

static int mytimer_close( struct inode *inode, struct file *filp )
{
	return 0;
}

/* -------------------------------------------------------- */
/* driver module load / unload */
/* -------------------------------------------------------- */
static int mytimer_init()
{
	int   ret   = 0;
	dev_t devno = 0;	/* デバイス番号(メジャー番号+マイナー番号) */
	
	/* メジャー番号の確保 */
	ret = alloc_chrdev_region( &devno, 0, MYTIMER_DEV_NUM, MYTIMER_DEV_NAME );	/* ベースマイナー番号、個数、デバイス名 */
	if ( 0 > ret )
	{
		printk( KERN_ERR "Error Alloc DevNo. ret=%d\n", ret );
		goto ERROR;
	}
	
	/* fopsの登録 */
	cdev_init( &g_mytimer_cdev, &mytimer_fops );

	/* カーネルへ登録 */
	g_mytimer_cdev.owner = THIS_MODULE;
	g_mytimer_cdev.ops   = &mytimer_fops;
	ret = cdev_add( &g_mytimer_cdev, devno, MYTIMER_DEV_NUM );
	if ( 0 > ret )
	{
		printk( KERN_ERR "Error Add Device. ret=%d\n", ret );
		goto UNREGISTER_CHDEV;
	}
	
	
//	/* udev対応-----> */
//	int   major = 0;
//	/* /sys/class/mytimer登録 */
//	mytimer_class = class_create( THIS_MODULE, MYTIMER_DEV_NAME );
//	if ( IS_ERR( mytimer_class ) )
//	{
//		/* エラーコードに変換 */
//		ret = PTR_ERR( mytimer_class );
//		printk( KERN_ERR "Error Create Class. ret=%d\n", ret );
//		goto CDV_DEL;
//	}
//	
//	/* /dev/mytimer作成 */
//	major = MAJOR( devno );
//	device_create( mytimer_class, NULL, MKDEV( major, 0 ), NULL, MYTIMER_DEV_NAME );
//	
//	/* <-----udev対応 */
	
	g_mytimer_useinfo.pid  = NULL;
	g_mytimer_useinfo.signo = 0;
	
	/* 割り込みハンドラ登録 */
	/* IRQを共有しない場合はNULL */
	ret = request_irq( MYTIMER_IRQ, mytimer_intr, IRQF_DISABLED | IRQF_SHARED, "mytimer_intr", g_mytimer_intr_id );
	if ( 0 != ret )
	{
		printk( KERN_ERR "Error request_irq. ret=%d\n", ret );
	}
	
	return 0;
	
//	/* udev対応-----> */
//CDV_DEL:
//	cdev_del( &g_mytimer_cdev );
//	/* <-----udev対応 */
UNREGISTER_CHDEV:
	unregister_chrdev_region( devno, MYTIMER_DEV_NUM );
ERROR:
	return ret;
}

static void mytimer_exit()
{
	dev_t devno = g_mytimer_cdev.dev;
	
	mytimer_unset();
	
	/* 割り込みハンドラ登録解除 */
	free_irq( MYTIMER_IRQ, NULL );

	//	/* udev対応-----> */
//	int major = 0;
	
//	major = MAJOR( devno );
//	device_destroy( mytimer_class, MKDEV( major, 0 ) );

//	class_destroy( mytimer_class );
//	/* <-----udev対応 */
	cdev_del( &g_mytimer_cdev );
	unregister_chrdev_region( devno, MYTIMER_DEV_NUM );
}

module_init( mytimer_init );
module_exit( mytimer_exit );

MODULE_LICENSE("GPL");


