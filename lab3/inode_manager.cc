#include "inode_manager.h"

// disk layer -----------------------------------------

disk::disk()
{
  bzero(blocks, sizeof(blocks));
}

void
disk::read_block(blockid_t id, char *buf)
{
  /*
   *your lab1 code goes here.
   *if id is smaller than 0 or larger than BLOCK_NUM 
   *or buf is null, just return.
   *put the content of target block into buf.
   *hint: use memcpy
  */
  if((id < 0) || (id > BLOCK_NUM) || (buf == NULL))
    return;
  memcpy(buf, blocks[id], BLOCK_SIZE);
  return;
}

void
disk::write_block(blockid_t id, const char *buf)
{
  /*
   *your lab1 code goes here.
   *hint: just like read_block
  */
  if((id < 0) || (id > BLOCK_NUM) || (buf == NULL))
    return;
  memcpy(blocks[id], buf, BLOCK_SIZE);
  return;
}

// block layer -----------------------------------------

// Allocate a free disk block.
blockid_t
block_manager::alloc_block()
{
  /*
   * your lab1 code goes here.
   * note: you should mark the corresponding bit in block bitmap when alloc.
   * you need to think about which block you can start to be allocated.

   *hint: use macro IBLOCK and BBLOCK.
          use bit operation.
          remind yourself of the layout of disk.
   */
  blockid_t block_id = (sb.nblocks)/BPB + INODE_NUM/IPB + 3;
  
  for(; block_id<sb.nblocks; block_id++){
      uint32_t bitmap_block = block_id/BPB + 2;
      uint32_t bitmap_loc = block_id%BPB;
      uint32_t bitmap_pos = bitmap_loc/8;
      uint32_t bitmap_bit = bitmap_loc%8;
      
      char buf[BLOCK_SIZE];
      read_block(bitmap_block, buf);
      
      bool used = (buf[bitmap_pos] >> (7-bitmap_bit)) & 0x1;
      if(!used){
          buf[bitmap_pos] = buf[bitmap_pos] | (0x1 << (7 - bitmap_bit));
          write_block(bitmap_block, buf);
          return block_id;
      }
  }
  
  printf("\tim: block is not enough\n");
  return -1;
}

void
block_manager::free_block(uint32_t id)
{
  /* 
   * your lab1 code goes here.
   * note: you should unmark the corresponding bit in the block bitmap when free.
   */
   blockid_t limit = (sb.nblocks)/BPB + INODE_NUM/IPB + 3;
   if((id<limit) || (id>sb.nblocks)){
       printf("\tim: block not exist when free block\n");
       return;
   }
   
   uint32_t bitmap_block = id/BPB + 2;
   uint32_t bitmap_loc = id%BPB;
   uint32_t bitmap_pos = bitmap_loc/8;
   uint32_t bitmap_bit = bitmap_loc%8;
   
   char buf[BLOCK_SIZE];
   read_block(bitmap_block, buf);
   
   buf[bitmap_pos] = buf[bitmap_pos] & (~(0x1 << (7 - bitmap_bit)));
   write_block(bitmap_block, buf);
   
   return;
}

// The layout of disk should be like this:
// |<-sb->|<-free block bitmap->|<-inode table->|<-data->|
block_manager::block_manager()
{
  d = new disk();

  // format the disk
  sb.size = BLOCK_SIZE * BLOCK_NUM;
  sb.nblocks = BLOCK_NUM;
  sb.ninodes = INODE_NUM;

}

void
block_manager::read_block(uint32_t id, char *buf)
{
  d->read_block(id, buf);
}

void
block_manager::write_block(uint32_t id, const char *buf)
{
  d->write_block(id, buf);
}

// inode layer -----------------------------------------

inode_manager::inode_manager()
{
  bm = new block_manager();
  uint32_t root_dir = alloc_inode(extent_protocol::T_DIR);
  if (root_dir != 1) {
    printf("\tim: error! alloc first inode %d, should be 1\n", root_dir);
    exit(0);
  }
}

/* Create a new file.
 * Return its inum. */
uint32_t
inode_manager::alloc_inode(uint32_t type)
{
  /* 
   * your lab1 code goes here.
   * note: the normal inode block should begin from the 2nd inode block.
   * the 1st is used for root_dir, see inode_manager::inode_manager().
    
   * if you get some heap memory, do not forget to free it.
   */
  struct inode *ino;
  char buf[BLOCK_SIZE];
  blockid_t block_id, temp;
  for(uint32_t i=1; i<INODE_NUM; i++){
      ino = get_inode(i);
      if(ino == NULL){
          ino = new inode;
          ino->type = type;
          ino->size = 0;
          ino->atime = 0;
          ino->ctime = 0;
          ino->mtime = 0;
          put_inode(i, ino);
          delete ino;
          return i;
      }
      else {
          delete ino;
          continue;
      }
  }
  printf("\tim: inode number is not enough\n");
  return -1;
}

void
inode_manager::free_inode(uint32_t inum)
{
  /* 
   * your lab1 code goes here.
   * note: you need to check if the inode is already a freed one;
   * if not, clear it, and remember to write back to disk.
   * do not forget to free memory if necessary.
   */
   struct inode* ino = NULL;
   char buf[BLOCK_SIZE];
   blockid_t block_id, temp;
   
   temp = IBLOCK(inum, bm->sb.nblocks);
   block_id = BBLOCK(temp);
   temp = temp%BPB;
   bm->read_block(block_id, buf);
      
   int pos = temp/8;
   int bit = temp%8;
   bool used = (buf[pos] >> (7-bit)) & 0x1;
   if(!used) return;
   
   ino = get_inode(inum);
   memset(ino, 0, sizeof(ino));
   put_inode(inum, ino);
   delete ino;
   
   buf[pos] = buf[pos] | (0x0 << (7 - bit));
   bm->write_block(block_id, buf);
   return;
}


/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
struct inode* 
inode_manager::get_inode(uint32_t inum)
{
  struct inode *ino, *ino_disk;
  char buf[BLOCK_SIZE];

  printf("\tim: get_inode %d\n", inum);

  if (inum < 0 || inum >= INODE_NUM) {
    printf("\tim: inum out of range\n");
    return NULL;
  }

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  // printf("%s:%d\n", __FILE__, __LINE__);

  ino_disk = (struct inode*)buf + inum%IPB;
  if (ino_disk->type == 0) {
    printf("\tim: inode not exist\n");
    return NULL;
  }

  ino = (struct inode*)malloc(sizeof(struct inode));
  *ino = *ino_disk;

  return ino;
}

void
inode_manager::put_inode(uint32_t inum, struct inode *ino)
{
  char buf[BLOCK_SIZE];
  struct inode *ino_disk;

  printf("\tim: put_inode %d\n", inum);
  if (ino == NULL)
    return;

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  ino_disk = (struct inode*)buf + inum%IPB;
  *ino_disk = *ino;
  bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);
}

#define MIN(a,b) ((a)<(b) ? (a) : (b))

/* Get all the data of a file by inum. 
 * Return alloced data, should be freed by caller. */
void
inode_manager::read_file(uint32_t inum, char **buf_out, int *size)
{
  /*
   * your lab1 code goes here.
   * note: read blocks related to inode number inum,
   * and copy them to buf_out
   */
   struct inode *ino = get_inode(inum);
   if(ino == NULL) return;
   
   int block_num = ((ino->size % BLOCK_SIZE) == 0) ? ino->size/BLOCK_SIZE : ino->size/BLOCK_SIZE + 1;
   *size = ino->size;
   
   *buf_out = new char[*size+1];
   char *pos = *buf_out;
   pos[*size]='\0';
   int asize = *size;
   char block_content[BLOCK_SIZE];
   
   if(block_num<=NDIRECT){
       for(int i=0; i<block_num; i++){
           if(asize <= BLOCK_SIZE){
               bm->read_block(ino->blocks[i], block_content);
               memcpy(pos, block_content, asize);
               asize = 0;
           }
           else{
               bm->read_block(ino->blocks[i], pos);
               asize -= BLOCK_SIZE;
               pos += BLOCK_SIZE;
           }
       }
   }  
   else{
       int i;
       for(i=0; i<NDIRECT; i++){
           if(asize <= BLOCK_SIZE){
               bm->read_block(ino->blocks[i], block_content);
               memcpy(pos, block_content, asize);
               asize = 0;
           }
           else{
               bm->read_block(ino->blocks[i], pos);
               asize -= BLOCK_SIZE;
               pos += BLOCK_SIZE;
           }
       }
       
       char indir_block[BLOCK_SIZE];
       bm->read_block(ino->blocks[NDIRECT], indir_block);
       char *ptr = indir_block;       
       
       for(; i<block_num; i++){
           if(asize <= BLOCK_SIZE){
               bm->read_block(*((int*)ptr), block_content);
               memcpy(pos, block_content, asize);
               asize = 0;
           }
           else{
               bm->read_block(*((int*)ptr), pos);
               asize -= BLOCK_SIZE;
               pos += BLOCK_SIZE;
           }
           
           ptr+=4;
       }
   }
   
   return;
}

/* alloc/free blocks if needed */
void
inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
  /*
   * your lab1 code goes here.
   * note: write buf to blocks of inode inum.
   * you need to consider the situation when the size of buf 
   * is larger or smaller than the size of original inode.
   * you should free some blocks if necessary.
   */
    inode_t* inode = get_inode(inum);
	bool prev_indir_used = ((inode->size/BLOCK_SIZE) > NDIRECT);
	uint32_t prev_size = inode->size;
	uint32_t prev_num = prev_size / BLOCK_SIZE + 1;
	if (prev_size % BLOCK_SIZE == 0) prev_num -= 1;

	inode->size = MIN(MAXFILE *BLOCK_SIZE, (unsigned)size);
	
	
	uint32_t block_num = inode->size / BLOCK_SIZE + 1;
	if (inode->size % BLOCK_SIZE == 0) block_num -= 1;
	bool now_indir_used = (block_num > NDIRECT); 
	
	if (prev_indir_used) { 
		uint32_t indirect_block = inode->blocks[NDIRECT];
		char indir_content[BLOCK_SIZE];
		bm->read_block(indirect_block, indir_content);
		uint32_t* indir_blockid = (uint32_t*)indir_content;
		for (uint32_t i = 0; i < prev_num - NDIRECT; i++)
			bm->free_block(indir_blockid[i]);	
		bm->free_block(indirect_block);						
	}

	for (uint32_t i = 0; i < prev_num && i < NDIRECT; i++)
		bm->free_block(inode->blocks[i]);
	
    char indirect_buffer[BLOCK_SIZE];
	uint32_t* indir_blocks;

	if (now_indir_used) {
		inode->blocks[NDIRECT] = bm->alloc_block();
		bm->read_block(inode->blocks[NDIRECT], indirect_buffer);
		indir_blocks = (uint32_t*)indirect_buffer;
	}


    uint32_t remain_size = inode->size;
	uint32_t total = 0;	
	uint32_t size_to_write = 0;	
	uint32_t write_block = 0; 
	uint32_t block_allocated = 0; 
	char write_buffer[BLOCK_SIZE];

	for (total = 0; write_block < block_num; total += size_to_write, remain_size -= size_to_write) {
		size_to_write = MIN(remain_size, BLOCK_SIZE);

		if (write_block < NDIRECT) {
			block_allocated = bm->alloc_block();
			inode->blocks[write_block] = block_allocated;
		}

		else {
			block_allocated = bm->alloc_block();
			indir_blocks[write_block - NDIRECT] = block_allocated;
			bm->write_block(inode->blocks[NDIRECT], indirect_buffer);	
		}
		memset(write_buffer, 0, BLOCK_SIZE);
		memcpy(write_buffer, buf, size_to_write);
		bm->write_block(block_allocated, write_buffer);
		buf += size_to_write;
		write_block++;
  }

  inode->ctime = inode->mtime = time(NULL);
  put_inode(inum, inode);
  free(inode);
  return;
}

void
inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
  /*
   * your lab1 code goes here.
   * note: get the attributes of inode inum.
   * you can refer to "struct attr" in extent_protocol.h
   */
   struct inode *ino = get_inode(inum);
   if(ino == NULL) return;
   a.type = ino->type;
   a.atime = ino->atime;
   a.mtime = ino->mtime;
   a.ctime = ino->ctime;
   a.size = ino->size;
   return;
}

void
inode_manager::remove_file(uint32_t inum)
{
  /*
   * your lab1 code goes here
   * note: you need to consider about both the data block and inode of the file
   * do not forget to free memory if necessary.
   */
   struct inode* ino = NULL;
   char buf[BLOCK_SIZE];
   blockid_t block_id, temp;
   
   temp = IBLOCK(inum, bm->sb.nblocks);
   block_id = BBLOCK(temp);
   temp = temp%BPB;
   bm->read_block(block_id, buf);
      
   int pos = temp/8;
   int bit = temp%8;
   bool used = (buf[pos] >> (7-bit)) & 0x1;
   if(!used) return;
   
   ino = get_inode(inum);
   int block_num = ((ino->size % BLOCK_SIZE) == 0) ? ino->size/BLOCK_SIZE : ino->size/BLOCK_SIZE + 1;
   
   if(block_num<=NDIRECT) {
       for (int i = 0; i < block_num; i++){
           bm->free_block(ino->blocks[i]);
       }
   }
   else{
       int i;
       for(i=0; i<NDIRECT; i++){
           bm->free_block(ino->blocks[i]);
       }
       
       char indir_block[BLOCK_SIZE];
       bm->read_block(ino->blocks[NDIRECT], indir_block);
       char *ptr = indir_block; 
       
       for(; i<block_num; i++){
           bm->free_block(*((int*)ptr));
           ptr+=4;
       }
   }
   
   free_inode(inum);
}
