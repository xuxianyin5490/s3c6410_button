#include<linux/module.h>
#include<linux/init.h>
#include<linux/miscdevice.h>
#include<linux/fs.h>
#include<linux/interrupt.h>
#include<plat/irqs.h>
#include<linux/irq.h>
#include<linux/wait.h>
#include<asm/gpio.h>
#include<asm/io.h>
#include<linux/ioport.h>
#include<asm-generic/poll.h>
#include<asm/uaccess.h>
#include<linux/sched.h>
#include<linux/poll.h>

#define DEVICENAME	"s3c6410_button"

#define GPNDAT		0x7F008834
static volatile long int *gpndat_addr;

static DECLARE_WAIT_QUEUE_HEAD(button_waitq);

struct button_info
{
	int irq;
	int port;
	char *name;
	int num;
};

static struct button_info info[]=
{
	{IRQ_EINT(0),0,"button0",0},
	{IRQ_EINT(1),1,"button1",1},
	{IRQ_EINT(2),2,"button2",2},
	{IRQ_EINT(3),3,"button3",3},
	{IRQ_EINT(4),4,"button4",4},
	{IRQ_EINT(5),5,"button5",5},
	{IRQ_EINT(9),9,"button6",6},
	{IRQ_EINT(11),11,"button7",7},
};
char value[] = {0,0,0,0,0,0,0,0};
char press_flag;
static irqreturn_t button_interrupt (int irq, void *dev_id)
{
	struct button_info info = *(struct button_info *)dev_id;
	char button_value = (ioread16(gpndat_addr)>>info.port)&0x01;
	if(button_value != (value[info.num]&0x01))
	{
		value[info.num] = '0' + button_value;
		press_flag = 1;
		wake_up_interruptible(&button_waitq);
	}
	return IRQ_RETVAL(IRQ_HANDLED);
}

int s3c6410_open (struct inode *inode, struct file *file)
{
	int i=0;
	int ret=0;
	for(i=0;i<8;i++)
	{
		ret = request_irq(info[i].irq, button_interrupt,IRQ_TYPE_EDGE_BOTH, info[i].name, (void*)&info[i]);
		if(ret<0)
			break;
	}
	if(ret<0)
	{
		i--;
		for(;i>=0;i--)
		{
			if(info[i].irq < 0)
			{
				continue;
			}
			disable_irq(info[i].irq);
			free_irq(info[i].irq,(void*)&info[i]);
		}
	}
	press_flag = 1;
	return ret;
}

ssize_t s3c6410_read (struct file *file, char __user *user, size_t size, loff_t *loff)
{
	unsigned long err;
	if(!press_flag)
	{
		if(file->f_flags & O_NONBLOCK)
		{
				return -EAGAIN;
		}
		else
		{
			wait_event_interruptible(button_waitq,press_flag);
		}
	}
	press_flag = 0;
	err = copy_to_user(user,(const void*)value,min(sizeof(value),size));
	return err ? -EFAULT:min(sizeof(value),size);
}

unsigned int s3c6410_poll (struct file *file, struct poll_table_struct *table)
{
	unsigned int mask = 0;
	poll_wait(file,&button_waitq,table);
	if(press_flag==1)
	{
		mask |= POLLIN | POLLRDNORM;
	}
	return mask;
}

int s3c6410_close(struct inode *inode, struct file *file)
{
	int i=0;
	for(i=0;i>8;i++)
	{
		if(info[i].irq < 0)
		{
			continue;
		}
		disable_irq(info[i].irq);
		free_irq(info[i].irq,(void*)&info[i]);
	}
	return 0;
}

struct file_operations s3c6410_fops = 
{
	.owner = THIS_MODULE,
	.read = &s3c6410_read,
	.open = &s3c6410_open,
	.poll = &s3c6410_poll,
	.release = &s3c6410_close,
};

struct miscdevice misc =
{
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICENAME,
	.fops = &s3c6410_fops,
};


static int __init button_init(void)
{
	int ret=0;
	if(!request_mem_region(GPNDAT,4,"s3c6410_button"))
	{
		ret = -EBUSY;
		printk("s3c6410_button request_mem_failed\n");
		goto request_mem_failed;
	}
	gpndat_addr = ioremap(GPNDAT,4);
	if(gpndat_addr==NULL)
	{
		ret = -EBUSY;
		printk("s3c6410_button ioremap_failed\n");
		goto ioremap_failed;
	}
	ret = misc_register(&misc);
	if(ret<0)
	{
		return -EBUSY;
		goto misc_regster_failed;
	}
	printk("s3c6410 button initialize\n");
	return ret;
misc_regster_failed:
	misc_deregister(&misc);
ioremap_failed:
	iounmap(gpndat_addr);
request_mem_failed:
	release_mem_region(GPNDAT,4);
	return ret;
}

static void __exit button_exit(void)
{
	iounmap(gpndat_addr);
	release_mem_region(GPNDAT,4);
	misc_deregister(&misc);
	printk("s3c6410 button exit\n");
}

module_init(button_init);
module_exit(button_exit);

MODULE_AUTHOR("Xu Xianyin");
MODULE_LICENSE("GPL");

