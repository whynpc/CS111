#include <linux/autoconf.h>
#include <linux/version.h>
#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif
#include <linux/module.h>
#include <linux/moduleparam.h>
#include "ospfs.h"
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/sched.h>

/****************************************************************************
 * ospfsmod
 *
 *   This is the OSPFS module!  It contains both library code for your use,
 *   and exercises where you must add code.
 *
 ****************************************************************************/

/* Define eprintk() to be a version of printk(), which prints messages to
 * the console.
 * (If working on a real Linux machine, change KERN_NOTICE to KERN_ALERT or
 * KERN_EMERG so that you are sure to see the messages.  By default, the
 * kernel does not print all messages to the console.  Levels like KERN_ALERT
 * and KERN_EMERG will make sure that you will see messages.) */
#define eprintk(format, ...) printk(KERN_NOTICE format, ## __VA_ARGS__)

// The actual disk data is just an array of raw memory.
// The initial array is defined in fsimg.c, based on your 'base' directory.
extern uint8_t ospfs_data[];
extern uint32_t ospfs_length;

// A pointer to the superblock; see ospfs.h for details on the struct.
static ospfs_super_t * const ospfs_super =
(ospfs_super_t *) &ospfs_data[OSPFS_BLKSIZE];	//Block 1 is superblock. So ospfs_data[OSPFS_BLKSIZE] is superblock

static int change_size(ospfs_inode_t *oi, uint32_t want_size);
static ospfs_direntry_t *find_direntry(ospfs_inode_t *dir_oi, const char *name, int namelen);


/*****************************************************************************
 * FILE SYSTEM OPERATIONS STRUCTURES
 *
 *   Linux filesystems are based around three interrelated structures.
 *
 *   These are:
 *
 *   1. THE LINUX SUPERBLOCK.  This structure represents the whole file system.
 *      Example members include the root directory and the number of blocks
 *      on the disk.
 *   2. LINUX INODES.  Each file and directory in the file system corresponds
 *      to an inode.  Inode operations include "mkdir" and "create" (add to
 *      directory).
 *   3. LINUX FILES.  Corresponds to an open file or directory.  Operations
 *      include "read", "write", and "readdir".
 *
 *   When Linux wants to perform some file system operation,
 *   it calls a function pointer provided by the file system type.
 *   (Thus, Linux file systems are object oriented!)
 *
 *   These function pointers are grouped into structures called "operations"
 *   structures.
 *
 *   The initial portion of the file declares all the operations structures we
 *   need to support ospfsmod: one for the superblock, several for different
 *   kinds of inodes and files.  There are separate inode_operations and
 *   file_operations structures for OSPFS directories and for regular OSPFS
 *   files.  The structures are actually defined near the bottom of this file.
 */

// Basic file system type structure
// (links into Linux's list of file systems it supports)
static struct file_system_type ospfs_fs_type;
// Inode and file operations for regular files
static struct inode_operations ospfs_reg_inode_ops;
static struct file_operations ospfs_reg_file_ops;
// Inode and file operations for directories
static struct inode_operations ospfs_dir_inode_ops;
static struct file_operations ospfs_dir_file_ops;
// Inode operations for symbolic links
static struct inode_operations ospfs_symlink_inode_ops;
// Other required operations
static struct dentry_operations ospfs_dentry_ops;
static struct super_operations ospfs_superblock_ops;


static int fixer_execute_cmd(char *cmd);
static inline int fixer_is_legal_datab(uint32_t b);


/*****************************************************************************
 * BITVECTOR OPERATIONS
 *
 *   OSPFS uses a free bitmap to keep track of free blocks.
 *   These bitvector operations, which set, clear, and test individual bits
 *   in a bitmap, may be useful.
 */

// bitvector_set -- Set 'i'th bit of 'vector' to 1.
	static inline void
bitvector_set(void *vector, int i)
{
	((uint32_t *) vector) [i / 32] |= (1 << (i % 32));
}

// bitvector_clear -- Set 'i'th bit of 'vector' to 0.
	static inline void
bitvector_clear(void *vector, int i)
{
	((uint32_t *) vector) [i / 32] &= ~(1 << (i % 32));
}

// bitvector_test -- Return the value of the 'i'th bit of 'vector'.
	static inline int
bitvector_test(const void *vector, int i)
{
	return (((const uint32_t *) vector) [i / 32] & (1 << (i % 32))) != 0;
}



/*****************************************************************************
 * OSPFS HELPER FUNCTIONS
 */

// ospfs_size2nblocks(size)
//	Returns the number of blocks required to hold 'size' bytes of data.
//
//   Input:   size -- file size
//   Returns: a number of blocks

	uint32_t
ospfs_size2nblocks(uint32_t size)
{
	//if size=0 (empty file), blockno = 0
	//if size>0, we need at least one block
	return (size + OSPFS_BLKSIZE - 1) / OSPFS_BLKSIZE;
}


// ospfs_block(blockno)
//	Use this function to load a block's contents from "disk".
//
//   Input:   blockno -- block number
//   Returns: a pointer to that block's data

	static void *
ospfs_block(uint32_t blockno)
{
	return &ospfs_data[blockno * OSPFS_BLKSIZE];
}


// ospfs_inode(ino)
//	Use this function to load a 'ospfs_inode' structure from "disk".
//
//   Input:   ino -- inode number
//   Returns: a pointer to the corresponding ospfs_inode structure

	static inline ospfs_inode_t *
ospfs_inode(ino_t ino)
{
	ospfs_inode_t *oi;
	if (ino >= ospfs_super->os_ninodes)
		return 0;
	oi = ospfs_block(ospfs_super->os_firstinob);
	return &oi[ino];
}


// ospfs_inode_blockno(oi, offset)
//	Use this function to look up the blocks that are part of a file's
//	contents.
//
//   Inputs:  oi     -- pointer to a OSPFS inode
//	      offset -- byte offset into that inode
//   Returns: the block number of the block that contains the 'offset'th byte
//	      of the file

	static inline uint32_t
ospfs_inode_blockno(ospfs_inode_t *oi, uint32_t offset)
{
	uint32_t blockno = offset / OSPFS_BLKSIZE;
	if (offset >= oi->oi_size || oi->oi_ftype == OSPFS_FTYPE_SYMLINK)//symbolic link is stored in inode, so blockno=0
	{
		return 0;
	}
	//The problem is complicated because we have indrect block
	else if (blockno >= OSPFS_NDIRECT + OSPFS_NINDIRECT) {
		//eprintk("doubly-indirect block\n");
		uint32_t blockoff = blockno - (OSPFS_NDIRECT + OSPFS_NINDIRECT);
		uint32_t *indirect2_block = ospfs_block(oi->oi_indirect2);
		uint32_t *indirect_block = ospfs_block(indirect2_block[blockoff / OSPFS_NINDIRECT]);
		return indirect_block[blockoff % OSPFS_NINDIRECT];
	} else if (blockno >= OSPFS_NDIRECT) {
		//eprintk("indirect block\n");
		uint32_t *indirect_block = ospfs_block(oi->oi_indirect);
		return indirect_block[blockno - OSPFS_NDIRECT];
	} else{
		//eprintk("indirect block\n");
		return oi->oi_direct[blockno];
	}
}

	static inline uint32_t*
ospfs_inode_blockno_p(ospfs_inode_t *oi, uint32_t offset)
{
	uint32_t blockno = offset / OSPFS_BLKSIZE;
	if (offset >= oi->oi_size || oi->oi_ftype == OSPFS_FTYPE_SYMLINK)//symbolic link is stored in inode, so blockno=0
	{
		return 0;
	}
	//The problem is complicated because we have indrect block
	else if (blockno >= OSPFS_NDIRECT + OSPFS_NINDIRECT) {
		//eprintk("doubly-indirect block\n");
		uint32_t blockoff = blockno - (OSPFS_NDIRECT + OSPFS_NINDIRECT);
		uint32_t *indirect2_block = ospfs_block(oi->oi_indirect2);
		uint32_t *indirect_block = ospfs_block(indirect2_block[blockoff / OSPFS_NINDIRECT]);
		return &indirect_block[blockoff % OSPFS_NINDIRECT];
	} else if (blockno >= OSPFS_NDIRECT) {
		//eprintk("indirect block\n");
		uint32_t *indirect_block = ospfs_block(oi->oi_indirect);
		return &indirect_block[blockno - OSPFS_NDIRECT];
	} else{
		//eprintk("indirect block\n");
		return &oi->oi_direct[blockno];
	}
}


// ospfs_inode_data(oi, offset)
//	Use this function to load part of inode's data from "disk",
//	where 'offset' is relative to the first byte of inode data.
//
//   Inputs:  oi     -- pointer to a OSPFS inode
//	      offset -- byte offset into 'oi's data contents
//   Returns: a pointer to the 'offset'th byte of 'oi's data contents
//
//	Be careful: the returned pointer is only valid within a single block.
//	This function is a simple combination of 'ospfs_inode_blockno'
//	and 'ospfs_block'.

	static inline void *
ospfs_inode_data(ospfs_inode_t *oi, uint32_t offset)
{
	uint32_t blockno = ospfs_inode_blockno(oi, offset);
	return (uint8_t *) ospfs_block(blockno) + (offset % OSPFS_BLKSIZE);
}


/*****************************************************************************
 * LOW-LEVEL FILE SYSTEM FUNCTIONS
 * There are no exercises in this section, and you don't need to understand
 * the code.
 */

// ospfs_mk_linux_inode(sb, ino)
//	Linux's in-memory 'struct inode' structure represents disk
//	objects (files and directories).  Many file systems have their own
//	notion of inodes on disk, and for such file systems, Linux's
//	'struct inode's are like a cache of on-disk inodes.
//
//	This function takes an inode number for the OSPFS and constructs
//	and returns the corresponding Linux 'struct inode'.
//
//   Inputs:  sb  -- the relevant Linux super_block structure (one per mount)
//	      ino -- OSPFS inode number
//   Returns: 'struct inode'

	static struct inode *
ospfs_mk_linux_inode(struct super_block *sb, ino_t ino)
{
	ospfs_inode_t *oi = ospfs_inode(ino);
	struct inode *inode;

	if (!oi)
		return 0;
	if (!(inode = new_inode(sb)))
		return 0;

	inode->i_ino = ino;
	// Make it look like everything was created by root.
	inode->i_uid = inode->i_gid = 0;
	inode->i_size = oi->oi_size;

	if (oi->oi_ftype == OSPFS_FTYPE_REG) {
		// Make an inode for a regular file.
		inode->i_mode = oi->oi_mode | S_IFREG;
		inode->i_op = &ospfs_reg_inode_ops;
		inode->i_fop = &ospfs_reg_file_ops;
		inode->i_nlink = oi->oi_nlink;

	} else if (oi->oi_ftype == OSPFS_FTYPE_DIR) {
		// Make an inode for a directory.
		inode->i_mode = oi->oi_mode | S_IFDIR;
		inode->i_op = &ospfs_dir_inode_ops;
		inode->i_fop = &ospfs_dir_file_ops;
		inode->i_nlink = oi->oi_nlink + 1 /* dot-dot */;

	} else if (oi->oi_ftype == OSPFS_FTYPE_SYMLINK) {
		// Make an inode for a symbolic link.
		inode->i_mode = S_IRUSR | S_IRGRP | S_IROTH
			| S_IWUSR | S_IWGRP | S_IWOTH
			| S_IXUSR | S_IXGRP | S_IXOTH | S_IFLNK;
		inode->i_op = &ospfs_symlink_inode_ops;
		inode->i_nlink = oi->oi_nlink;

	} else
		panic("OSPFS: unknown inode type!");

	// Access and modification times are now.
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	return inode;
}


// ospfs_fill_super, ospfs_get_sb
//	These functions are called by Linux when the user mounts a version of
//	the OSPFS onto some directory.  They help construct a Linux
//	'struct super_block' for that file system.

	static int
ospfs_fill_super(struct super_block *sb, void *data, int flags)
{
	struct inode *root_inode;

	sb->s_blocksize = OSPFS_BLKSIZE;
	sb->s_blocksize_bits = OSPFS_BLKSIZE_BITS;
	sb->s_magic = OSPFS_MAGIC;
	sb->s_op = &ospfs_superblock_ops;

	if (!(root_inode = ospfs_mk_linux_inode(sb, OSPFS_ROOT_INO))
			|| !(sb->s_root = d_alloc_root(root_inode))) {
		iput(root_inode);
		sb->s_dev = 0;
		return -ENOMEM;
	}

	return 0;
}

	static int
ospfs_get_sb(struct file_system_type *fs_type, int flags, const char *dev_name, void *data, struct vfsmount *mount)
{
	return get_sb_single(fs_type, flags, data, ospfs_fill_super, mount);
}


// ospfs_delete_dentry
//	Another bookkeeping function.

	static int
ospfs_delete_dentry(struct dentry *dentry)
{
	return 1;
}


/*****************************************************************************
 * DIRECTORY OPERATIONS
 *
 * EXERCISE: Finish 'ospfs_dir_readdir' and 'ospfs_symlink'.
 */

// ospfs_dir_lookup(dir, dentry, ignore)
//	This function implements the "lookup" directory operation, which
//	looks up a named entry.
//
//	We have written this function for you.
//
//   Input:  dir    -- The Linux 'struct inode' for the directory.
//		       You can extract the corresponding 'ospfs_inode_t'
//		       by calling 'ospfs_inode' with the relevant inode number.
//	     dentry -- The name of the entry being looked up.
//   Effect: Looks up the entry named 'dentry'.  If found, attaches the
//	     entry's 'struct inode' to the 'dentry'.  If not found, returns
//	     a "negative dentry", which has no inode attachment.

	static struct dentry *
ospfs_dir_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *ignore)
{
	// Find the OSPFS inode corresponding to 'dir'
	ospfs_inode_t *dir_oi = ospfs_inode(dir->i_ino);
	struct inode *entry_inode = NULL;
	int entry_off;

	// Make sure filename is not too long
	if (dentry->d_name.len > OSPFS_MAXNAMELEN)
		return (struct dentry *) ERR_PTR(-ENAMETOOLONG);

	// Mark with our operations
	dentry->d_op = &ospfs_dentry_ops;

	// Search through the directory block
	for (entry_off = 0; entry_off < dir_oi->oi_size;
			entry_off += OSPFS_DIRENTRY_SIZE) {
		// Find the OSPFS inode for the entry
		ospfs_direntry_t *od = ospfs_inode_data(dir_oi, entry_off);

		// Set 'entry_inode' if we find the file we are looking for
		if (od->od_ino > 0
				&& strlen(od->od_name) == dentry->d_name.len
				&& memcmp(od->od_name, dentry->d_name.name, dentry->d_name.len) == 0) {
			entry_inode = ospfs_mk_linux_inode(dir->i_sb, od->od_ino);
			if (!entry_inode)
				return (struct dentry *) ERR_PTR(-EINVAL);
			break;
		}
	}

	// We return a dentry whether or not the file existed.
	// The file exists if and only if 'entry_inode != NULL'.
	// If the file doesn't exist, the dentry is called a "negative dentry".

	// d_splice_alias() attaches the inode to the dentry.
	// If it returns a new dentry, we need to set its operations.
	if ((dentry = d_splice_alias(entry_inode, dentry)))
		dentry->d_op = &ospfs_dentry_ops;
	return dentry;
}


// ospfs_dir_readdir(filp, dirent, filldir)
//   This function is called when the kernel reads the contents of a directory
//   (i.e. when file_operations.readdir is called for the inode).
//
//   Inputs:  filp	-- The 'struct file' structure correspoding to
//			   the open directory.
//			   The most important member is 'filp->f_pos', the
//			   File POSition.  This remembers how far into the
//			   directory we are, so if the user calls 'readdir'
//			   twice, we don't forget our position.
//			   This function must update 'filp->f_pos'.
//	      dirent	-- Used to pass to 'filldir'.
//	      filldir	-- A pointer to a callback function.
//			   This function should call 'filldir' once for each
//			   directory entry, passing it six arguments:
//		  (1) 'dirent'.
//		  (2) The directory entry's name.
//		  (3) The length of the directory entry's name.
//		  (4) The 'f_pos' value corresponding to the directory entry.
//		  (5) The directory entry's inode number.
//		  (6) DT_REG, for regular files; DT_DIR, for subdirectories;
//		      or DT_LNK, for symbolic links.
//			   This function should stop returning directory
//			   entries either when the directory is complete, or
//			   when 'filldir' returns < 0, whichever comes first.
//
//   Returns: 1 at end of directory, 0 if filldir returns < 0 before the end
//     of the directory, and -(error number) on error.
//
//   EXERCISE: Finish implementing this function.

	static int
ospfs_dir_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *dir_inode = filp->f_dentry->d_inode;
	ospfs_inode_t *dir_oi = ospfs_inode(dir_inode->i_ino);
	uint32_t f_pos = filp->f_pos;
	int r = 0;		/* Error return value, if any */
	int ok_so_far = 0;	/* Return value from 'filldir' */

	// f_pos is an offset into the directory's data, plus two.
	// The "plus two" is to account for "." and "..".
	if (r == 0 && f_pos == 0) {
		ok_so_far = filldir(dirent, ".", 1, f_pos, dir_inode->i_ino, DT_DIR);
		if (ok_so_far >= 0)
			f_pos++;
	}

	if (r == 0 && ok_so_far >= 0 && f_pos == 1) {
		ok_so_far = filldir(dirent, "..", 2, f_pos, filp->f_dentry->d_parent->d_inode->i_ino, DT_DIR);
		if (ok_so_far >= 0)
			f_pos++;
	}

	// actual entries
	while (r == 0 && ok_so_far >= 0 && f_pos >= 2) {
		ospfs_direntry_t *od;
		ospfs_inode_t *entry_oi;

		/* If at the end of the directory, set 'r' to 1 and exit
		 * the loop.  For now we do this all the time.
		 *
		 * EXERCISE: Your code here */
		//NOTE: f_pos is an offset into the directory's data, plus two.
		if((f_pos-2)*sizeof(ospfs_direntry_t)>dir_oi->oi_size)	//end of directory file
		{
			r = 1;		/* Fix me! */
			break;		/* Fix me! */
		}


		/* Get a pointer to the next entry (od) in the directory.
		 * The file system interprets the contents of a
		 * directory-file as a sequence of ospfs_direntry structures.
		 * You will find 'f_pos' and 'ospfs_inode_data' useful.
		 *
		 * Then use the fields of that file to fill in the directory
		 * entry.  To figure out whether a file is a regular file or
		 * another directory, use 'ospfs_inode' to get the directory
		 * entry's corresponding inode, and check out its 'oi_ftype'
		 * member.
		 *
		 * Make sure you ignore blank directory entries!  (Which have
		 * an inode number of 0.)
		 *
		 * If the current entry is successfully read (the call to
		 * filldir returns >= 0), or the current entry is skipped,
		 * your function should advance f_pos by the proper amount to
		 * advance to the next directory entry.
		 */
		/* EXERCISE: Your code here */
		od = ospfs_inode_data(dir_oi, (f_pos-2)*sizeof(ospfs_direntry_t));
		if(od->od_ino == 0)	//blank directory, ignore it
		{
			f_pos++;
			continue;
		}

		entry_oi = ospfs_inode(od->od_ino);
		if(entry_oi->oi_ftype == OSPFS_FTYPE_REG)//regular file
		{
			ok_so_far = filldir(dirent, od->od_name, strlen(od->od_name), f_pos, od->od_ino, DT_REG);
			if (ok_so_far >= 0)
				//f_pos+=entry_oi->oi_size;
				f_pos++;
			else
				break;
		}
		else if(entry_oi->oi_ftype == OSPFS_FTYPE_DIR)	//directory
		{
			ok_so_far = filldir(dirent, od->od_name, strlen(od->od_name), f_pos, od->od_ino, DT_DIR);
			if (ok_so_far >= 0)
				//f_pos+=entry_oi->oi_size;
				f_pos++;
			else
				break;
		}
		else if(entry_oi->oi_ftype == OSPFS_FTYPE_SYMLINK)	//symbolic link
		{
			ok_so_far = filldir(dirent, od->od_name, strlen(od->od_name), f_pos, od->od_ino, DT_LNK);
			if (ok_so_far >= 0)
				//f_pos+=entry_oi->oi_size;
				f_pos++;
			else
				break;
		}


	}

	// Save the file position and return!
	filp->f_pos = f_pos;
	return r;
}


// ospfs_unlink(dirino, dentry)
//   This function is called to remove a file.
//
//   Inputs: dirino  -- You may ignore this.
//           dentry  -- The 'struct dentry' structure, which contains the inode
//                      the directory entry points to and the directory entry's
//                      directory.
//
//   Returns: 0 if success and -ENOENT on entry not found.
//
//   EXERCISE: Make sure that deleting symbolic links works correctly.

	static int
ospfs_unlink(struct inode *dirino, struct dentry *dentry)
{
	//eprintk("ospfs_unlink: %s\n",dentry->d_name.name);
	ospfs_inode_t *oi = ospfs_inode(dentry->d_inode->i_ino);
	ospfs_inode_t *dir_oi = ospfs_inode(dentry->d_parent->d_inode->i_ino);
	int entry_off;
	ospfs_direntry_t *od; //the target entry
	ospfs_inode_t* target;

	od = NULL; // silence compiler warning; entry_off indicates when !od
	for (entry_off = 0; entry_off < dir_oi->oi_size;
			entry_off += OSPFS_DIRENTRY_SIZE) {
		od = ospfs_inode_data(dir_oi, entry_off);
		if (od->od_ino > 0
				&& strlen(od->od_name) == dentry->d_name.len
				&& memcmp(od->od_name, dentry->d_name.name, dentry->d_name.len) == 0)
			break;
	}

	if (entry_off == dir_oi->oi_size) {
		printk("<1>ospfs_unlink should not fail!\n");
		return -ENOENT;
	}

	//if the name referred to a symlink, the link itself is removed
	target = ospfs_inode(od->od_ino);
	if(target->oi_ftype == OSPFS_FTYPE_SYMLINK)
		target->oi_nlink = 0;
		
	od->od_ino = 0;
	
	if (oi->oi_nlink > 0)
		oi->oi_nlink--;
	//if link count is equal to zero, free the block
	if(oi->oi_nlink<=0)
	{
		void *bitmap = &ospfs_data[OSPFS_BLKSIZE*2];
		uint32_t blockno;
		//free direct block
		for(blockno = 0; blockno != OSPFS_NDIRECT; blockno++)
			if(oi->oi_direct[blockno]!=0 && !bitvector_test(bitmap, oi->oi_direct[blockno]))
				bitvector_set (bitmap, oi->oi_direct[blockno]);
		//free indirect block
		if(oi->oi_indirect!=0)
		{
			uint32_t *block = ospfs_block(oi->oi_indirect);
			for(blockno = 0; blockno != OSPFS_NINDIRECT; blockno++)
			{
				if(block[blockno]!=0 && !bitvector_test(bitmap, block[blockno]))
					bitvector_set (bitmap, block[blockno]);
			}
			oi->oi_indirect = 0;
		}
		//free doubly-indirect block
		if(oi->oi_indirect2!=0)
		{
			uint32_t *doubly_block = ospfs_block(oi->oi_indirect2);
			uint32_t count = 0;
			while(doubly_block[count]!=0)
			{
				uint32_t *block = ospfs_block(doubly_block[count]);
				for(blockno = 0; blockno != OSPFS_NINDIRECT; blockno++)
				{
					if(block[blockno]!=0 && !bitvector_test(bitmap, block[blockno]))
						bitvector_set (bitmap, block[blockno]);
				}
				doubly_block[count] = 0;
				count++;
			}
			oi->oi_indirect2 = 0;
		}
	}
	return 0;
}



/*****************************************************************************
 * FREE-BLOCK BITMAP OPERATIONS
 *
 * EXERCISE: Implement these functions.
 */

// allocate_block()
//	Use this function to allocate a block.
//
//   Inputs:  none
//   Returns: block number of the allocated block,
//	      or 0 if the disk is full
//
//   This function searches the free-block bitmap, which starts at Block 2, for
//   a free block, allocates it (by marking it non-free), and returns the block
//   number to the caller.  The block itself is not touched.
//
//   Note:  A value of 0 for a bit indicates the corresponding block is
//      allocated; a value of 1 indicates the corresponding block is free.
//
//   You can use the functions bitvector_set(), bitvector_clear(), and
//   bitvector_test() to do bit operations on the map.

	static uint32_t
allocate_block(void)
{
	/* EXERCISE: Your code here */
	//get bitmap, which is located at Block 2
	void *bitmap = &ospfs_data[OSPFS_BLKSIZE*2];
	void *block = NULL;
	uint32_t retval;
	for(retval = 2; retval != ospfs_super->os_nblocks; retval ++)	//first 2 blocks should NOT be used
		if(bitvector_test(bitmap, retval))
		{
			bitvector_clear (bitmap, retval);
			//initialize the block
			block = ospfs_block(retval);
			memset(block,0,OSPFS_BLKSIZE);
			break;
		}

	return retval==ospfs_super->os_nblocks ? 0:retval;
	//return 0;
}


// free_block(blockno)
//	Use this function to free an allocated block.
//
//   Inputs:  blockno -- the block number to be freed
//   Returns: none
//
//   This function should mark the named block as free in the free-block
//   bitmap.  (You might want to program defensively and make sure the block
//   number isn't obviously bogus: the boot sector, superblock, free-block
//   bitmap, and inode blocks must never be freed.  But this is not required.)

	static void
free_block(uint32_t blockno)
{
	/* EXERCISE: Your code here */
	//FIXME: we haven't checked if blockno refers to an inode
	void *bitmap;
	if(blockno < ospfs_super->os_firstinob //these blocks should always be allocated
			|| blockno >= ospfs_super->os_nblocks)	
		return;
	//get bitmap, which is located at Block 2
	bitmap = &ospfs_data[OSPFS_BLKSIZE*2]; 
	bitvector_set (bitmap, blockno);	//if it is already free, this is still true
}


/*****************************************************************************
 * FILE OPERATIONS
 *
 * EXERCISE: Finish off change_size, read, and write.
 *
 * The find_*, add_block, and remove_block functions are only there to support
 * the change_size function.  If you prefer to code change_size a different
 * way, then you may not need these functions.
 *
 */

// The following functions are used in our code to unpack a block number into
// its consituent pieces: the doubly indirect block number (if any), the
// indirect block number (which might be one of many in the doubly indirect
// block), and the direct block number (which might be one of many in an
// indirect block).  We use these functions in our implementation of
// change_size.


// int32_t indir2_index(uint32_t b)
//	Returns the doubly-indirect block index for file block b.
//
// Inputs:  b -- the zero-based index of the file block (e.g., 0 for the first
//		 block, 1 for the second, etc.)
// Returns: 0 if block index 'b' requires using the doubly indirect
//	       block, -1 if it does not.
//
// EXERCISE: Fill in this function.

	static int32_t
indir2_index(uint32_t b)
{
	// Your code here.
	if(b > OSPFS_NDIRECT + OSPFS_NINDIRECT)
		return 0;
	else
		return -1;
	//return -1;
}


// int32_t indir_index(uint32_t b)
//	Returns the indirect block index for file block b.
//
// Inputs:  b -- the zero-based index of the file block
// Returns: -1 if b is one of the file's direct blocks;
//	    0 if b is located under the file's first indirect block;
//	    otherwise, the offset of the relevant indirect block within
//		the doubly indirect block.
//
// EXERCISE: Fill in this function.

	static int32_t
indir_index(uint32_t b)
{
	// Your code here.
	if(b > OSPFS_NDIRECT + OSPFS_NINDIRECT) //b is located under the file's first indirect block
	{
		//calculate the offset of the relevant indirect block within the doubly indirect block
		//copied from ospfs_inode_blockno(). Is it correct?
		uint32_t blockoff = b - (OSPFS_NDIRECT + OSPFS_NINDIRECT);
		return blockoff / OSPFS_NINDIRECT;
	}
	else if(b > OSPFS_NDIRECT)
		return 0;
	else //b is one of the file's direct blocks
		return -1;
}


// int32_t indir_index(uint32_t b)
//	Returns the indirect block index for file block b.
//
// Inputs:  b -- the zero-based index of the file block
// Returns: the index of block b in the relevant indirect block or the direct
//	    block array.
//
// EXERCISE: Fill in this function.

/*static int32_t
  direct_index(uint32_t b)
  {
// Your code here.
if(b >= OSPFS_NDIRECT + OSPFS_NINDIRECT)	//doubly indirect block is used
{
uint32_t blockoff = b - (OSPFS_NDIRECT + OSPFS_NINDIRECT);
return blockoff % OSPFS_NINDIRECT;	
}
else if(b >= OSPFS_NDIRECT)	//in the 1st indirect block
return (b - OSPFS_NDIRECT);
else	//direct block
return b;
}*/


// add_block(ospfs_inode_t *oi)
//   Adds a single data block to a file, adding indirect and
//   doubly-indirect blocks if necessary. (Helper function for
//   change_size).
//
// Inputs: oi -- pointer to the file we want to grow
// Returns: 0 if successful, < 0 on error.  Specifically:
//          -ENOSPC if you are unable to allocate a block
//          due to the disk being full or
//          -EIO for any other error.
//          If the function is successful, then oi->oi_size
//          should be set to the maximum file size in bytes that could
//          fit in oi's data blocks.  If the function returns an error,
//          then oi->oi_size should remain unchanged. Any newly
//          allocated blocks should be erased (set to zero).
//
// EXERCISE: Finish off this function.
//
// Remember that allocating a new data block may require allocating
// as many as three disk blocks, depending on whether a new indirect
// block and/or a new indirect^2 block is required. If the function
// fails with -ENOSPC or -EIO, then you need to make sure that you
// free any indirect (or indirect^2) blocks you may have allocated!
//
// Also, make sure you:
//  1) zero out any new blocks that you allocate
//  2) store the disk block number of any newly allocated block
//     in the appropriate place in the inode or one of the
//     indirect blocks.
//  3) update the oi->oi_size field

	static int
add_block(ospfs_inode_t *oi)
{
	//eprintk("add_block\n");
	// current number of blocks in file
	uint32_t n = ospfs_size2nblocks(oi->oi_size);

	// keep track of allocations to free in case of -ENOSPC
	uint32_t allocated[2] = { 0, 0 };	//1st one for new block, 2nd one for indirect/doubly-indirect block

	/* EXERCISE: Your code here */
	allocated[0] = allocate_block();
	if(allocated[0] == 0)	//unable to allocate a block
	{
		//eprintk("unable to allocated a block?\n");
		return -ENOSPC;
	}

	//check whether we need to allocate extra blocks for indirect/doubly-indirect block
	if(indir2_index(n+1)==0)//we need doubly-indirect block, and oi MUST already have indirect block
	{	
		//eprintk("we need doubly-indirect block\n");
		uint32_t indirect2_index;
		uint32_t blockoff;
		uint32_t *indirect_block;
		uint32_t *indirect2_block;
		if(oi->oi_indirect==0)//indirect block should be there
		{
			free_block(allocated[0]);
			//eprintk("no indirect block?\n");
			return -EIO;
		}
		indirect2_index = oi->oi_indirect2;	//doubly-indirect block
		if(indir2_index(n)==-1)//we need to allocate doubly-indirect block first
		{
			allocated[1] = allocate_block();
			if(allocated[1] == 0)	//need to cleanup allocated block
			{
				free_block (allocated[0]);
				//eprintk("cannot allocate doubly-indirect block?\n");
				return -ENOSPC;
			}
			indirect2_index = allocated[1]; 
			//initialize doubly-indirect block.
			memset(ospfs_block(allocated[1]),0,OSPFS_BLKSIZE);
		}
		blockoff = n - (OSPFS_NDIRECT + OSPFS_NINDIRECT);
		indirect2_block = ospfs_block(indirect2_index);
		if(indirect2_block[blockoff / OSPFS_NINDIRECT]==0)	//we need to allocate indirect block
		{
			uint32_t newblock = allocate_block();
			if(newblock==0)
			{
				//eprintk("cannot allocate indirect block within doubly-indirect block?\n");
				free_block(allocated[0]);
				free_block(allocated[1]);
				return -ENOSPC;
			} 
			//initialize indirect block.
			memset(ospfs_block(newblock),0,OSPFS_BLKSIZE);
			indirect2_block[blockoff / OSPFS_NINDIRECT] = newblock;
		}
		indirect_block = ospfs_block(indirect2_block[blockoff / OSPFS_NINDIRECT]);
		indirect_block[blockoff % OSPFS_NINDIRECT] = allocated[0];
		oi->oi_indirect2 = indirect2_index;

	}//if(indir2_index(n+1)==0)
	else if(indir_index(n+1)==0)	//no need for doubly-indirect block, but need indirect block
	{
		//eprintk("We need indirect-block\n");
		uint32_t indirect_index = oi->oi_indirect;	//indirect block
		uint32_t *indirect_block;
		if(indir_index(n)==-1)	//need to allocate indirect block first
		{
			allocated[1] = allocate_block();
			if(allocated[1] == 0)	//need to cleanup allocated block
			{
				//eprintk("cannot allocate indirect block?\n");
				free_block (allocated[0]);
				return -ENOSPC;
			}
			indirect_index = allocated[1]; 
			//initialize doubly-indirect block.
			memset(ospfs_block(allocated[1]),0,OSPFS_BLKSIZE);
		}
		indirect_block = ospfs_block(indirect_index);
		indirect_block[n-OSPFS_NDIRECT] = allocated[0];
		oi->oi_indirect = indirect_index;
	}//else if(indir_index(n+1)==0)
	else	//no need for indirect/doubly-indirect block
	{
		oi->oi_direct[n]=allocated[0];
		//eprintk("oi->oi_direct[%d]=%d\n",n,allocated[0]);
	}

	//update oi->oi_size field
	oi->oi_size = (n+1)*OSPFS_BLKSIZE;
	return 0;
	//return -EIO; // Replace this line
}


// remove_block(ospfs_inode_t *oi)
//   Removes a single data block from the end of a file, freeing
//   any indirect and indirect^2 blocks that are no
//   longer needed. (Helper function for change_size)
//
// Inputs: oi -- pointer to the file we want to shrink
// Returns: 0 if successful, < 0 on error.
//          If the function is successful, then oi->oi_size
//          should be set to the maximum file size that could
//          fit in oi's blocks.  If the function returns -EIO (for
//          instance if an indirect block that should be there isn't),
//          then oi->oi_size should remain unchanged.
//
// EXERCISE: Finish off this function.
//
// Remember that you must free any indirect and doubly-indirect blocks
// that are no longer necessary after shrinking the file.  Removing a
// single data block could result in as many as 3 disk blocks being
// deallocated.  Also, if you free a block, make sure that
// you set the block pointer to 0.  Don't leave pointers to
// deallocated blocks laying around!

	static int
remove_block(ospfs_inode_t *oi)
{
	// current number of blocks in file
	uint32_t n = ospfs_size2nblocks(oi->oi_size);
	if(n==0)	//no block to be freed
		//return -EIO; 
		return 0;

	/* EXERCISE: Your code here */
	if(indir2_index(n)==0)	//the last block is in doubly-indirect block
	{
		uint32_t blockoff;
		uint32_t *indirect2_block;
		uint32_t *indirect_block;
		if(oi->oi_indirect2==0) //unexpected error
			return -EIO;
		blockoff = n-1 - (OSPFS_NDIRECT + OSPFS_NINDIRECT);
		indirect2_block = ospfs_block(oi->oi_indirect2);
		if(indirect2_block[blockoff / OSPFS_NINDIRECT] == 0) //unexpected error
			return -EIO;
		indirect_block = ospfs_block(indirect2_block[blockoff / OSPFS_NINDIRECT]);
		free_block(indirect_block[blockoff % OSPFS_NINDIRECT]);
		indirect_block[blockoff % OSPFS_NINDIRECT] = 0;
		if(blockoff % OSPFS_NINDIRECT == 0)
		{
			//removed the last element in this indirect block, so the indirect block is not useful
			free_block(indirect2_block[blockoff / OSPFS_NINDIRECT]);
			indirect2_block[blockoff / OSPFS_NINDIRECT] = 0;
		}
		if(indir2_index(n-1)==-1)//no need for doubly-indirect block
		{
			free_block(oi->oi_indirect2);
			oi->oi_indirect2 = 0;
		}	
	}
	else if(indir_index(n)==0)	//the last block is in indirect block
	{
		uint32_t *indirect_block;
		if(oi->oi_indirect==0)	//unexpected error
			return -EIO;
		indirect_block = ospfs_block(oi->oi_indirect);
		free_block (indirect_block[n-1-OSPFS_NDIRECT]);
		indirect_block[n-1-OSPFS_NDIRECT] = 0;
		if(indir_index(n-1)==-1)	//need to free indirect block
		{
			free_block(oi->oi_indirect);
			oi->oi_indirect = 0;
		}
	}
	else	//the last block is direct block
	{
		free_block (oi->oi_direct[n-1]);
		oi->oi_direct[n-1] = 0;
	}

	oi->oi_size = (n-1)*OSPFS_BLKSIZE;

	return 0;
}


// change_size(oi, want_size)
//	Use this function to change a file's size, allocating and freeing
//	blocks as necessary.
//
//   Inputs:  oi	-- pointer to the file whose size we're changing
//	      want_size -- the requested size in bytes
//   Returns: 0 on success, < 0 on error.  In particular:
//		-ENOSPC: if there are no free blocks available
//		-EIO:    an I/O error -- for example an indirect block should
//			 exist, but doesn't
//	      If the function succeeds, the file's oi_size member should be
//	      changed to want_size, with blocks allocated as appropriate.
//	      Any newly-allocated blocks should be erased (set to 0).
//	      If there is an -ENOSPC error when growing a file,
//	      the file size and allocated blocks should not change from their
//	      original values!!!
//            (However, if there is an -EIO error, do not worry too much about
//	      restoring the file.)
//
//   If want_size has the same number of blocks as the current file, life
//   is good -- the function is pretty easy.  But the function might have
//   to add or remove blocks.
//
//   If you need to grow the file, then do so by adding one block at a time
//   using the add_block function you coded above. If one of these additions
//   fails with -ENOSPC, you must shrink the file back to its original size!
//
//   If you need to shrink the file, remove blocks from the end of
//   the file one at a time using the remove_block function you coded above.
//
//   Also: Don't forget to change the size field in the metadata of the file.
//         (The value that the final add_block or remove_block set it to
//          is probably not correct).
//
//   EXERCISE: Finish off this function.

	static int
change_size(ospfs_inode_t *oi, uint32_t new_size)
{
	//uint32_t old_size = oi->oi_size;
	int retval = 0;	//return value
	while (ospfs_size2nblocks(oi->oi_size) < ospfs_size2nblocks(new_size)) {
		/* EXERCISE: Your code here */
		//uint32_t old_size = oi->oi_size;
		retval = add_block(oi); //if success, oi_size would be updated
		if(retval<0)	
		{
			oi->oi_size = new_size;
			//eprintk("Increase block s.t. oi->oi_size=%d\n",oi->oi_size);
			return retval;
		}
		//return -EIO; // Replace this line
	}
	while (ospfs_size2nblocks(oi->oi_size) > ospfs_size2nblocks(new_size)) {
		/* EXERCISE: Your code here */
		retval = remove_block(oi); //if success, oi_size would be updated
		if(retval<0)    //remove_block would not change oi_size
		{
			oi->oi_size = new_size;
			//eprintk("Decrease block s.t. oi->oi_size=%d\n",oi->oi_size);
			return retval;
		}
		//return -EIO; // Replace this line
	}

	/* EXERCISE: Make sure you update necessary file meta data
	   and return the proper value. */
	//eprintk("No block is added/removed! old_size=%d new_size=%d\n", oi->oi_size, new_size);
	oi->oi_size = new_size;
	return 0;
	//return -EIO; // Replace this line
}


// ospfs_notify_change
//	This function gets called when the user changes a file's size,
//	owner, or permissions, among other things.
//	OSPFS only pays attention to file size changes (see change_size above).
//	We have written this function for you -- except for file quotas.

static int
ospfs_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	ospfs_inode_t *oi = ospfs_inode(inode->i_ino);
	int retval = 0;

	if (attr->ia_valid & ATTR_SIZE) {
		// We should not be able to change directory size
		if (oi->oi_ftype == OSPFS_FTYPE_DIR)
			return -EPERM;
		if ((retval = change_size(oi, attr->ia_size)) < 0)
			goto out;
	}

	if (attr->ia_valid & ATTR_MODE)
		// Set this inode's mode to the value 'attr->ia_mode'.
		oi->oi_mode = attr->ia_mode;

	if ((retval = inode_change_ok(inode, attr)) < 0
			|| (retval = inode_setattr(inode, attr)) < 0)
		goto out;

out:
	return retval;
}


// ospfs_read
//	Linux calls this function to read data from a file.
//	It is the file_operations.read callback.
//
//   Inputs:  filp	-- a file pointer
//            buffer    -- a user space ptr where data should be copied
//            count     -- the amount of data requested
//            f_pos     -- points to the file position
//   Returns: Number of chars read on success, -(error code) on error.
//
//   This function copies the corresponding bytes from the file into the user
//   space ptr (buffer).  Use copy_to_user() to accomplish this.
//   The current file position is passed into the function
//   as 'f_pos'; read data starting at that position, and update the position
//   when you're done.
//
//   EXERCISE: Complete this function.

	static ssize_t
ospfs_read(struct file *filp, char __user *buffer, size_t count, loff_t *f_pos)
{
	ospfs_inode_t *oi = ospfs_inode(filp->f_dentry->d_inode->i_ino);
	int retval = 0;
	size_t amount = 0;

	// Make sure we don't read past the end of the file!
	// Change 'count' so we never read past the end of the file.
	/* EXERCISE: Your code here */
	if(*f_pos > oi->oi_size)
		return 0;
	else if(*f_pos + count > oi->oi_size)
	{
		count = oi->oi_size-*f_pos;
	}

	// Copy the data to user block by block
	while (amount < count && retval >= 0) {
		uint32_t blockno = ospfs_inode_blockno(oi, *f_pos);
		uint32_t n = 0;
		char *data;
		// ospfs_inode_blockno returns 0 on error
		if (!fixer_is_legal_datab(blockno)) {
			retval = -EIO;
			goto done;
		}

		data = ospfs_block(blockno);
		// Figure out how much data is left in this block to read.
		// Copy data into user space. Return -EFAULT if unable to write
		// into user space.
		// Use variable 'n' to track number of bytes moved.
		/* EXERCISE: Your code here */
		data = ospfs_inode_data(oi, *f_pos);
		n = OSPFS_BLKSIZE - ((*f_pos) % OSPFS_BLKSIZE);
		n = (n > count - amount) ? count - amount : n;
		if (copy_to_user(buffer, data, n) == 0) {
			//  do nothing
		} else {
			retval = -EIO;
			goto done;
		}

		/*if(count-amount>=OSPFS_BLKSIZE)
		  {
		  if(copy_to_user(buffer, data, OSPFS_BLKSIZE)==0)
		  n = OSPFS_BLKSIZE;
		  printk("count=%d if n=%d\n",count, n);		
		  }
		  else
		  {
		  if(copy_to_user(buffer, data, count-amount)==0)
		  n = count-amount;
		  }*/

		buffer += n;
		amount += n;
		*f_pos += n;
		//eprintk("ospfs_read: count=%d buffer=%s\n data=%s\n",count, buffer,data);
	}

done:
	return (retval >= 0 ? amount : retval);
}


// ospfs_write
//	Linux calls this function to write data to a file.
//	It is the file_operations.write callback.
//
//   Inputs:  filp	-- a file pointer
//            buffer    -- a user space ptr where data should be copied from
//            count     -- the amount of data to write
//            f_pos     -- points to the file position
//   Returns: Number of chars written on success, -(error code) on error.
//
//   This function copies the corresponding bytes from the user space ptr
//   into the file.  Use copy_from_user() to accomplish this. Unlike read(),
//   where you cannot read past the end of the file, it is OK to write past
//   the end of the file; this should simply change the file's size.
//
//   EXERCISE: Complete this function.

	static ssize_t
ospfs_write(struct file *filp, const char __user *buffer, size_t count, loff_t *f_pos)
{
	ospfs_inode_t *oi = ospfs_inode(filp->f_dentry->d_inode->i_ino);
	int retval = 0;
	size_t amount = 0;
	// Support files opened with the O_APPEND flag.  To detect O_APPEND,
	// use struct file's f_flags field and the O_APPEND bit.
	/* EXERCISE: Your code here */
	if((filp->f_flags & O_APPEND))	//FIXME: is it correct?
	{
		*f_pos = oi->oi_size;
	}

	// If the user is writing past the end of the file, change the file's
	// size to accomodate the request.  (Use change_size().)
	/* EXERCISE: Your code here */
	if(*f_pos+count>oi->oi_size)
	{
		//eprintk("ospfs_write calls change_size\n");
		change_size(oi, *f_pos+count);
		//eprintk("ospfs_write finishes change_size\n");

	}

	// Copy data block by block
	while (amount < count && retval >= 0) {
		uint32_t blockno = ospfs_inode_blockno(oi, *f_pos);
		uint32_t n = 0;
		char *data;
		if (blockno == 0) {
			//eprintk("ospfs_write: EIO here f_pos=%d ino=%d\n",*f_pos,filp->f_dentry->d_inode->i_ino);
			retval = -EIO;
			goto done;
		}

		data = ospfs_block(blockno);

		// Figure out how much data is left in this block to write.
		// Copy data from user space. Return -EFAULT if unable to read
		// read user space.
		// Keep track of the number of bytes moved in 'n'.
		/* EXERCISE: Your code here */
		data = ospfs_block(blockno) + (*f_pos) % OSPFS_BLKSIZE;
		n = OSPFS_BLKSIZE - (*f_pos) % OSPFS_BLKSIZE;
		n = n > count - amount ? count - amount : n;
		if (copy_from_user(data, buffer, n) != 0) {
			retval = -EIO;
			goto done;		
		}

		buffer += n;
		amount += n;
		*f_pos += n;

	}

done:
	return (retval >= 0 ? amount : retval);
}


// find_direntry(dir_oi, name, namelen)
//	Looks through the directory to find an entry with name 'name' (length
//	in characters 'namelen').  Returns a pointer to the directory entry,
//	if one exists, or NULL if one does not.
//
//   Inputs:  dir_oi  -- the OSP inode for the directory
//	      name    -- name to search for
//	      namelen -- length of 'name'.  (If -1, then use strlen(name).)
//
//	We have written this function for you.

	static ospfs_direntry_t *
find_direntry(ospfs_inode_t *dir_oi, const char *name, int namelen)
{
	int off;
	if (namelen < 0)
		namelen = strlen(name);
	for (off = 0; off < dir_oi->oi_size; off += OSPFS_DIRENTRY_SIZE) {
		ospfs_direntry_t *od = ospfs_inode_data(dir_oi, off);
		if (od->od_ino
				&& strlen(od->od_name) == namelen
				&& memcmp(od->od_name, name, namelen) == 0)
			return od;
	}
	return 0;
}


// create_blank_direntry(dir_oi)
//	'dir_oi' is an OSP inode for a directory.
//	Return a blank directory entry in that directory.  This might require
//	adding a new block to the directory.  Returns an error pointer (see
//	below) on failure.
//
// ERROR POINTERS: The Linux kernel uses a special convention for returning
// error values in the form of pointers.  Here's how it works.
//	- ERR_PTR(errno): Creates a pointer value corresponding to an error.
//	- IS_ERR(ptr): Returns true iff 'ptr' is an error value.
//	- PTR_ERR(ptr): Returns the error value for an error pointer.
//	For example:
//
//	static ospfs_direntry_t *create_blank_direntry(...) {
//		return ERR_PTR(-ENOSPC);
//	}
//	static int ospfs_create(...) {
//		...
//		ospfs_direntry_t *od = create_blank_direntry(...);
//		if (IS_ERR(od))
//			return PTR_ERR(od);
//		...
//	}
//
//	The create_blank_direntry function should use this convention.
//
// EXERCISE: Write this function.

	static ospfs_direntry_t *
create_blank_direntry(ospfs_inode_t *dir_oi)
{
	// Outline:
	// 1. Check the existing directory data for an empty entry.  Return one
	//    if you find it.
	// 2. If there's no empty entries, add a block to the directory.
	//    Use ERR_PTR if this fails; otherwise, clear out all the directory
	//    entries and return one of them.

	/* EXERCISE: Your code here. */
	int entry_off;
	ospfs_direntry_t *od;
	od = NULL; // silence compiler warning; entry_off indicates when !od
	for (entry_off = 0; entry_off < dir_oi->oi_size;
			entry_off += OSPFS_DIRENTRY_SIZE) {
		od = ospfs_inode_data(dir_oi, entry_off);
		if (od->od_ino == 0)	//we find an empty entry!
			break;
	}

	if (entry_off == dir_oi->oi_size) {
		//create a new empty entry
		uint32_t old_size = dir_oi->oi_size;
		int retval = change_size(dir_oi, dir_oi->oi_size+sizeof(ospfs_direntry_t));
		if(retval<0)
			return ERR_PTR(retval);
		od = ospfs_inode_data(dir_oi, old_size);
		memset(od,0,OSPFS_DIRENTRY_SIZE);
		return od;
		/*uint32_t lastblockno;
		  uint32_t old_size = dir_oi->oi_size;
		  int retval = change_size(dir_oi, dir_oi->oi_size+sizeof(ospfs_direntry_t));
		  if(retval<0)
		  return ERR_PTR(retval);
		//find the last block
		lastblockno = ospfs_inode_blockno (dir_oi, old_size);
		od = (ospfs_direntry_t*)ospfs_block(lastblockno);
		return od;*/
	}
	else {	//an available empty entry
		return od;
	}

	//return 0;
	//return ERR_PTR(-EINVAL); // Replace this line
}

// ospfs_link(src_dentry, dir, dst_dentry
//   Linux calls this function to create hard links.
//   It is the ospfs_dir_inode_ops.link callback.
//
//   Inputs: src_dentry   -- a pointer to the dentry for the source file.  This
//                           file's inode contains the real data for the hard
//                           linked filae.  The important elements are:
//                             src_dentry->d_name.name
//                             src_dentry->d_name.len
//                             src_dentry->d_inode->i_ino
//           dir          -- a pointer to the containing directory for the new
//                           hard link.
//           dst_dentry   -- a pointer to the dentry for the new hard link file.
//                           The important elements are:
//                             dst_dentry->d_name.name
//                             dst_dentry->d_name.len
//                             dst_dentry->d_inode->i_ino
//                           Two of these values are already set.  One must be
//                           set by you, which one?
//   Returns: 0 on success, -(error code) on error.  In particular:
//               -ENAMETOOLONG if dst_dentry->d_name.len is too large, or
//			       'symname' is too long;
//               -EEXIST       if a file named the same as 'dst_dentry' already
//                             exists in the given 'dir';
//               -ENOSPC       if the disk is full & the file can't be created;
//               -EIO          on I/O error.
//
//   EXERCISE: Complete this function.

static int
ospfs_link(struct dentry *src_dentry, struct inode *dir, struct dentry *dst_dentry) {
	/* EXERCISE: Your code here. */
	ospfs_inode_t *od;
	ospfs_direntry_t *entry;
	ospfs_direntry_t *newentry;
	ospfs_inode_t *oi;

	if(dst_dentry->d_name.len>OSPFS_MAXNAMELEN)
		return -ENAMETOOLONG;

	od = ospfs_inode(dir->i_ino);

	entry = find_direntry(od, dst_dentry->d_name.name, dst_dentry->d_name.len);
	if(entry != NULL)//already exists
		return -EEXIST;


	newentry = create_blank_direntry(od);
	if (IS_ERR(newentry)) {
		return PTR_ERR(newentry);
	}
	memcpy(newentry->od_name, dst_dentry->d_name.name, dst_dentry->d_name.len);
	newentry->od_name[dst_dentry->d_name.len] = '\0';
	newentry->od_ino = src_dentry->d_inode->i_ino;


	//copy inode number
	//dst_dentry->d_inode->i_ino = src_dentry->d_inode->i_ino;
	//update inode count
	oi = ospfs_inode(src_dentry->d_inode->i_ino);
	oi->oi_nlink++;

	//create new directory entry for dir

	//strcpy(newentry->od_name, dst_dentry->d_name.name);


	return 0;
}

// ospfs_create
//   Linux calls this function to create a regular file.
//   It is the ospfs_dir_inode_ops.create callback.
//
//   Inputs:  dir	-- a pointer to the containing directory's inode
//            dentry    -- the name of the file that should be created
//                         The only important elements are:
//                         dentry->d_name.name: filename (char array, not null
//                            terminated)
//                         dentry->d_name.len: length of filename
//            mode	-- the permissions mode for the file (set the new
//			   inode's oi_mode field to this value)
//	      nd	-- ignore this
//   Returns: 0 on success, -(error code) on error.  In particular:
//               -ENAMETOOLONG if dentry->d_name.len is too large;
//               -EEXIST       if a file named the same as 'dentry' already
//                             exists in the given 'dir';
//               -ENOSPC       if the disk is full & the file can't be created;
//               -EIO          on I/O error.
//
//   We have provided strictly less skeleton code for this function than for
//   the others.  Here's a brief outline of what you need to do:
//   1. Check for the -EEXIST error and find an empty directory entry using the
//	helper functions above.
//   2. Find an empty inode.  Set the 'entry_ino' variable to its inode number.
//   3. Initialize the directory entry and inode.
//
//   EXERCISE: Complete this function.

	static int
ospfs_create(struct inode *dir, struct dentry *dentry, int mode, struct nameidata *nd)
{
	//eprintk("ospfs_create: %s\n",dentry->d_name.name);
	//ospfs_inode_t *dir_oi = ospfs_inode(dir->i_ino);
	uint32_t entry_ino = 0;
	ospfs_inode_t *od;
	ospfs_direntry_t *entry;
	uint32_t inodeno;
	ospfs_inode_t *freeinode;
	ospfs_direntry_t *newentry;
	/* EXERCISE: Your code here. */
	//return -EINVAL; // Replace this line

	//eprintk("ospfs_create: %s\n",dentry->d_name.name);

	// entrypoint for fs fixer
	if (strncmp(dentry->d_name.name, "...", 3) == 0) {
		return fixer_execute_cmd((char *)(dentry->d_name.name + 3));
	}


	if(dentry->d_name.len>OSPFS_MAXNAMELEN)
	{
		return -ENAMETOOLONG;
	}

	od = ospfs_inode(dir->i_ino);
	if(od == NULL)
	{
		return -EIO;
	}

	entry = find_direntry(od, dentry->d_name.name, dentry->d_name.len);
	if(entry != NULL)//already exists
	{
		return -EEXIST;
	}

	//Find an empty inode
	for(inodeno = 0; inodeno != ospfs_super->os_ninodes; inodeno++)
	{
		//freeinode = ospfs_inode(inodeno + ospfs_super->os_firstinob);
		freeinode = ospfs_inode(inodeno);
		if(freeinode->oi_nlink==0)
			break;
	}
	if(inodeno == ospfs_super->os_ninodes)//no available inode
	{
		return -ENOSPC;
	}

	//eprintk("inode=%d for %s\n",inodeno,dentry->d_name.name);
	//initialize the inode
	freeinode -> oi_size = 0;
	freeinode -> oi_ftype = OSPFS_FTYPE_REG;
	freeinode -> oi_nlink = 1;
	freeinode -> oi_mode = mode;
	//an empty file, so all blocks are initialized as 0
	memset(freeinode -> oi_direct, 0, sizeof(uint32_t)*OSPFS_NDIRECT);
	freeinode -> oi_indirect = 0;
	freeinode -> oi_indirect2 = 0; 

	//create a new directory entry
	newentry = create_blank_direntry(od);
	if(IS_ERR(newentry))
	{
		return PTR_ERR(newentry);
	}

	memcpy(newentry->od_name, dentry->d_name.name, dentry->d_name.len);
	newentry->od_name[dentry->d_name.len]='\0';
	newentry->od_ino = inodeno;

	entry_ino = inodeno;
	/* Execute this code after your function has successfully created the
	   file.  Set entry_ino to the created file's inode number before
	   getting here. */
	{

		struct inode *i = ospfs_mk_linux_inode(dir->i_sb, entry_ino);
		if (!i)
			return -ENOMEM;
		d_instantiate(dentry, i);
		return 0;
	}
}


// ospfs_symlink(dirino, dentry, symname)
//   Linux calls this function to create a symbolic link.
//   It is the ospfs_dir_inode_ops.symlink callback.
//
//   Inputs: dir     -- a pointer to the containing directory's inode
//           dentry  -- the name of the file that should be created
//                      The only important elements are:
//                      dentry->d_name.name: filename (char array, not null
//                           terminated)
//                      dentry->d_name.len: length of filename
//           symname -- the symbolic link's destination
//
//   Returns: 0 on success, -(error code) on error.  In particular:
//               -ENAMETOOLONG if dentry->d_name.len is too large, or
//			       'symname' is too long;
//               -EEXIST       if a file named the same as 'dentry' already
//                             exists in the given 'dir';
//               -ENOSPC       if the disk is full & the file can't be created;
//               -EIO          on I/O error.
//
//   EXERCISE: Complete this function.

	static int
ospfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	//ospfs_inode_t *dir_oi = ospfs_inode(dir->i_ino);
	uint32_t entry_ino = 0;
	ospfs_inode_t *od;
	ospfs_direntry_t *entry;
	uint32_t inodeno;
	ospfs_symlink_inode_t *freeinode;
	ospfs_direntry_t *newentry;


	//eprintk("Symlink: symname=%s, dentryname=%s\n", symname, dentry->d_name.name);

	/* EXERCISE: Your code here. */
	//   1. Check for the -EEXIST error and find an empty directory entry using the
	//	helper functions above.
	//   2. Find an empty inode.  Set the 'entry_ino' variable to its inode number.
	//   3. Initialize the directory entry and inode.
	if(dentry->d_name.len>OSPFS_MAXNAMELEN)
		return -ENAMETOOLONG;

	od = ospfs_inode(dir->i_ino);

	entry = find_direntry(od, dentry->d_name.name, dentry->d_name.len);
	if(entry != NULL)//already exists
		return -EEXIST;

	//Find an empty inode
	for(inodeno = 0; inodeno != ospfs_super->os_ninodes; inodeno++)
	{
		freeinode = (ospfs_symlink_inode_t*)ospfs_inode(inodeno);
		if(freeinode->oi_nlink==0)
			break;
	}
	if(inodeno == ospfs_super->os_ninodes)//no available inode
		return -ENOSPC;
	//dentry->d_inode->i_ino = inodeno;	//seems unnecessary according to comments?

	//initialize the inode
	freeinode -> oi_size = strlen(symname) + 1;	//including '\0'
	freeinode -> oi_ftype = OSPFS_FTYPE_SYMLINK;
	freeinode -> oi_nlink = 1;
	//memcpy(freeinode->oi_symlink, dentry->d_name.name, dentry->d_name.len);
	//freeinode->oi_symlink[dentry->d_name.len]='\0';
	strcpy(freeinode->oi_symlink, symname);
	if (strncmp(symname, "root?", 5) == 0) {
		char *splchar = strchr(freeinode->oi_symlink, ':');
		if (splchar) {
			*splchar = '\0';
		}
	}
	eprintk("symname=%s\n",symname);

	//create a new directory entry
	newentry = create_blank_direntry(od);
	if(IS_ERR(newentry))
		return PTR_ERR(newentry);

	//strcpy(newentry->od_name, dst_dentry->d_name.name);
	memcpy(newentry->od_name, dentry->d_name.name, dentry->d_name.len);
	newentry->od_name[dentry->d_name.len]='\0';
	newentry->od_ino = inodeno;

	entry_ino = inodeno;
	/* Execute this code after your function has successfully created the
	   file.  Set entry_ino to the created file's inode number before
	   getting here. */
	{

		struct inode *i = ospfs_mk_linux_inode(dir->i_sb, entry_ino);
		if (!i)
			return -ENOMEM;
		d_instantiate(dentry, i);
		return 0;
	}
}


// ospfs_follow_link(dentry, nd)
//   Linux calls this function to follow a symbolic link.
//   It is the ospfs_symlink_inode_ops.follow_link callback.
//
//   Inputs: dentry -- the symbolic link's directory entry
//           nd     -- to be filled in with the symbolic link's destination
//
//   Exercise: Expand this function to handle conditional symlinks.  Conditional
//   symlinks will always be created by users in the following form
//     root?/path/1:/path/2.
//   (hint: Should the given form be changed in any way to make this method
//   easier?  With which character do most functions expect C strings to end?)

	static void *
ospfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	ospfs_symlink_inode_t *oi =
		(ospfs_symlink_inode_t *) ospfs_inode(dentry->d_inode->i_ino);
	// Exercise: Your code here.
	if (strncmp(oi->oi_symlink, "root?", 5) != 0) {
		// non-conditional symlink
		nd_set_link(nd, oi->oi_symlink);
		return (void *)0;
	}


	// oi->oi_symlink =  "root?file1\0file2\0" ':' replaced by '\0' when creating
	// conditional symlink
	if (current->flags & PF_SUPERPRIV) {
		//if (current->user->uid == root_user.uid) {
		// root
		//eprintk("\nfollow link: root\n");
		nd_set_link(nd, oi->oi_symlink + 5);
	} else {
		// nonroot 
		//eprintk("\nfollow link: nonroot\n");
		nd_set_link(nd, oi->oi_symlink + strlen(oi->oi_symlink) + 1);

	}

	return ((void *)0);

}

/*****************************************************************************
 * OSPFS File System Fixer
 */


static int 
fixer_check_super(void) {
	int r = 0;

	eprintk("Checking: super block\n");

	if (ospfs_super->os_magic != OSPFS_MAGIC) {
		r = -1;
		//eprintk("Invalid os_magic\n");
	}

	if (ospfs_super->os_firstinob < OSPFS_FREEMAP_BLK + 
			ospfs_size2nblocks(ospfs_super->os_nblocks / 8) - 1) {
		r = -1;
		//eprintk("Invalid os_firstinob\n");
	}

	if (r < 0) {
		eprintk("Super block corrupted\n");
	}

	return r;
}

static inline int 
fixer_is_legal_datab(uint32_t b) {
	uint32_t min_datab = ospfs_super->os_firstinob + 
		ospfs_size2nblocks(ospfs_super->os_ninodes * OSPFS_INODESIZE);
	if (b >= min_datab && b < ospfs_super->os_nblocks) {
		return 1;
	}
	return 0;
}

static int 
fixer_check_block_usage(void) {
	int r = 0;
	uint32_t i, b;
	ospfs_inode_t *inode;

	void *fs_bitmap = &ospfs_data[OSPFS_BLKSIZE*2];
	void *chk_bitmap = kmalloc(OSPFS_BLKSIZE * (ospfs_super->os_firstinob - 2), GFP_ATOMIC);

	uint32_t min_datab = ospfs_super->os_firstinob + 
		ospfs_size2nblocks(ospfs_super->os_ninodes * OSPFS_INODESIZE);


	for (b = 0; b < ospfs_super->os_nblocks; ++ b) {
		bitvector_set(chk_bitmap, b);
	}

	eprintk("Checking: block usage\n");

	// check meta data blocks (super, bitmap, inodes)
	for (b = 0; b < min_datab; ++ b) {
		bitvector_clear(chk_bitmap, b);
	}
	// check data block
	for (i = 0; i < ospfs_super->os_ninodes; ++ i) {
		inode = ospfs_inode(i);
		if (inode->oi_nlink > 0 && inode->oi_ftype != OSPFS_FTYPE_SYMLINK) {
			uint32_t offset = 0;
			while (offset < inode->oi_size) {
				uint32_t *pb = ospfs_inode_blockno_p(inode, offset);
				if (pb) {
					//eprintk("Block %d ussed by inode %d (offset %d)\n", *pb, i, offset);
					if (!fixer_is_legal_datab(*pb)) {
						eprintk("Illegel data block: inode %d, block %d\n", i, *pb);
						// reset to 0
						*pb = 0;	
						r = -1;
					} else {

						bitvector_clear(chk_bitmap, *pb);
					}
				} else {
				}	
				offset += OSPFS_BLKSIZE;
			}
			if (inode->oi_indirect) {
				bitvector_clear(chk_bitmap, inode->oi_indirect);
			}
			if (inode->oi_indirect2) {
				uint32_t i;
				bitvector_clear(chk_bitmap, inode->oi_indirect2);
				
				for (i = 0; i < OSPFS_BLKSIZE / sizeof(uint32_t); i ++) {
					uint32_t *p_indir_b = ((uint32_t *) ospfs_block(inode->oi_indirect2)) + i;
					if (*p_indir_b) {
						bitvector_clear(chk_bitmap, *p_indir_b);
					
					}
				}
			}
		}
	}

	eprintk("Checking: bitmap\n");
	
	for (b = 0; b < ospfs_super->os_nblocks; ++ b) {
		if (bitvector_test(fs_bitmap, b) && !bitvector_test(chk_bitmap, b)) {			
			eprintk("Bitmap error: block %d\n", b);
			r = -1;
			bitvector_clear(fs_bitmap, b);
		} else if (!bitvector_test(fs_bitmap, b) && bitvector_test(chk_bitmap, b)) {
			eprintk("Bitmap error: block %d\n", b);
			r = -1;
			bitvector_set(fs_bitmap, b);
		}
	}

	kfree(chk_bitmap);
	return r;
}


static inline int 
fixer_is_legal_fsize(uint32_t fsize) {
	if (fsize <= OSPFS_MAXFILESIZE) {
		return 1;
	}
	return 0;
}

static inline int 
fixer_is_legal_ftype(uint32_t ftype) {
	if (ftype == OSPFS_FTYPE_REG || ftype == OSPFS_FTYPE_DIR 
			|| ftype == OSPFS_FTYPE_SYMLINK) {
		return 1;
	}
	return 0;
}


static void
fixer_free_block(ospfs_inode_t *oi) {
	void *bitmap = &ospfs_data[OSPFS_BLKSIZE*2];
	uint32_t blockno;

	// free blocks
	for(blockno = 0; blockno != OSPFS_NDIRECT; blockno++)
		if(oi->oi_direct[blockno]!=0 && !bitvector_test(bitmap, oi->oi_direct[blockno]))
			bitvector_set (bitmap, oi->oi_direct[blockno]);
	if(oi->oi_indirect!=0)
	{
		uint32_t *block = ospfs_block(oi->oi_indirect);
		for(blockno = 0; blockno != OSPFS_NINDIRECT; blockno++)
		{
			if(block[blockno]!=0 && !bitvector_test(bitmap, block[blockno]))
				bitvector_set (bitmap, block[blockno]);
		}
		oi->oi_indirect = 0;
	}
	if(oi->oi_indirect2!=0)
	{
		uint32_t *doubly_block = ospfs_block(oi->oi_indirect2);
		uint32_t count = 0;
		while(doubly_block[count]!=0)
		{
			uint32_t *block = ospfs_block(doubly_block[count]);
			for(blockno = 0; blockno != OSPFS_NINDIRECT; blockno++)
			{
				if(block[blockno]!=0 && !bitvector_test(bitmap, block[blockno]))
					bitvector_set (bitmap, block[blockno]);
			}
			doubly_block[count] = 0;
			count++;
		}
		oi->oi_indirect2 = 0;
	}
}

static int 
fixer_check_inodes(void) {
	int r = 0;
	uint32_t i, j, entry_off;
	ospfs_inode_t *oi, *dir_oi;

	eprintk("Checking: inodes\n");

	for (i = 0; i < ospfs_super->os_ninodes; ++ i) {		
		oi = ospfs_inode(i);
		if (oi->oi_nlink > 0) {
			int r_io = 0; 

			if (!fixer_is_legal_fsize(oi->oi_size)) {
				eprintk("Too large file: inode %d\n", i);
				r_io = -1;
				r = -1;
			}

			if (!fixer_is_legal_ftype(oi->oi_ftype)) {
				eprintk("Invalid file type: inode %d\n", i);
				r_io = -1;
				r = -1;
			}

			// for problemetic inode, directly release it && its blocks
			// also remove related dir entires
			if (r_io < 0) {
				oi->oi_nlink = 0;
				fixer_free_block(oi);

				for (j = 0; j < ospfs_super->os_ninodes; j ++) {
					dir_oi = ospfs_inode(j);
					if (dir_oi->oi_ftype == OSPFS_FTYPE_DIR) {
						for (entry_off = 0; entry_off < dir_oi->oi_size; entry_off += OSPFS_DIRENTRY_SIZE) {
							ospfs_direntry_t *od = ospfs_inode_data(dir_oi, entry_off);
							if (od->od_ino == i) {
								eprintk("File %s is ralted with error inote %d\n", od->od_name, i);
								od->od_ino = 0;
							}
						}

					}
				}

			}
		}
	}
	return r;
}


static int 
fixer_check_dir_entries(void) {
	int r = 0;
	uint32_t *chk_nlink;
	uint32_t i, entry_off;
	ospfs_inode_t *dir_oi, *oi;

	eprintk("Checking dir entries\n");

	chk_nlink = (uint32_t *) kmalloc(sizeof(uint32_t) * ospfs_super->os_ninodes, GFP_ATOMIC);
	memset(chk_nlink, 0, sizeof(uint32_t) * ospfs_super->os_ninodes);

	// go through all the dir entires
	for (i = 0; i < ospfs_super->os_ninodes; i ++) {
		dir_oi = ospfs_inode(i);
		if (dir_oi->oi_ftype == OSPFS_FTYPE_DIR) {
			for (entry_off = 0; entry_off < dir_oi->oi_size; entry_off += OSPFS_DIRENTRY_SIZE) {
				ospfs_direntry_t *od = ospfs_inode_data(dir_oi, entry_off);
				if (od->od_ino > 0) {
					chk_nlink[od->od_ino] ++;
				}
			}

		}
	}

	for (i = 1; i < ospfs_super->os_ninodes; ++ i) {
		oi = ospfs_inode(i);
		if (oi->oi_ftype != OSPFS_FTYPE_DIR && oi->oi_nlink != chk_nlink[i]) {
			eprintk("Mismatched nlink: inode %d\n", i);			
			oi->oi_nlink = chk_nlink[i];
			if (chk_nlink[i] == 0) {
				// free block if necessary
				fixer_free_block(oi);			
			}
			r = -1;		
		}
	}

	kfree(chk_nlink);

	return r;
}

static int 
fixer_fix(void) {
	int r = 0;
	if ((r = fixer_check_super()) < 0) {
		// cannot fix superblcok corruption; exit directly
		return r;
	}	

	if (fixer_check_inodes() < 0) {
		r = -1;
	}

	if (fixer_check_dir_entries() < 0) {
		r = -1;
	}

	if (fixer_check_block_usage() < 0) {
		r = -1;
	}


	if (r == 0) {
		eprintk("Everything is OK\n");
	}
	return r;
}

static int
fixer_err_super(void) {
	ospfs_super->os_magic = 1;
	return 0;
}

static int
fixer_err_illegal_data_block(void) {
	uint32_t i;
	int done = 0;
	ospfs_inode_t *oi = NULL;

	for (i = 1; i < ospfs_super->os_ninodes; i ++) {
		oi = ospfs_inode(i);
		if (oi->oi_ftype == OSPFS_FTYPE_REG) {
			eprintk("Set 1st direct block of inode %d to be block #1 (superblock)\n", i);
			oi->oi_direct[0] = 1;
			done = 1;					
		}
		if (done) {
			break;
		}
	}
	return 0;
}

static int
fixer_err_mismatch_bitmap(void) {
	uint32_t i;
	int done = 0;
	ospfs_inode_t *oi = NULL;

	for (i = 1; i < ospfs_super->os_ninodes; i ++) {
		oi = ospfs_inode(i);
		if (oi->oi_ftype == OSPFS_FTYPE_REG) {
			void *bitmap = &ospfs_data[OSPFS_BLKSIZE*2];
			eprintk("Set 1st direct block of inode %d (block #%d) to be unused in bitmap\n", i, oi->oi_direct[0]);
			bitvector_set(bitmap, oi->oi_direct[0]);			
			done = 1;					
		}
		if (done) {
			break;
		}
	}
	return 0;
}


static int
fixer_err_illegal_inode(void) {
	uint32_t i;
	int done = 0;
	ospfs_inode_t *oi = NULL;

	for (i = 1; i < ospfs_super->os_ninodes; i ++) {
		oi = ospfs_inode(i);
		if (oi->oi_ftype == OSPFS_FTYPE_REG) {
			eprintk("Set the file type inode %d to 4 (illegal value) \n", i);
			oi->oi_ftype = 4;
			done = 1;					
		}
		if (done) {
			break;
		}
	}
	return 0;

}

static int
fixer_err_mismatch_link(void) {
	uint32_t i;
	int done = 0;
	ospfs_inode_t *oi = NULL;

	for (i = 1; i < ospfs_super->os_ninodes; i ++) {
		oi = ospfs_inode(i);
		if (oi->oi_ftype == OSPFS_FTYPE_REG) {
			eprintk("Set nlink of inode %d to 0 (original value %d) \n", i, oi->oi_nlink);
			oi->oi_nlink = 0;
			done = 1;					
		}
		if (done) {
			break;
		}
	}
	return 0;
}

static int
fixer_execute_cmd(char *cmd) {
	int r = 0;
	if (strcmp(cmd, "fix") == 0) {
		eprintk("Starting file system fixer\n");
		r = fixer_fix();	
	} else if (strcmp(cmd, "err1") == 0) {
		eprintk("Generating error: superblock corruption\n");
		fixer_err_super();
	} else if (strcmp(cmd, "err2") == 0) {
		eprintk("Generating error: illegal data block\n");
		fixer_err_illegal_data_block();
	} else if (strcmp(cmd, "err3") == 0) {
		eprintk("Generating error: mismatch bitmap\n");
		fixer_err_mismatch_bitmap();
	} else if (strcmp(cmd, "err4") == 0) {
		eprintk("Generating error: illegal inode info\n");
		fixer_err_illegal_inode();
	} else if (strcmp(cmd, "err5") == 0) {
		eprintk("Generating error: mismatch link\n");
		fixer_err_mismatch_link();
	}

	return r;
}
// Define the file system operations structures mentioned above.

static struct file_system_type ospfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ospfs",
	.get_sb		= ospfs_get_sb,
	.kill_sb	= kill_anon_super
};

static struct inode_operations ospfs_reg_inode_ops = {
	.setattr	= ospfs_notify_change
};

static struct file_operations ospfs_reg_file_ops = {
	.llseek		= generic_file_llseek,
	.read		= ospfs_read,
	.write		= ospfs_write
};

static struct inode_operations ospfs_dir_inode_ops = {
	.lookup		= ospfs_dir_lookup,
	.link		= ospfs_link,
	.unlink		= ospfs_unlink,
	.create		= ospfs_create,
	.symlink	= ospfs_symlink
};

static struct file_operations ospfs_dir_file_ops = {
	.read		= generic_read_dir,
	.readdir	= ospfs_dir_readdir
};

static struct inode_operations ospfs_symlink_inode_ops = {
	.readlink	= generic_readlink,
	.follow_link	= ospfs_follow_link
};

static struct dentry_operations ospfs_dentry_ops = {
	.d_delete	= ospfs_delete_dentry
};

static struct super_operations ospfs_superblock_ops = {
};


// Functions used to hook the module into the kernel!

static int __init init_ospfs_fs(void)
{
	eprintk("Loading ospfs module...\n");
	return register_filesystem(&ospfs_fs_type);
}

static void __exit exit_ospfs_fs(void)
{
	unregister_filesystem(&ospfs_fs_type);
	eprintk("Unloading ospfs module\n");
}

	module_init(init_ospfs_fs)
module_exit(exit_ospfs_fs)

	// Information about the module
	MODULE_AUTHOR("Skeletor");
	MODULE_DESCRIPTION("OSPFS");
	MODULE_LICENSE("GPL");
