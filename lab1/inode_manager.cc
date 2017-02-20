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
      temp = IBLOCK(i, bm->sb.nblocks);
      block_id = BBLOCK(temp);
      temp = temp%BPB;
      bm->read_block(block_id, buf);
      
      int pos = temp/8;
      int bit = temp%8;
      bool used = (buf[pos] >> (7-bit)) & 0x1;
      
      if(!used){
          buf[pos] = buf[pos] | (0x1 << (7 - bit));
          bm->write_block(block_id, buf);
          ino = new inode;
          ino->type = type;
          put_inode(i, ino);
          delete ino;
          return i;          
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
   printf("read_file\n");
   struct inode *ino = get_inode(inum);
   if(ino == NULL) return;

   int block_num = ((ino->size % BLOCK_SIZE) == 0) ? ino->size/BLOCK_SIZE : ino->size/BLOCK_SIZE + 1;
   *size = ino->size;
   buf_out = new char*[block_num];
   for(int i=0; i<block_num; i++){
       buf_out[i] = new char[BLOCK_SIZE];
   }

   if(block_num<=NDIRECT){
       for(int i=0; i<block_num; i++){
           bm->read_block(ino->blocks[i], buf_out[i]);
       }
   }
   else{
       int i;
       for(i=0; i<NDIRECT; i++){
           bm->read_block(ino->blocks[i], buf_out[i]);
       }

       char indir_block[BLOCK_SIZE];
       bm->read_block(ino->blocks[NDIRECT], indir_block);
       char *ptr = indir_block;

       for(; i<block_num; i++){
           bm->read_block(*((int*)ptr), buf_out[i]);
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
  printf("wirte file\n");
  int block_num = ((size % BLOCK_SIZE) == 0) ? size/BLOCK_SIZE : size/BLOCK_SIZE + 1;

   if((block_num < 0) || (block_num > (NDIRECT + NDIRECT))){
       printf("\tim: file size not supported\n");
       return;
   }

   struct inode *ino = get_inode(inum);
   ino->size = size;
   ino->type = extent_protocol::T_FILE;

   if(block_num<=NDIRECT){
       for(int i=0; i<block_num; i++){
           blockid_t block_id = bm->alloc_block();
           ino->blocks[i] = block_id;
           bm->write_block(block_id, buf + 4*i);
       }
   }
   else{
       int i;
       for(i=0; i<NDIRECT; i++){
           blockid_t block_id = bm->alloc_block();
           ino->blocks[i] = block_id;
           bm->write_block(block_id, buf + 4*i);
       }

       blockid_t indir = bm->alloc_block();
       ino->blocks[NDIRECT] = indir;
       blockid_t block_content[block_num-NDIRECT];

       for(; i<block_num; i++){
           blockid_t block_id = bm->alloc_block();
           block_content[i-NDIRECT] = block_id;
           bm->write_block(block_id, buf + 4*i);
       }

       char buffer[BLOCK_SIZE];
       memcpy(buffer, block_content, ((block_num-NDIRECT) * sizeof(blockid_t)));
       bm->write_block(indir, buffer);
   }

   put_inode(inum, ino);
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
}
