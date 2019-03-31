/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */
// 开启仿真协处理器
#define EM

#define __LIBRARY__ /* 为了包括定义在unistd.h中的内嵌汇编代码等信息 */

#include <unistd.h>
#include <time.h>

/*
 * we need this inline - forking from kernel space will result in NO COPY ON WRITE (!!!), until an execve
 * is executed. This is no problem, but for the stack. This is handled by not letting main() use the stack 
 * at all after fork(). Thus, no function calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there won't be any messing with the stack from 
 * main(), but we define some others too.
 */
/*
 * 我们需要下面这些内嵌语句 - 从内核空间创建进程将导致没有写时复制(COPY ON WRITE)!!!直到执行一个execve调用。
 * 这对堆栈可能带来问题。处理方法是在fork()调用后不让main()使用任何堆栈。因此就不能有函数调用 - 这意味着fork
 * 也要使用内嵌代码，否则我们在从fork()退出时就要使用堆栈了。
 *
 * 实际上只有pause和fork需要使用内嵌方式，以保证从main()中不会弄乱堆栈，但是我们同时还定义了其他一些函数。
 */
// Linux在内核空间创建进程时不使用写时复制技术(Copy on write)。main()在移动到用户模式（到任务0）后执行内嵌
// 方式的fork()和pause(),因此可保证不使用任务0的用户栈。在执行move_to_user_mode()之后，本程序main()就以任务
// 0的身份在运行了。而任务0是所有将创建子进程的父进程。当字创建一个子进程时（init进程），由于任务1代码属于内
// 核空间，因此没有使用写时复制功能。此时任务0的用户栈就是任务1的用户栈，即它们共同使用一个栈空间。因此希望
// 在main.c运行在任务0的环境下时不要有对堆栈的任何操作，以免弄乱堆栈。而在再次执行fork()并执行过execve()函数
// 后，被加载程序已不属于内核空间，因此可以使用写时复制技术了。

// fork()系统调用函数的定义
_syscall0(int, fork)
// pause()系统调用函数的定义
_syscall0(int, pause)
// int setup(void * BIOS)系统调用,仅用于linux初始化(仅在这个程序中被调用).
_syscall1(int, setup, void *, BIOS)
// int sync()系统调用：更新文件系统。
_syscall0(int, sync)

#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <string.h>

static char printbuf[1024];

extern char *strcpy();
extern int vsprintf();
extern void init(void);
extern void blk_dev_init(void);						/* 块设备初始化子程序(blk_drv/ll_rw_blk.c) */
extern void chr_dev_init(void);						/* 字符设备初始化(chr_drv/tty_io.c) */
extern void hd_init(void);							/* 硬盘初始化程序(blk_drv/hd.c) */
extern void floppy_init(void);						/* 软驱初始化程序(blk_drv/floppy.c) */
extern void mem_init(long start, long end);			/* 内存管理初始化(mm/memory.c) */
extern long rd_init(long mem_start, int length);	/* 虚拟盘初始化(blk_drv/ramdisk.c) */
extern long kernel_mktime(struct tm * tm);			/* 计算系统开机启动时间(秒) */

// fork系统调用函数,该函数作为static inline表示内联函数，主要用来在进程0里面创建进程1的时候内联，使进程0在生成进程1的时候
// 不使用自己的用户堆栈
static inline long fork_for_process0() {
	long __res;
	__asm__ volatile (
		"int $0x80\n\t"	/* 调用系统中断0x80 */
		: "=a" (__res)	/* 返回值->eax(__res) */
		: "0" (2));		/* 输入为系统中断调用号__NR_name */
	if (__res >= 0)		/* 如果返回值>=0,则直接返回该值 */
		return __res;
	errno = -__res; 	/* 否则置出错号,并返回-1 */
	return -1;
}

/* 内核专用sprintf()函数
 * 该函数用于产生格式化信息并输出到指定缓冲区str中
 * 参数'*fmt'指定输出将采用格式
 */
static int sprintf(char * str, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i = vsprintf(str, fmt, args);
	va_end(args);
	return i;
}

/*
 * This is set up by the setup-routine at boot-time
 */
/*
 * 以下这些数据是在内核引导期间由setup.s程序设置的.
 */
/* 下面三行分别将指定的线性地址强行转换为给定数据类型的指针,并获取指针所指内容.由于内核代码段被映射到从物理地址
 零开始的地方,因此这些纯属地址正好也是对应的物理地址. */
#define EXT_MEM_K (*(unsigned short *)0x90002)			/* 1MB以后的扩展内存大小(KB) */
#define CON_ROWS ((*(unsigned short *)0x9000e) & 0xff)	/* 选定的控制台屏幕行,列数 */
#define CON_COLS (((*(unsigned short *)0x9000e) & 0xff00) >> 8)
#define DRIVE_INFO (*((struct drive_info *)0x90080))	/* 硬盘参数表32字节内容 */
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)		/* 根文件系统所在设备号 */
#define ORIG_SWAP_DEV (*(unsigned short *)0x901FA)		/* 交换文件所在设备号 */

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly and this seems to work. I anybody has 
 * more info on the real-time clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */
/* 这段宏读取CMOS实时时钟信息.outb_p和inb_p是include/asm/io.h中定义的端口输入输出 */
#define CMOS_READ(addr) ({						\
	outb_p(0x80 | addr, 0x70);					\
	inb_p(0x71);								\
})

/* 将BCD码转换成二进制值 */
#define BCD_TO_BIN(val) ((val) = ((val)&15) + ((val) >> 4) * 10)

/* 该函数取CMOS实时钟信息作为开机时间,并保存到全局变量startup_time(秒)中.其中调用的函数kernel_mktime()用于计
 算从1970年1月1日0时起到开机当日经过的秒数,作为开机时间 */
static void time_init(void)
{
	struct tm time;
	/* CMOS的访问速度很慢.为了减小时间误差,在读取了下面循环中所有数值后,若此时CMOS中秒值了变化,那么就重新读
	 取所有值.这样内核就能把与CMOS时间误差控制在1秒之内 */
	do {
		time.tm_sec = CMOS_READ(0);				/* 当前时间秒值(均是BCD码值) */
		time.tm_min = CMOS_READ(2);				/* 当前分钟值 */
		time.tm_hour = CMOS_READ(4);			/* 当前小时值 */
		time.tm_mday = CMOS_READ(7);			/* 一月中的当天日期 */
		time.tm_mon = CMOS_READ(8);				/* 当前月份(1-12) */
		time.tm_year = CMOS_READ(9);			/* 当前年份 */
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);					/* 转换成进进制数值 */
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;								/* tm_mon中月份范围是0~11 */
	startup_time = kernel_mktime(&time);		/* 计算开机时间.kernel/mktime.c */
}

static long memory_end = 0;			/* 机器具有的物理内存容量(字节数) */
static long buffer_memory_end = 0;	/* 高速缓冲区末端地址 */
static long main_memory_start = 0;	/* 主内存(将用于分页)开始的位置 */
static char term[32];				/* 终端设置字符串(环境参数) */

/* 读取并执行/etc/rc文件时所使用的命令行参数和环境参数 */
static char * argv_rc[] = { "/bin/sh", NULL };		/* 调用执行程序时参数的字符串数组 */
static char * envp_rc[] = { "HOME=/", NULL ,NULL };	/* 调用执行程序时的环境字符串数组 */

/* 运行登录shell时所使用的命令行参数和环境参数 */
static char * argv[] = { "-/bin/sh",NULL };	/* 字符"-"是传递给shell程序sh的一个标志.通过识别该标志,
											 sh程序会作为登录shell执行.其执行过程与shell提示符下
											 执行sh不一样 */
static char * envp[] = { "HOME=/usr/root", NULL, NULL };

struct drive_info { char dummy[32]; } drive_info;	/* 用于存放硬盘参数表信息 */

/* 内核初始化主程序.初始化结束后将以任务0(idle任务即空闲任务)的身份运行 */
int main(void)		/* This really IS void, no error here. */
{					/* The startup routine assumes (well, ...) this */
#ifdef EM
	// 开启仿真协处理器
	__asm__("movl %cr0,%eax \n\t" \
	        "xorl $6,%eax \n\t" \
	        "movl %eax,%cr0");
#endif
	/*
	 * Interrupts are still disabled. Do necessary setups, then
	 * enable them
	 */
	/*
	 * 此时中断仍被禁止着,做完必要的设置后就将其开启.
	 */
	// 首先保存根文件系统设备和交换文件设备号,并根据setup.s程序中获取的信息设置控制台终端屏幕行,列数环境变量TERM,并用其设置初始init进程
	// 中执行etc/rc文件和shell程序使用的环境变量,以及复制内存0x90080处的硬盘表.
	// 其中ROOT_DEV已在前面包含进的include/linux/fs.h文件上被声明为extern_int
	// 而SWAP_DEV在include/linux/mm.h文件内也作了相同声明.这里mm.h文件并没有显式地列在本程序前部,因为前面包含进的include/linux/sched.h
	// 文件中已经含有它.
 	ROOT_DEV = ORIG_ROOT_DEV;										// ROOT_DEV定义在fs/super.c
 	SWAP_DEV = ORIG_SWAP_DEV;										// SWAP_DEV定义在mm/swap.c
   	sprintf(term, "TERM=con%dx%d", CON_COLS, CON_ROWS);
	envp[1] = term;
	envp_rc[1] = term;
    drive_info = DRIVE_INFO;										// 复制内存0x90080处的硬盘参数表.

	// 接着根据机器物理内存容量设置高速缓冲区和主内存的位置和范围.
	// 高速缓存末端地址->buffer_memory_end;机器内存容量->memory_end;主内存开始地址->main_memory_start.
	// 设置物理内存大小
	memory_end = (1 << 20) + (EXT_MEM_K << 10);						// 内存大小=1MB + 扩展内存(k)*1024字节.
	memory_end &= 0xfffff000;										// 忽略不到4KB(1页)的内存数.
	if (memory_end > 16 * 1024 * 1024)								// 如果内存量超过16MB,则按16MB计.
		memory_end = 16 * 1024 * 1024;
	// 根据物理内存的大小设置高速缓冲去的末端大小
	if (memory_end > 12 * 1024 * 1024) 								// 如果内存>12MB,则设置缓冲区末端=4MB
		buffer_memory_end = 4 * 1024 * 1024;
	else if (memory_end > 6 * 1024 * 1024)							// 否则若内存>6MB,则设置缓冲区末端=2MB
		buffer_memory_end = 2 * 1024 * 1024;
	else
		buffer_memory_end = 1 * 1024 * 1024;						// 否则则设置缓冲区末端=1MB
	// 根据高速缓冲区的末端大小设置主内存区的起始地址
	main_memory_start = buffer_memory_end;							// 主内存起始位置 = 缓冲区末端
	// 如果在Makefile文件中定义了内存虚拟盘符号RAMDISK,则初始化虚拟盘.此时主优点将减少.
	// 参见kernel/blk_drv/ramdisk.c.
#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK * 1024);
#endif
	// 以下是内核进行所有方面的初始化工作.
	mem_init(main_memory_start, memory_end);	// 主内存区初始化.(mm/memory.c)
	trap_init();								// 陷阱门(硬件中断向量)初始化.(kernel/traps.c)
	blk_dev_init();								// 块设备初始化.(blk_drv/ll_rw_blk.c)
	chr_dev_init();								// 字符设备初始化.(chr_drv/tty_io.c)
 	tty_init();									// tty初始化(chr_drv/tty_io.c)
	time_init();								// 设置开机启动时间.
 	sched_init();								// 调度程序初始化(加载任务0的tr,ldtr)(kernel/sched.c)
	buffer_init(buffer_memory_end);				// 缓冲管理初始化,建内存链表等.(fs/buffer.c)
	hd_init();									// 硬盘初始化.	(blk_drv/hd.c)
	floppy_init();								// 软驱初始化.	(blk_drv/floppy.c)
	sti();										// 所有初始化工作都完了,于是开启中断.
	// 打印内核初始化完毕
	Log(LOG_INFO_TYPE, "<<<<< Linux0.12 Kernel Init Finished, Ready Start Process0 >>>>>\n");
	// 下面过程通过在堆栈中设置的参数,利用中断返回指令启动任务0执行.
	move_to_user_mode();						// 移到用户模式下执行.(include/asm/system.h)
	if (!fork_for_process0()) {					/* we count on this going ok */
		init();									// 在新建的子进程(任务1即init进程)中执行.
	}
	/*
	 *   NOTE!!   For any other task 'pause()' would mean we have to get a
	 * signal to awaken, but task0 is the sole exception (see 'schedule()')
	 * as task 0 gets activated at every idle moment (when no other tasks
	 * can run). For task0 'pause()' just means we go check if some other
	 * task can run, and if not we return here.
	 */
	/*
	 * 注意!!对于任何其他任务,'pause()'将意味着我们必须等待收到一个信号才会返回就绪态,但任务0(task0)是唯一例外情况(参见'schedule()'),因为
	 * 任务0在任何空闲时间里都会被激活(当没有其他任务在运行时),因此对于任务0'pause()'仅意味着我们返回来查看是否有其他任务可以运行,如果没有的话
	 * 我们就回到这里,一直循环执行'pause()'.
	 */
	// pause()系统调用(kernel/sched.c)会把任务0转换成可中断等待状态,再执行调度函数.但是调度函数只要发现系统中没有其他任务可以运行时就会切换
	// 到任务0,是不信赖于任务0的状态.
	for(;;)
		__asm__("int $0x80"::"a" (__NR_pause):);					// 即执行系统调用pause().
}

// 下面函数产生格式化信息并输出到标准输出设备stdout(1),这里是指屏幕上显示.参数'*fmt'指定输出将采用的格式,参见标准C语言书籍.
// 该子程序正好是vsprintf如何使用的一个简单例子.该程序使用vsprintf()将格式化的字符串放入printbuf缓冲区,然后用write()将
// 缓冲区的内容输出到标准设备(1--stdout).vsprintf()函数的实现见kernel/vsprintf.c.
int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1, printbuf, i = vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

// 在main()中已经进行子系统初始化,包括内存管理,各种硬件设备和驱动程序.
// init()函数在任务0第1次创建的子进程(任务1)中.它首先对第一个将要执行
// 的程序(shell)的环境进行初始化,然后以登录shell方式加载程序并执行之.
void init(void)
{
	int pid, i, fd;
	// setup()是一个系统调用.用于读取硬盘参数和分区表信息并加载虚拟盘(若存在的话)和安装根文件系统设备.
	// 该函数用25行上的宏定义,对就函数是sys_setup(),
	// 在块设备子目录kernel/blk_drv/hd.c.
	setup((void *) &drive_info);
	/* 下面以读写访问方式打开设备"/dev/tty0",它对应终端控制台.由于这是第一次打开文件操作,因此产生的文件句柄号(文
	件描述符)肯定是0.该句柄是UNIX类操作系统默认的控制台标准输入句柄stdin.这里再把它以读和写的方式分别打开是为了
	复制产生标准输出(写)句柄stdout和标准出错输出句柄stderr.函数前面的"(void)"前缀用于表示强制函数无需返回值 */
	(void) open("/dev/tty1", O_RDWR, 0);
	(void) dup(0);			// 复制句柄,产生句柄1号--stdout标准输出设备.
	(void) dup(0);			// 复制句柄,产生句柄2号--stderr标准出错输出设备.
	// 进程1执行的用户级代码的开始
	printf("<<<<< Process 1 console fd = %d >>>>>\n", fd);
	// 下面打印缓冲区块数和总字节数,每块1024字节,以及主内存区空闲内存字节数.
	printf("<<<<< %d buffers = %d bytes buffer space >>>>>\n\r", NR_BUFFERS,
			NR_BUFFERS * BLOCK_SIZE);
	printf("<<<<< Free mem: %d bytes >>>>>\n\r", memory_end - main_memory_start);
	// 下面fork()用于创建一个子进程(任务2).对于被创建的子进程,fork()将返回0值,对于原进程(父进程)则返回子进程的进程号pid.所以第202--206行是子进程执行的内容.
	// 该子进程关闭了句柄0(stdin),以只读方式打开/etc/rc文件,并使用execve()函数将进程自身替换成/bin/sh程序(即shell程序),然后执行/bin/sh程序.所携带的参数
	// 和环境变量分别由argv_rc和envp_rc数组给出.关闭句柄0并立刻打开/etc/rc文件的作用是把标准输入stdin重定向到/etc/rc/文件.这样shell程序/bin/sh就可以运行
	// rc文件中设置的命令.由于这里sh的运行方式是非交互式的,因此在执行完rc文件中的命令后就会立刻退出,进程2也随之结束.并于execve()函数说明请参见fs/exec.c程序.
	// 函数_exit()退出时的出错码1 - 操作未许可;2 -- 文件或目录不存在.
	if (!(pid = fork())) {
		close(0);
		if (open("/etc/rc", O_RDONLY, 0))
			_exit(1);												// 若打开文件失败,则退出(lib/_exit.c).
		execve("/bin/sh", argv_rc, envp_rc);						// 替换成/bin/sh程序并执行.
		_exit(2);													// 若execve()执行失败则退出.
    }
	// 下面还是父进程（1）执行的语句。wait()等待子进程停止或终止，返回值应是子进程的进程号（pid)。这三句的作用是父进程等待子进程
	// 的结束。&i是存放返回状态信息的位置。如果wait()返回值不等于子进程号，则继续等待。
  	if (pid > 0)
		while (pid != wait(&i));
	// 如果执行到这里，说明刚创建的子进程的执行已停止或终止了。下面循环中首先再创建一个子进程，如果出错，则显示“初始化程序创建子进程
	// 失败”信息并继续执行。对于所创建的子进程将关闭所有以前还遗留的句柄（stdin、stdout、stderr），新创建一个会话并设置进程组号，
	// 然后重新打开/dev/tty0作为stdin，并复制成stdout和stderr。再次执行系统解释程序/bin/sh。但这次执行所选用的参数和环境数组另
	// 选了一套。然后父进程再次运行wait()等等。如果子进程又停止了执行，则在标准输出上显示出错信息“子进程pid停止了运行，返回码是i”，
	// 然后继续重试下去...，形成“大”死循环。
	while (1) {
		if ((pid = fork()) < 0) {
			printf("Fork failed in init %c\r\n", ' ');
			continue;
		}
		if (!pid) {                             					// 新的子进程。
			close(0); close(1); close(2);
			setsid();                       						// 创建一新的会话期，见后面说明。
			(void) open("/dev/tty1", O_RDWR, 0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh", argv, envp));
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r", pid, i);
		sync();
	}
	_exit(0);														/* NOTE! _exit, not exit() */
}


