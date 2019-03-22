/*
 *  linux/fs/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <utime.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>

#include <asm/segment.h>

// 取文件系统信息
// 该系统调用用于返回已安装（mounted）文件系统的统计信息。
// 参数	dev		含有用户已安装文件系统的设备号
//		ubuf	一个ustat结构缓冲区指针，用于存放系统返回的文件系统信息。
// 成功时返回0，并且ubuf指向的ustate结构被添入文件系统总空闲块和空闲i节点数。
int sys_ustat(int dev, struct ustat * ubuf)
{
	return -ENOSYS;		/* 出错码：功能还未实现 */
}

// 设置文件访问和修改时间
// 参数	filename	文件名
//		times		访问和修改时间结构指针
// 如果times指针不为NULL，则取utimbuf结构中的时间信息来设置文件的访问和修改时间。
// 如果times指针是NULL，则取系统当前时间来设置指定文件的访问和修改时间域。
int sys_utime(char * filename, struct utimbuf * times)
{
	struct m_inode * inode;
	long actime, modtime;

	if (!(inode = namei(filename))) {
		return -ENOENT;
	}
	
	if (times) {
		actime = get_fs_long((unsigned long *) &times->actime);
		modtime = get_fs_long((unsigned long *) &times->modtime);
	} else {
		actime = modtime = CURRENT_TIME;
	}
	/* 然后修改i节点中的访问时间字段和修改时间字段。再设置i节点已修改标志，放回该i节点，并返回0。*/
	inode->i_atime = actime;
	inode->i_mtime = modtime;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

/*
 * XXX should we use the real or effective uid?  BSD uses the real uid,
 * so as to make this call useful to setuid programs.
 */
/*
 * XXX我们该用真实用户id（ruid）还是有效有户id（euid）？BSD系统使用了真实用户id，以使该调用可以供setuid程序
 * 使用。
 * （注：POSIX标准建议使用真实用户ID）。
 * （注1：英文注释开始的‘XXX’表示重要提示）。
 */

// 检查文件的访问权限
// 参数	filename	文件名
//		mode		检查的访问属性，它有3个有效位组成：R_OK（4）、W_OK（2）、X_OK（1）和F_OK（0）组
//					成，分别表示检测文件是否可读、可写、可执行和文件是否存在。
// 如果访问允许的话，则返回0,否则返回出错码。
int sys_access(const char * filename, int mode)
{
	struct m_inode * inode;
	int res, i_mode;

	mode &= 0007;
	if (!(inode = namei(filename))) {
		return -EACCES;
	}
	i_mode = res = inode->i_mode & 0777;
	iput(inode);	// 放在太前面了吧，下面还在用呢
	if (current->uid == inode->i_uid) {
		res >>= 6;
	} else if (current->gid == inode->i_gid) {
		res >>= 3;
	}
	if ((res & 0007 & mode) == mode) {
		return 0;
	}
	/*
	 * XXX we are doing this test last because we really should be
	 * swapping the effective with the real user id (temporarily),
	 * and then calling suser() routine.  If we do call the
	 * suser() routine, it needs to be called last.
	 */
    /*
     * XXX我们最后才做下面的测试，因为我们实际上需要交换有效用户ID和真实用户ID（临时地），然后才调用
     * suser()函数，如果我们确实要调用suser()函数，则需要最后才被调用。
     */
	// 如果当前用户ID为0（超级用户）并且屏蔽码执行位是0或者文件可以被任何人执行、搜索，则返回0。否则
	// 返回出错码。
	if ((!current->uid) && (!(mode & 1) || (i_mode & 0111))) {
		return 0;
	}
	return -EACCES;
}

// 改变当前工作目录系统调用
// 参数	filename	目录名
// 操作成功则返回0,否则返回出错码。
int sys_chdir(const char * filename)
{
	struct m_inode * inode;

	/* 改变当前工作目录就是要求把进程任务结构的当前工作目录字段指向给定目录名的i节点 */
	if (!(inode = namei(filename))) {
		return -ENOENT;	// 出错码：文件或目录不存在
	}
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;// 出错码：不是目录名
	}
	/* 然后释放进程原工作目录i节点，并使其指向新设置的工作目录i节点。返回0 */
	iput(current->pwd);
	current->pwd = inode;
	return (0);
}

// 改变根目录系统调用。
// 把指定的目录名设置成为当前进程的根目录“/”。
// 如果操作成功则返回0，否则返回出错码。
int sys_chroot(const char * filename)
{
	struct m_inode * inode;

	if (!(inode = namei(filename))) {
		return -ENOENT;
	}
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	// 然后释放当前进程的根目录，并重新设置为指定目录名的i节点，返回0。
	iput(current->root);
	current->root = inode;
	return (0);
}

// 修改文件属性系统调用
// 参数	filename	文件名
//		mode		新的文件属性
int sys_chmod(const char * filename, int mode)
{
	struct m_inode * inode;

	if (!(inode = namei(filename))) {
		return -ENOENT;
	}
	if ((current->euid != inode->i_uid) && !suser()) {
		iput(inode);
		return -EACCES;
	}
	/* 否则就重新设置该i节点的文件属性，并置该i节点已修改标志。放回该i节点，返回0 */
	inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

// 修改文件宿主(只有root用户能操作)
// 参数	filename	文件名
//		uid			用户标识符（用户ID）
//		gid			组ID
// 若操作成功则返回0，否则返回出错码。
int sys_chown(const char * filename, int uid, int gid)
{
	struct m_inode * inode;

	if (!(inode = namei(filename))) {
		return -ENOENT;
	}
	// 如果当前进程不是超级用户，则放回该i节点，并返回出错码（没有访问权限）
	if (!suser()) {
		iput(inode);
		return -EACCES;
	}
	// 否则我们就用参数提供的值来设置文件i节点的用户ID和组ID，并置i节点已经修改标志，放回该i节点，返回0。
	inode->i_uid = uid;
	inode->i_gid = gid;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

// 检查字符设备类型
// 该函数仅用于下面文件打开系统调用sys_open(),用于检查若打开的文件是tty终端字符设备时,需要对当前进程的
// 设置和对tty表的设置.
// 返回0检测处理成功,返回-1表示失败,对应字符设备不能打开.
static int check_char_dev(struct m_inode * inode, int dev, int flag)
{
	struct tty_struct *tty;
	int min;		// 子设备号.

	// 只处理主设备号是4(/dev/ttyxx文件)或5(/dev/tty文件)的情况./dev/tty的子设备号是0.如果一个进程有
	// 控制终端,则它是进程控制终端设备的同义名.即/dev/tty设备是一个虚拟设备,它对应到进程实际使用的
	// /dev/ttyxx设备之一.对于一个进程来说,若其有控制终端,那么它的任务结构中的tty字段将是4号设备的某
	// 一个子设备号.
	// 如果打开操作的文件是/dev/tty(即MAJOR(dev) = 5),那么我们令min = 进程任务结构中的tty字段,即取4号
	// 设备的子设备号.否则如果打开的是某个4号设备,则直接取其子设备号.如果得到的4号设备子设备号小于0,
	// 那么说明进程没有控制终端,或者设备号错误,则返回-1,表示由于进程没有控制终端或者不能打开这个设备.
	if (MAJOR(dev) == 4 || MAJOR(dev) == 5) {
		if (MAJOR(dev) == 5) {
			min = current->tty;
		} else {
			min = MINOR(dev);
		}
		if (min < 0) {
			return -1;
		}
		// 主伪终端设备文件只能被进程独占使用.如果子设备号表明是一个主伪终端,并且该打开文件i节点引用
		// 计数大于1,则说明该设备已被其他进程使用.因此不能再打开该字符设备文件,于是返回-1.否则,我们
		// 让tty结构指针tty指向tty表中对应结构项.若打开文件操作标志flag中不含无需控制终端标志O_NOCTTY,
		// 并且进程是进程组首领,并且当前进程没有控制终端,并且tty结构中session字段为0(表示该终端还不是
		// 任何进程组的控制终端),那么就允许为进程设置这个终端设备min为其控制终端.于是设置进程任务结构
		// 终端设备号字段tty值等于min,并且设置对应tty结构的会话号session和进程组号pgrp分别等于进程的会
		// 话号和进程组号.
		if ((IS_A_PTY_MASTER(min)) && (inode->i_count > 1)) {
			return -1;
		}
		tty = TTY_TABLE(min);
		// Log(LOG_INFO_TYPE, "<<<<< tty index = %d>>>>>\n", min);
		if (!(flag & O_NOCTTY) &&
		    current->leader &&
		    current->tty < 0 &&
		    tty->session == 0) {
			current->tty = min;
			tty->session = current->session;
			tty->pgrp = current->pgrp;
		}
		// 如果打开文件操作标志flag中含有O_NONBLOCK(非阻塞)标志,则我们需要对该字符终端设备进行相关设置,设置为满足读操作需要读取的最少字符数为0,设置超时
		// 定时值为0,并把终端设备设置成非规范模式.非阻塞方式只能工作于非规范模式.在此模式下当VMIN和VTIME均设置为0时,辅助队列中有多少支进程就读取多少字符,
		// 并立刻返回.
		if (flag & O_NONBLOCK) {
			TTY_TABLE(min)->termios.c_cc[VMIN] = 0;
			TTY_TABLE(min)->termios.c_cc[VTIME] = 0;
			TTY_TABLE(min)->termios.c_lflag &= ~ICANON;
		}
	}
	return 0;
}

// 打开(或创建)文件系统调用
// 参数	filename	文件名
//		flag		打开文件标志，O_RDONLY，WRONLY，O_RDWR，O_CREAT，O_EXCL，O_APPEND或标志的组合
//		mode		用于指定文件的许可属性.这些属性有S_IRWXU,S_IRUSR,S_IRWXG等.
// 对于新创建的文件,这些属性只应用于将来对文件的访问,创建了只读文件的打开调用也将返回一个读写的文件句柄.
// 返回	如果调用操作成功,则返回文件句柄(文件描述符),否则返回出错码.
int sys_open(const char * filename, int flag, int mode)
{
	struct m_inode * inode;
	struct file * f;
	int i, fd;

	// 首先对参数进行处理.将用户设置的文件模式和进程模式屏蔽码相与,产适配器的文件模式.为了为打开文件建
	// 立一个文件句柄,需要搜索进程结构中文件结构指针数组,以查找一个空闲项.空闲项的索引号fd即是句柄值.若
	// 已经没有空闲项,则返回出错码(参数无效).
	mode &= 0777 & ~current->umask;
	for(fd = 0 ; fd < NR_OPEN ; fd++) {
		if (!current->filp[fd]) {
			break;	// 找到空闲项.
		}
	}
	if (fd >= NR_OPEN) {
		return -EINVAL;	// 这里应该返回-ENFILE吧
	}
	// 然后我们设置当前进程的执行时关闭文件句柄(close_on_exec)位图,复位对应的位。close_on_exec是一个进程
	// 所有文件句柄的位图标志.每个位代表一个打开着的文件描述符,用于确定调用系统调用execve()时需要关闭的
	// 文件句柄.当程序使用fork()函数创建一个子进程时,通常会在该子进程中调用execve()函数加载执行另一个新
	// 程序.此时子进程中开始执行新程序.若一个文件句柄close_on_exec中的对应位被置位,那么在执行execve()时
	// 该对应文件句柄将被关闭,否则该文件句柄将始终处于打开状态.当打开一个文件时,默认情况下文件句柄在子
	// 进程中也处于打开状态.因此这里要复位对应位.然后为打开文件在文件表中寻找一个空闲结构项.我们令f指向
	// 文件表数组开始处.搜索空闲文件结构项(引用计数为0的项),若已经没有空闲文件表结构项,则返回出错码.另
	// 外,第184行上的指针赋值"0+file_table"等同于"file_table"和"&file_table[0]"
	// 不过这样写可能更能明了一些.
	current->close_on_exec &= ~(1 << fd);           // 复位对应文件打开位
	f = 0 + file_table;
	for (i = 0 ; i < NR_FILE ; i++, f++)
		if (!f->f_count) break;         			// 在文件表中找到空闲结构项。
	if (i >= NR_FILE)
		return -EINVAL;
	// 此时我们让进程对应文件句柄fd的文件结构指针指向搜索到的文件结构,并令文件引用计数递增1.然后调用函
	// 数open_namei()执行打开操作,若返回值小于0,则说明出错,于是释放刚申请到的文件结构,返回出错码i.若文
	// 件打开操作成功,则inode是已打开文件的i节点指针.
	(current->filp[fd] = f)->f_count++;
	if ((i = open_namei(filename, flag, mode, &inode)) < 0) {
		current->filp[fd] = NULL;
		f->f_count = 0;
		return i;
	}
	// 根据已打开文件i节点的属性字段,我们可以知道文件的类型.对于不同类型的文件,我们需要作一些特别处理.如
	// 果打开的是字符设备文件,那么我们就要调用check_char_dev()函数来检查当前进程是否能打开这个字符设备文
	// 件.如果允许(函数返回0),那么在check_char_dev()中会根据具体文件打开标志为进程设置控制终端.如果不允许
	// 打开使用该字符设备文件,那么我们只能释放上面申请的文件项和句柄资源.返回出错码.
	/* ttys are somewhat special (ttyxx major==4, tty major==5) */
	if (S_ISCHR(inode->i_mode))
		if (check_char_dev(inode, inode->i_zone[0], flag)) {
			iput(inode);
			current->filp[fd] = NULL;
			f->f_count = 0;
			return -EAGAIN;	// 出错号:资源暂不可用.
		}
	// 如果打开的是块设备文件,则检查盘片是否更换过.若更换过则需要让高速缓冲区中该设备的所有缓冲块失效.
	/* Likewise with block-devices: check for floppy_change */
	/* 同样对于块设备文件:需要检查盘片是否被更换 */
	if (S_ISBLK(inode->i_mode))
		check_disk_change(inode->i_zone[0]);
	// 现在我们初始化打开文件的文件结构.设置文件结构属性和标志,置句柄引用计数为1,并设置i节点字段为打开文
	// 件的i节点,初始化文件读写指针为0.最后返回文件句柄号.
	f->f_mode = inode->i_mode;
	f->f_flags = flag;
	f->f_count = 1;
	f->f_inode = inode;
	f->f_pos = 0;
	return (fd);
}

// 创建文件系统调用
// 参数	pathname是路径名
//		mode与上面的sys_open()函数相同
// 成功则返回文件句柄，否则返回出错码
int sys_creat(const char * pathname, int mode)
{
	return sys_open(pathname, O_CREAT | O_TRUNC, mode);
}

// 关闭文件系统调用.
// 参数	fd		文件句柄.
// 成功则返回0,否则返回出错码.
int sys_close(unsigned int fd)
{
	struct file* filp;

	/* 1. 参数有效性的判断 */
	if (fd >= NR_OPEN) {
		return -EINVAL;
	}

	/* 2. 修改当前进程中的结构体参数 */
	current->close_on_exec &= ~(1 << fd);
	
	if (!(filp = current->filp[fd])) {
		return -EINVAL;
	}
	current->filp[fd] = NULL;

	/* 3. 对关闭的文件进行判断 */
	if (filp->f_count == 0) {
		panic("Close: file count is 0");
	}
	if (--filp->f_count) {
		return (0);
	}
	/* 如果引用计数已等于0,说明该文件已经没有进程引用,该文件结构已变为空闲.则释放该文件i节点,返回0 */
	iput(filp->f_inode);
	return (0);
}
