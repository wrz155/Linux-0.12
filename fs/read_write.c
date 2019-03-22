/*
 *  linux/fs/read_write.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/segment.h>

// 字符设备读写函数。fs/char_dev.c。
extern int rw_char(int rw, int dev, char * buf, int count, off_t * pos);
// 读管道操作函数。fs/pipe.c
extern int read_pipe(struct m_inode * inode, char * buf, int count);
// 写管道操作函数。fs/pipe.c
extern int write_pipe(struct m_inode * inode, char * buf, int count);
// 块设备读操作函数。fs/block_dev.c
extern int block_read(int dev, off_t * pos, char * buf, int count);
// 块设备写操作函数。fs/block_dev.c
extern int block_write(int dev, off_t * pos, char * buf, int count);
// 读文件操作函数。fs/file_dev.c
extern int file_read(struct m_inode * inode, struct file * filp, char * buf, int count);
// 写文件操作函数。fs/file_dev.c
extern int file_write(struct m_inode * inode, struct file * filp, char * buf, int count);

// 重定位文件读写指针系统调用。
// 参数	fd		文件句柄
//		offset	新的文件读写指针偏移值
//		origin	偏移的起始位置，三种选择：SEEK_SET(0,文件开始处)|SEEK_CUR(1,当前读写位置)|SEEK_END(2,文件尾处)
int sys_lseek(unsigned int fd, off_t offset, int origin)
{
	struct file * file;
	int tmp;

	// 如果文件句柄大于程序最多打开文件数NR_OPEN（20），或者该句柄的文件结构指针为空，或者对应文件结构的i节点字
	// 段为空，或者指定设备文件指针是不可定位的，则返回出错码并退出。
	if (fd >= NR_OPEN || !(file = current->filp[fd]) || !(file->f_inode)
	   || !IS_SEEKABLE(MAJOR(file->f_inode->i_dev))) {
		return -EBADF;
	}
	// 如果文件对应i节点是管道节点，则返回出错码退出。因为管道头尾指针不可随意移动！
	if (file->f_inode->i_pipe) {
		return -ESPIPE;
	}
	switch (origin) {
		case SEEK_SET: /* 以文件起始处作为原点设置文件读写指针 */
			if (offset < 0) {
				return -EINVAL;
			}
			file->f_pos = offset;
			break;
		case SEEK_CUR: /* 以文件当前读写指针处作为原点重定位读写指针 */
			if (file->f_pos + offset < 0) {
				return -EINVAL;
			}
			file->f_pos += offset;
			break;
		case SEEK_END: /* 以文件末尾作为原点重定位读写指针 */
			if ((tmp = file->f_inode->i_size + offset) < 0) {
				return -EINVAL;
			}
			file->f_pos = tmp;
			break;
		default:
			return -EINVAL;
	}
	return file->f_pos;
}

// 读文件系统调用。
// 参数	fd		文件句柄
//		buf		缓冲区
//		count	欲读字节数
int sys_read(unsigned int fd, char * buf, int count)
{
	struct file * file;
	struct m_inode * inode;

	// 进程文件句柄值大于程序最多打开文件数NR_OPEN,或者需要写入的字节计数小于0,或者该句柄的文件结构指针为空,
	// 则返回出错码并退出.如果需读取的字节数count等于0,则返回0退出.
	if (fd >= NR_OPEN || count < 0 || !(file = current->filp[fd])) {
		return -EINVAL;
	}
	if (!count) {
		return 0;
	}
	// 然后验证存放数据的缓冲区内存限制，并取文件的i节点。用于根据该i节点的属性，分别调用相应的读操作函数
	verify_area(buf, count);
	inode = file->f_inode;
	// 管道文件的读操作
	if (inode->i_pipe) {
		return (file->f_mode & 1) ? read_pipe(inode, buf, count) : -EIO;
	}
	// 字符设备的读操作
	if (S_ISCHR(inode->i_mode)) {
		return rw_char(READ, inode->i_zone[0], buf, count, &file->f_pos);
	}
	// 块设备的读操作
	if (S_ISBLK(inode->i_mode)) {
		return block_read(inode->i_zone[0], &file->f_pos, buf, count);
	}
	// 如果是目录文件或者是常规文件，则首先验证读取字节数count的有效性并进行调整（若读取字节数加上文件当前读
	// 写指针值大于文件长度，则重新设置读取字节数为文件长度-当前读写指针值，若读取数等于0,则返回0退出），然后
	// 执行文件读操作，返回读取的字节数并退出。
	if (S_ISDIR(inode->i_mode) || S_ISREG(inode->i_mode)) {
		if (count + file->f_pos > inode->i_size)
			count = inode->i_size - file->f_pos;
		if (count <= 0) {
			return 0;
		}
		return file_read(inode, file, buf, count);
	}
	// 执行到这里，说明我们无法判断文件的属性。则打印节点文件的属性，并返回出错码退出。
	printk("(Read)inode->i_mode=%06o\n\r", inode->i_mode);
	return -EINVAL;
}

// 写文件系统调用
// 参数	fd		文件句柄
//		buf		用户缓冲区
//		count	欲写字节数
int sys_write(unsigned int fd, char * buf, int count)
{
	struct file * file;
	struct m_inode * inode;

	// 同样地,我们首先判断函数参数的有效性.如果进程文件句柄值大于程序最多打开文件数NR_OPEN,或者需要写入的字节
	// 计数小于0,或者该句柄的文件结构指针为空,则返回出错码并退出.如果需读取的字节数count等于0,则返回0退出.
	if (fd >= NR_OPEN || count < 0 || !(file = current->filp[fd])) {
		return -EINVAL;
	}
	if (!count) {
		return 0;
	}
	// 然后验证存放数据的缓冲区内存限制.并取文件的i节点.根据该i节点的属性,分别调用相应的写操作函数
	inode = file->f_inode;
	// 管道的写操作
	if (inode->i_pipe) {
		return (file->f_mode & 2) ? write_pipe(inode, buf, count) : -EIO;
	}
	/* 字符设备的写操作 */
	if (S_ISCHR(inode->i_mode)) {
		return rw_char(WRITE, inode->i_zone[0], buf, count, &file->f_pos);
	}
	/* 块设备的写操作 */
	if (S_ISBLK(inode->i_mode)) {
		return block_write(inode->i_zone[0], &file->f_pos, buf, count);
	}
	/* 文件的写操作 */
	if (S_ISREG(inode->i_mode)) {
		return file_write(inode, file, buf, count);
	}
	/* 执行到这里,说明我们无法判断文件的属性.则打印节点文件属性,并返回出错码退出 */
	printk("(Write)inode->i_mode=%06o\n\r", inode->i_mode);
	return -EINVAL;
}
