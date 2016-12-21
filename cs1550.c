//////////////////////////////////////////////////////////////////////////
///////////// CS 1550 PROJECT 4: FILE SYSTEM BY PETER STAMOS /////////////
//////////////////////////////////////////////////////////////////////////

/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.
*/

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

//How many files can there be in one directory?
#define MAX_FILES_IN_DIR (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))
#define NODE_POINTERS (BLOCK_SIZE - sizeof(int) - sizeof(long)) / sizeof(long)
#define END_OF_BITMAP (5 * BLOCK_SIZE - 1)

//////////////////////////////////////////////////////////////////////////
/////////////////////////// SECTION COMPLETE /////////////////////////////
//////////////////////////////////////////////////////////////////////////

struct cs1550_bitmap {

	unsigned char bitmap[5 * BLOCK_SIZE];
};

//////////////////////////////////////////////////////////////////////////
/////////////////////////// SECTION COMPLETE /////////////////////////////
//////////////////////////////////////////////////////////////////////////

//The attribute packed means to not align these things
struct cs1550_directory_entry
{
	int nFiles;	//How many files are in this directory.
				//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;					//file size
		long nStartBlock;				//where the first block is on disk
	} __attribute__((packed)) files[MAX_FILES_IN_DIR];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.
	char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory) - sizeof(int)];
};

//////////////////////////////////////////////////////////////////////////
/////////////////////////// SECTION COMPLETE /////////////////////////////
//////////////////////////////////////////////////////////////////////////

typedef struct cs1550_root_directory cs1550_root_directory;

#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

struct cs1550_root_directory
{
	int nDirectories;	//How many subdirectories are in the root
						//Needs to be less than MAX_DIRS_IN_ROOT
	struct cs1550_directory
	{
		char dname[MAX_FILENAME + 1];	//directory name (plus space for nul)
		long nStartBlock;				//where the directory block is on disk
	} __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.
	char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct cs1550_directory) - sizeof(int)];
} ;

//////////////////////////////////////////////////////////////////////////
/////////////////////////// SECTION COMPLETE /////////////////////////////
//////////////////////////////////////////////////////////////////////////

typedef struct cs1550_directory_entry cs1550_directory_entry;

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE - sizeof(long))

struct cs1550_disk_block
{
	//The next disk block, if needed. This is the next pointer in the linked
	//allocation list
	long nNextBlock;

	//And all the rest of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

//////////////////////////////////////////////////////////////////////////
/////////////////////////// SECTION COMPLETE /////////////////////////////
//////////////////////////////////////////////////////////////////////////

struct cs1550_node {
	int next_node;
	long value;
	long node_pointers[NODE_POINTERS];
};

//////////////////////////////////////////////////////////////////////////
/////////////////////////// SECTION COMPLETE /////////////////////////////
//////////////////////////////////////////////////////////////////////////

typedef struct cs1550_node cs1550_node;

static long directory_offset(FILE *file, char *dir) {

	cs1550_root_directory root;

	if (fseek(file, 0, SEEK_SET) == 0) {
		if (fread(&root, 1, BLOCK_SIZE, file) == BLOCK_SIZE) {

			int directories = root.nDirectories;
			int index;

			for (index = 0; index < directories; index++) {

				if (strcmp(dir, root.directories[index].dname) == 0) {
					return root.directories[index].nStartBlock;
				}
			}
		}
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////
/////////////////////////// SECTION COMPLETE /////////////////////////////
//////////////////////////////////////////////////////////////////////////

int write(FILE *file, cs1550_directory_entry entry, long location, char filename[MAX_FILENAME + 1], char extension[MAX_EXTENSION + 1], size_t size, long nStartBlock) {

	int current;

	if (entry.nFiles == MAX_FILES_IN_DIR) {
		return -1;
	}

	for (current = 0; current < MAX_FILES_IN_DIR; current++) {

		if (entry.files[current].fsize == -1) {

			strcpy(entry.files[current].fname, filename);
			strcpy(entry.files[current].fext, extension);

			entry.files[current].fsize = size;
			entry.files[current].nStartBlock = nStartBlock;

			entry.nFiles++;

			fseek(file, BLOCK_SIZE * location, SEEK_SET);
			fwrite(&entry, 1, BLOCK_SIZE, file);

			return 1;
		}
	}

	return -1;
}

//////////////////////////////////////////////////////////////////////////
/////////////////////////// SECTION COMPLETE /////////////////////////////
//////////////////////////////////////////////////////////////////////////

static int retrieve_block(FILE *file) {

	struct cs1550_bitmap bitmap;

	if (fread(&bitmap, 1, BLOCK_SIZE * 5, file) == (END_OF_BITMAP + 1)) {

		int index;
		int x;

		for (index = 1; index < END_OF_BITMAP; index++) {

			if (bitmap.bitmap[index] < 255) {

				unsigned char bits = 128;
				unsigned char mark = bitmap.bitmap[index] + 1;

				for (x = 7; (bits ^ mark) != 0; bits >>= 1, x--);

				x += (index * 8);
				bitmap.bitmap[index] = bitmap.bitmap[index] | mark;

				break;
			}
		}

		if (fwrite(bitmap.bitmap, 1, BLOCK_SIZE * 5, file) == END_OF_BITMAP + 1) {
			return x;
		}
	}

	return -1;
}

//////////////////////////////////////////////////////////////////////////
/////////////////////////// SECTION COMPLETE /////////////////////////////
//////////////////////////////////////////////////////////////////////////

int add_node(FILE *file, cs1550_node node, long this_node_location, long next_node_location) {

	if (node.value == 0) {

		node.node_pointers[node.next_node] = next_node_location;
		node.next_node++;

		if (fseek(file, this_node_location * BLOCK_SIZE, SEEK_SET) == 0) {
			if (fwrite(&node, 1, BLOCK_SIZE, file) == BLOCK_SIZE) {
				return 1;
			}
		}
	}

	return -1;
}

//////////////////////////////////////////////////////////////////////////
/////////////////////////// SECTION COMPLETE /////////////////////////////
//////////////////////////////////////////////////////////////////////////

typedef struct cs1550_disk_block cs1550_disk_block;

/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not.
 *
 * man -s 2 stat will show the fields of a stat structure
 */

static int cs1550_getattr(const char *path, struct stat *stbuf) {

	int directory_files
	int file_location;
	int index = 0;
	int res = 0;

	long directory_location = -1;
	long file_start = -1;

	cs1550_root_directory root;
	cs1550_directory_entry entry;

	FILE *file;

	memset(stbuf, 0, sizeof(struct stat));

	char extension[MAX_EXTENSION + 1];
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];

	memset(extension, 0, MAX_EXTENSION + 1);
	memset(directory, 0, MAX_FILENAME + 1);
	memset(filename, 0, MAX_FILENAME + 1);

	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	//is path the root dir?
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	}

	else {
		if (directory[0] && directory[MAX_FILENAME] == '\0' && filename[MAX_FILENAME] == '\0' && extension[MAX_EXTENSION] == '\0') {

			file = fopen(".disk", "rb");

			if (file) {

				directory_location = directory_offset(file, directory);

				if (directory_location) {
					if (filename[0] == '\0') {

						//Might want to return a structure with these fields
						stbuf->st_mode = S_IFDIR | 0755;
						stbuf->st_nlink = 2;
						res = 0; //no error
					}

					else {
						if (fseek(file, BLOCK_SIZE *directory_location, SEEK_SET) == 0) {
							if (fread(&entry, 1, BLOCK_SIZE, file) == BLOCK_SIZE) {

								directory_files = entry.nFiles;

								for (file_location = 0; file_location < directory_files; file_location++) {

									if ((strcmp(extension, entry.files[file_location].fext) == 0) && (strcmp(filename, entry.files[file_location].fname) == 0)) {
										file_start = root.directories[index].nStartBlock;
									}
								}

								if (file_start == -1) {

									fclose(file);
									return -ENOENT;
								}

								else {

									//regular file, probably want to be read and write
									stbuf->st_mode = S_IFREG | 0666;
									stbuf->st_nlink = 1; //file links
									stbuf->st_size = entry.files[file_start].fsize; //file size - make sure you replace with real size!
									res = 0; // no error
								}
							}
						}
					}
				}
			}

			else {
				res = -1;
			}

			fclose(file);
		}

		else {
			//Else return that path doesn't exist
			res = -ENOENT;
		}
	}

	return res;
}

//////////////////////////////////////////////////////////////////////////
/////////////////////////// SECTION COMPLETE /////////////////////////////
//////////////////////////////////////////////////////////////////////////

/*
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */

static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	//Since we're building with -Wall (all warnings reported) we need
	//to "use" every parameter, so let's just cast them to void to
	//satisfy the compiler
	(void) offset;
	(void) fi;

	int directory_files, index;
	int res = -ENOENT;
	long directory_location = -1;

	cs1550_root_directory root;
	cs1550_directory_entry entry;

	FILE *file;

	char extension[MAX_EXTENSION + 1];
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];

	memset(extension, 0, MAX_EXTENSION + 1);
	memset(directory, 0, MAX_FILENAME + 1);
	memset(filename, 0, MAX_FILENAME + 1);

	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	char temporary[MAX_EXTENSION + MAX_FILENAME + 2];

	//the filler function allows us to add entries to the listing
	//read the fuse.h file for a description (in the ../include dir)
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	//This line assumes we have no subdirectories, need to change
	if (strcmp(path, "/") != 0) {
		if (directory[0] && directory[MAX_FILENAME] == '\0' && extension[MAX_EXTENSION] == '\0' && filename[MAX_FILENAME] == '\0') {

			file = fopen(".disk", "rb");

			directory_location = directory_offset(file, directory);

			if (directory_location) {
				if (fseek(file, directory_location * BLOCK_SIZE, SEEK_SET) == 0) {
					if (fread(&file, 1, BLOCK_SIZE, file) == BLOCK_SIZE) {

						directory_files = entry.nFiles;

						for (index = 0; index < directory_files; index++) {

							strcpy(temporary, entry.files[index].fname);

							if (entry.files[index].fext[0]) {

								strcat(temporary, ".");
								strcat(temporary, entry.files[index].fext);
							}

							filler(buf, temporary, NULL, 0);
						}

						res = 0;
					}
				}
			}

			fclose(file);
		}

		else {
			return -ENOENT;
		}
	}

	else {

		file = fopen(".disk", "rb");

		if (file && (fread(&root, 1, BLOCK_SIZE, file) == BLOCK_SIZE)) {

			int directories = root.nDirectories;

			for (index = 0; index < directories; index++) {
				filler(buf, root.directories[index].dname, NULL, 0);
			}

			res = 0;
		}

		fclose(file);
	}

	/*
	//add the user stuff (subdirs or files)
	//the +1 skips the leading '/' on the filenames
	filler(buf, newpath + 1, NULL, 0);
	*/
	return res;
}

//////////////////////////////////////////////////////////////////////////
/////////////////////////// SECTION COMPLETE /////////////////////////////
//////////////////////////////////////////////////////////////////////////

/*
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */

static int cs1550_mkdir(const char *path, mode_t mode) {

	(void) path;
	(void) mode;

	int index, directories;
	long start;

	struct cs1550_bitmap bit_map;

	cs1550_root_directory root;
	cs1550_directory_entry entry;

	char extension[MAX_EXTENSION + 1];
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];

	unsigned char x = 128;
	unsigned char map;

	memset(extension, 0, MAX_EXTENSION + 1);
	memset(directory, 0, MAX_FILENAME + 1);
	memset(filename, 0, MAX_FILENAME + 1);

	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	FILE *file = fopen(".disk", "r+b");

	size_t bytes = 0;

	bytes = fread(&root, sizeof(cs1550_root_directory), 1, file);

	if (filename[0] == '/') {
		return -EPERM;
	}

	if (directory[MAX_FILENAME]) {
		return -ENAMETOOLONG;
	}

	if (directory[0] && filename[0] == '\0' && directory[MAX_FILENAME] == '\0') {

		directories = root.nDirectories;

		for (index = 0; index < directories; index++) {

			if (strcmp(directory, root.directories[index].dname) == 0) {
				return -EEXIST;
			}
		}

		if (!fseek(file, -BLOCK_SIZE * 5, SEEK_END)) {

			fread(&bit_map, 1, BLOCK_SIZE * 5, file);

			start = 0;

			for (index = 1; index < END_OF_BITMAP; index++) {

				if (bit_map.bitmap[index] < 255) {

					map = bit_map.bitmap[index] + 1;

					for (start = 7; (map^x) != 0; x>>=1, start--);

					start += (index * 8);

					bit_map.bitmap[index] = bit_map.bitmap[index] | map;

					fwrite(&bit_map, 1, BLOCK_SIZE * 5, file);

					break;
				}
			}

			if (start != 0) {

				fseek(file, 0, SEEK_SET);
				fread(&root, 1, BLOCK_SIZE, file);

				root.directories[root.nDirectories].nStartBlock = start;

				strcpy(root.directories[root.nDirectories].dname, directory);

				root.nDirectories++;

				fseek(file, 0, SEEK_SET);
				fwrite(&root, 1, BLOCK_SIZE, file);

				fseek(file, start * BLOCK_SIZE, SEEK_SET);
				fwrite(&entry, 1, BLOCK_SIZE, file);
			}
		}
	}

	fclose(file);
	return 0;
}

//////////////////////////////////////////////////////////////////////////
/////////////////////////// SECTION COMPLETE /////////////////////////////
//////////////////////////////////////////////////////////////////////////


/*
 * Removes a directory.
 */

static int cs1550_rmdir(const char *path)
{
	(void) path;
    return 0;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////// SECTION COMPLETE ////////////////////////////
//////////////////////////////////////////////////////////////////////////


/*
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */

static int cs1550_mknod(const char *path, mode_t mode, dev_t dev) {

	(void) mode;
	(void) dev;

	int nStartBlock;
	int res = -1;
	int count;
	int flag;

	long location;

	cs1550_directory_entry entry;

	char extension[MAX_EXTENSION + 1];
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];

	memset(extension, 0, MAX_EXTENSION + 1);
	memset(directory, 0, MAX_FILENAME + 1);
	memset(filename, 0, MAX_FILENAME + 1);

	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	FILE *file = fopen(".disk", "rb+");

	if (strcmp(path, "/") != 0) {

		if (directory[MAX_FILENAME]) {
			res = -ENAMETOOLONG;
		}

		else if (directory[0] && (directory[MAX_FILENAME] == '\0') && (filename[MAX_FILENAME] == '\0')) {

			if (file) {

				if (fseek(file, BLOCK_SIZE * -5, SEEK_END) == 0) {

					nStartBlock = retrieve_block(file);

					location = directory_offset(file, directory);

					printf("Directory: %s, Directory Location: %d\n", directory, location);

					if (location) {
						if (fseek(file, BLOCK_SIZE * location, SEEK_SET) == 0) {
							if (fread(&entry, 1, BLOCK_SIZE, file) == BLOCK_SIZE) {

								int index;

								printf("Directory Located");

								count = entry.nFiles;

								for (index = 0; index < count; index++) {

									if (strcmp(entry.files[index].fext, extension) == 0 && strcmp(entry.files[index].fname, filename) == 0) {

										fclose(file);
										return -EEXIST;
									}
								}

								flag = write(file, entry, location, filename, extension, 0, nStartBlock);

								if (flag < 0) {
									return -1;
								}

								cs1550_node new_node;

								new_node.next_node = 0;
								new_node.value = 0;

								if (fseek(file, nStartBlock * BLOCK_SIZE, SEEK_SET) == 0) {
									if (fwrite(&new_node, 1, BLOCK_SIZE, file) == BLOCK_SIZE) {
										res = 0;
									}
								}
							}
						}
					}
				}
			}

			fclose(file);
		}
	}

	else {
		res = -EPERM;
	}

	return res;
}

//////////////////////////////////////////////////////////////////////////
/////////////////////////// SECTION COMPLETE /////////////////////////////
//////////////////////////////////////////////////////////////////////////

/*
 * Deletes a file
 */

static int cs1550_unlink(const char *path) {

    (void) path;

    return 0;
}

//////////////////////////////////////////////////////////////////////////
/////////////////////////// SECTION COMPLETE /////////////////////////////
//////////////////////////////////////////////////////////////////////////

/*
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {

	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	int directory_location;
	int file_location;
	int res = 0;
	int i;
	int x;

	long file_offset;

	cs1550_directory_entry entry;
	cs1550_node node;

	char extension[MAX_EXTENSION + 1];
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];

	memset(extension, 0, MAX_EXTENSION + 1);
	memset(directory, 0, MAX_FILENAME + 1);
	memset(filename, 0, MAX_FILENAME + 1);

	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	FILE *file;

	//check that size is > 0
	if (size <= 0) {
		return -1;
	}

	//check to make sure path exists
	if (strcmp(path, "/") != 0) {

		memset(&entry, 0, sizeof(cs1550_directory_entry));
		file = fopen(".disk", "rb+");

		if (file) {

			directory_location = directory_offset(file, directory);

			if (directory_location) {
				if (fseek(file, BLOCK_SIZE * directory_location, SEEK_SET) == 0) {
					if (fread(&entry, 1, BLOCK_SIZE, file) == BLOCK_SIZE) {
						for (file_location = 0; file_location < entry.nFiles; file_location++) {

							if (strcmp(entry.files[file_location].fname, filename) == 0 && strcmp(entry.files[file_location].fext, extension) == 0) {

								file_offset = entry.files[file_location].nStartBlock;
								break;
							}
						}

						if (file_location == entry.nFiles) {
							return 0;
						}

						if (fseek(file, file_offset * BLOCK_SIZE, SEEK_SET) == 0) {
							if (fread(&node, 1, BLOCK_SIZE, file) == BLOCK_SIZE) {

								x = size;

								while(x > 0) {

									cs1550_disk_block block;

									int index;
									int v;

									fseek(file, node.node_pointers[i] * BLOCK_SIZE, SEEK_SET);
									fread(&block, 1, BLOCK_SIZE, file);

									if (x <= BLOCK_SIZE) {
										v = size;
									}

									else {
										v = BLOCK_SIZE;
									}

									for (index = 0; index < v; index++) {

										*buf = block.data[index];
										buf++;
									}

									i++;

									res += strlen(block.data);
									x -= BLOCK_SIZE;
								}
							}
						}
					}
				}
			}

			fclose(file);
		}

		else {
			printf("Error Reading File");
		}
	}

	return res;
}

//////////////////////////////////////////////////////////////////////////
///////////////////////// SECTION NOT COMPLETE ///////////////////////////
//////////////////////////////////////////////////////////////////////////

/*
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {

	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	int directory_location;
	int file_location;
	int data_location;
	int res = 0;

	long file_offset;
	long x;

	cs1550_directory_entry entry;
	cs1550_node node;

	char extension[MAX_EXTENSION + 1];
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];

	memset(extension, 0, MAX_EXTENSION + 1);
	memset(directory, 0, MAX_FILENAME + 1);
	memset(filename, 0, MAX_FILENAME + 1);

	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	FILE *file;

	//check that size is > 0
	if (size <= 0) {
		return -1;
	}

	//check to make sure path exists
	if (strcmp(path, "/") != 0) {

		memset(&entry, 0, sizeof(cs1550_directory_entry));
		file = fopen(".disk", "rb+");

		if (file) {

			directory_location = directory_offset(file, directory);

			if (directory_location) {
				if (fseek(file, BLOCK_SIZE * directory_location, SEEK_SET) == 0) {
					if (fread(&entry, 1, BLOCK_SIZE, file) == BLOCK_SIZE) {
						for (file_location = 0; file_location < entry.nFiles; file_location++) {

							if (strcmp(entry.files[file_location].fname, filename) == 0 && strcmp(entry.files[file_location].fext, extension) == 0) {

								file_offset = entry.files[file_location].nStartBlock;
								break;
							}
						}

						if (file_location == entry.nFiles) {
							return 0;
						}

						if (offset > entry.files[file_location].fsize) {
							fclose(file);
							return -EFBIG;
						}

						if (fseek(file, file_offset * BLOCK_SIZE, SEEK_SET) == 0) {
							if (fread(&node, 1, BLOCK_SIZE, file) == BLOCK_SIZE) {

								data_location = offset;
								x = size;

								while (x > 0) {

									int v, location = retrieve_block(file);

									cs1550_disk_block block;

									block.nNextBlock = 0;

									if (x <= BLOCK_SIZE) {
										v = x;
									}

									else {
										v = BLOCK_SIZE;
									}

									for (data_location = 0; data_location < v; data_location++) {

										block.data[data_location] = buf[0];
										buf++;
									}

									add_node(file, node, file_offset, location);

									if (fseek(file, location * BLOCK_SIZE, SEEK_SET) == 0) {
										fwrite(&block, 1, BLOCK_SIZE, file);
									}

									data_location = 0;
									x -= MAX_DATA_IN_BLOCK;
								}

								entry.files[file_location].fsize = size;

								if (fseek(file, directory_location * BLOCK_SIZE, SEEK_SET) == 0) {
									fwrite(&entry, 1, BLOCK_SIZE, file);
								}

								res = size;
							}
						}
					}
				}
			}

			fclose(file);
		}

		else {
			printf("Error Writing File");
		}
	}

	return res;
}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/*
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
	.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
	.mknod	= cs1550_mknod,
	.unlink = cs1550_unlink,
	.truncate = cs1550_truncate,
	.flush = cs1550_flush,
	.open	= cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}

//////////////////////////////////////////////////////////////////////////
///////////// CS 1550 PROJECT 4: FILE SYSTEM BY PETER STAMOS /////////////
//////////////////////////////////////////////////////////////////////////
