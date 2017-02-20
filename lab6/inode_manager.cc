#include "inode_manager.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#define NPAR 4
#define TRUE 1
#define FALSE 0

pthread_mutex_t lock;

typedef unsigned long BIT32;
typedef unsigned short BIT16;

#define MAXDEG (NPAR*2)

int pBytes[MAXDEG];

int synBytes[MAXDEG];

int genPoly[MAXDEG * 2];

int DEBUG = FALSE;

void initialize_ecc(void);
int check_syndrome(void);
void decode_data(unsigned char data[], int nbytes);
void encode_data(unsigned char msg[], int nbytes, unsigned char dst[]);
static void calculate_poly(int nbytes, int genpoly[]);

BIT16 crc_ccitt(unsigned char *msg, int len);

int gexp[512];
int glog[256];

void init_galois_tables(void);
int ginv(int elt);
int galois_multiply(int a, int b);


int correct_errors(unsigned char codeword[], int csize, int nerasures, int erasures[]);

void add_polys(int dst[], int src[]);
void scale_poly(int k, int poly[]);
void mult_polys(int dst[], int p1[], int p2[]);

void copy_poly(int dst[], int src[]);
void zero_poly(int poly[]);

static int Lambda[MAXDEG];

static int Omega[MAXDEG];

static int compute_discrepancy(int lambda[], int S[], int L, int n);
static void init_gamma(int gamma[]);
static void compute_modified_omega(void);
static void mul_z_poly(int src[]);
static void init_exp_table(void);


static int ErrorLocs[256];
static int NErrors;

static int ErasureLocs[256];
static int NErasures;

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

  pthread_mutex_init(&lock, NULL);

  init_galois_tables();
  calculate_poly(NPAR, genPoly);

  // format the disk
  sb.size = BLOCK_SIZE * BLOCK_NUM;
  sb.nblocks = BLOCK_NUM;
  sb.ninodes = INODE_NUM;

}

void
block_manager::read_block(uint32_t id, char *buf)
{
  if(id >= 2 && id <= (sb.nblocks/BPB + 2)){
      d->read_block(id, buf);
  }
  else{
      //pthread_mutex_lock(&lock);
      int data_length = BLOCK_SIZE/2 - 5;
      int code_length = BLOCK_SIZE/2 - 1;
      char buffer1[BLOCK_SIZE], buffer2[BLOCK_SIZE];
      int erasures[16];
      int nerasures = 0;

      d->read_block(id, buffer1);
      d->read_block(id, buffer2);

      decode_data((unsigned char*)buffer1, code_length);
      if (check_syndrome () != 0) {
        correct_errors ((unsigned char*)buffer1, code_length, 0, NULL);
      }

      decode_data((unsigned char*)(buffer2+code_length), code_length);
      if (check_syndrome () != 0) {
        correct_errors ((unsigned char*)(buffer2+code_length), code_length, 0, NULL);
      }

      memcpy(buf, buffer1, data_length);
      memcpy(buf+data_length, buffer2+code_length, data_length);
      memcpy(buffer1+code_length, buffer2+code_length, code_length);
      d->write_block(id, buffer1);
      //pthread_mutex_unlock(&lock);
  }
  return;
}

void
block_manager::write_block(uint32_t id, const char *buf)
{
  if(id >= 2 && id <= (sb.nblocks/BPB + 2)){
      d->write_block(id, buf);
  }
  else{
      //pthread_mutex_lock(&lock);
      char convert_buf[BLOCK_SIZE - 10];
      memcpy(convert_buf, buf, BLOCK_SIZE - 10);
      int data_length = BLOCK_SIZE/2 - 5;
      int code_length = BLOCK_SIZE/2 - 1;
      char buffer[BLOCK_SIZE];
      encode_data((unsigned char*)convert_buf, data_length, (unsigned char*)buffer);
      encode_data((unsigned char*)(convert_buf+data_length), data_length, (unsigned char*)(buffer+code_length));
      d->write_block(id, buffer);
      //pthread_mutex_unlock(&lock);
  }
}

// inode layer -----------------------------------------

inode_manager::inode_manager()
{
  bm = new block_manager();
  memset(inode_used, 0, INODE_NUM);
  trans_id = 1;
  version = 0;
  position = 0;
  logs.reserve(100);
  pthread_mutex_init(&alclock, 0);
  pthread_mutex_init(&loglock, 0);
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
  
  static uint32_t inum=1;
  pthread_mutex_lock(&alclock);
  while(inode_used[inum] != 0){
      pthread_mutex_unlock(&alclock);
      pthread_mutex_lock(&alclock);
      inum = inum % (INODE_NUM - 1) + 1;
  }
  inode_used[inum] = 1;
  pthread_mutex_unlock(&alclock);

  struct inode *ino;
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
          delete ino;
      }
      else {
          delete ino;
          continue;
      }
  }

  char buf[BLOCK_SIZE];
  struct inode *ino_disk;
  bm->read_block(IBLOCK(inum, BLOCK_NUM), buf);
  ino_disk = (struct inode*)buf + inum%IPB;
  ino_disk->type = type;
  ino_disk->size = 0;
  ino_disk->atime = ino_disk->mtime = ino_disk->ctime = time(NULL);
  bm->write_block(IBLOCK(inum, BLOCK_NUM), buf);
  return inum;
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
   
   ino = get_inode(inum);
   memset(ino, 0, sizeof(ino));
   put_inode(inum, ino);
   delete ino;
   
   buf[pos] = buf[pos] | (0x0 << (7 - bit));
   bm->write_block(block_id, buf);
   inode_used[inum] = 2;
}


/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
struct inode* 
inode_manager::get_inode(uint32_t inum)
{
  /*struct inode *ino, *ino_disk;
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

  return ino;*/
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
    if (inode_used[inum] != 1) {
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
   
   int CUR_BLKSIZE = BLOCK_SIZE - 10;
   int block_num = ((ino->size % CUR_BLKSIZE) == 0) ? ino->size/CUR_BLKSIZE : ino->size/CUR_BLKSIZE + 1;
   *size = ino->size;
   
   *buf_out = new char[*size+1];
   char *pos = *buf_out;
   pos[*size]='\0';
   int asize = *size;
   char block_content[CUR_BLKSIZE];
   
   if(block_num<=NDIRECT){
       for(int i=0; i<block_num; i++){
           if(asize <= CUR_BLKSIZE){
               bm->read_block(ino->blocks[i], block_content);
               memcpy(pos, block_content, asize);
               asize = 0;
           }
           else{
               bm->read_block(ino->blocks[i], pos);
               asize -= CUR_BLKSIZE;
               pos += CUR_BLKSIZE;
           }
       }
   }  
   else{
       int i;
       for(i=0; i<NDIRECT; i++){
           if(asize <= CUR_BLKSIZE){
               bm->read_block(ino->blocks[i], block_content);
               memcpy(pos, block_content, asize);
               asize = 0;
           }
           else{
               bm->read_block(ino->blocks[i], pos);
               asize -= CUR_BLKSIZE;
               pos += CUR_BLKSIZE;
           }
       }
       
       char indir_block[CUR_BLKSIZE];
       bm->read_block(ino->blocks[NDIRECT], indir_block);
       char *ptr = indir_block;       
       
       for(; i<block_num; i++){
           if(asize <= CUR_BLKSIZE){
               bm->read_block(*((int*)ptr), block_content);
               memcpy(pos, block_content, asize);
               asize = 0;
           }
           else{
               bm->read_block(*((int*)ptr), pos);
               asize -= CUR_BLKSIZE;
               pos += CUR_BLKSIZE;
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
    int CUR_BLKSIZE = BLOCK_SIZE - 10;
    inode_t* inode = get_inode(inum);
	bool prev_indir_used = ((inode->size/CUR_BLKSIZE) > NDIRECT);
	uint32_t prev_size = inode->size;
	uint32_t prev_num = prev_size / CUR_BLKSIZE + 1;
	if (prev_size % CUR_BLKSIZE == 0) prev_num -= 1;

	inode->size = MIN(MAXFILE *CUR_BLKSIZE, (unsigned)size);
	
	char indirect_buffer[CUR_BLKSIZE];
	uint32_t* indirect_array;
	uint32_t block_num = inode->size / CUR_BLKSIZE + 1;
	if (inode->size % CUR_BLKSIZE == 0) block_num -= 1;
	bool now_dir_used = (block_num > NDIRECT); 

	if (prev_indir_used) { 
		uint32_t indirect_block = inode->blocks[NDIRECT];
		char indir_content[CUR_BLKSIZE];
		bm->read_block(indirect_block, indir_content);
		uint32_t* old_indirect_array = (uint32_t*)indir_content;
		for (uint32_t i = 0; i < prev_num - NDIRECT; i++)
			bm->free_block(old_indirect_array[i]);	
		bm->free_block(indirect_block);						
	}

	for (uint32_t i = 0; i < prev_num && i < NDIRECT; i++)
		bm->free_block(inode->blocks[i]);
	
	if (now_dir_used) {
		inode->blocks[NDIRECT] = bm->alloc_block();
		bm->read_block(inode->blocks[NDIRECT], indirect_buffer);
		indirect_array = (uint32_t*)indirect_buffer;
	}

	uint32_t remain_size = inode->size;
	uint32_t total = 0;	
	uint32_t size_to_write = 0;	
	uint32_t write_block = 0; 
	uint32_t block_allocated = 0; 
	char write_buffer[CUR_BLKSIZE];

	for (total = 0; write_block < block_num; total += size_to_write, remain_size -= size_to_write) {
		size_to_write = MIN(remain_size, CUR_BLKSIZE);

		if (write_block < NDIRECT) {
			block_allocated = bm->alloc_block();
			inode->blocks[write_block] = block_allocated;
		}

		else {
			block_allocated = bm->alloc_block();
			indirect_array[write_block - NDIRECT] = block_allocated;
			bm->write_block(inode->blocks[NDIRECT], indirect_buffer);	
		}
		memset(write_buffer, 0, CUR_BLKSIZE);
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




/*RS code implemention*/
void
generate_rscode(unsigned char msg[], int nbytes, unsigned char dst[])
{
	int i;

	for (i = 0; i < nbytes; i++) dst[i] = msg[i];

	for (i = 0; i < NPAR; i++) {
		dst[i + nbytes] = pBytes[NPAR - 1 - i];
	}
}

void
decode_data(unsigned char data[], int nbytes)
{
	int i, j, sum;
	for (j = 0; j < NPAR; j++) {
		sum = 0;
		for (i = 0; i < nbytes; i++) {
			sum = data[i] ^ galois_multiply(gexp[j + 1], sum);
		}
		synBytes[j] = sum;
	}
}

int
check_syndrome(void)
{
	int i, nz = 0;
	for (i = 0; i < NPAR; i++) {
		if (synBytes[i] != 0) {
			nz = 1;
			break;
		}
	}
	return nz;
}


void
debug_check_syndrome(void)
{
	int i;

	for (i = 0; i < 3; i++) {
		printf(" inv log S[%d]/S[%d] = %d\n", i, i + 1,
			glog[galois_multiply(synBytes[i], ginv(synBytes[i + 1]))]);
	}
}

static void
calculate_poly(int nbytes, int genpoly[])
{
	int i, tp[256], tp1[256];

	zero_poly(tp1);
	tp1[0] = 1;

	for (i = 1; i <= nbytes; i++) {
		zero_poly(tp);
		tp[0] = gexp[i];		/* set up x+a^n */
		tp[1] = 1;

		mult_polys(genpoly, tp, tp1);
		copy_poly(tp1, genpoly);
	}
}

void
encode_data(unsigned char msg[], int nbytes, unsigned char dst[])
{
	int i, LFSR[NPAR + 1], dbyte, j;

	for (i = 0; i < NPAR + 1; i++) LFSR[i] = 0;

	for (i = 0; i < nbytes; i++) {
		dbyte = msg[i] ^ LFSR[NPAR - 1];
		for (j = NPAR - 1; j > 0; j--) {
			LFSR[j] = LFSR[j - 1] ^ galois_multiply(genPoly[j], dbyte);
		}
		LFSR[0] = galois_multiply(genPoly[0], dbyte);
	}

	for (i = 0; i < NPAR; i++)
		pBytes[i] = LFSR[i];

	generate_rscode(msg, nbytes, dst);
}

void
init_galois_tables(void)
{
	init_exp_table();
}


static void
init_exp_table(void)
{
	int i, z;
	int pinit, p1, p2, p3, p4, p5, p6, p7, p8;

	pinit = p2 = p3 = p4 = p5 = p6 = p7 = p8 = 0;
	p1 = 1;

	gexp[0] = 1;
	gexp[255] = gexp[0];
	glog[0] = 0;			/* shouldn't log[0] be an error? */

	for (i = 1; i < 256; i++) {
		pinit = p8;
		p8 = p7;
		p7 = p6;
		p6 = p5;
		p5 = p4 ^ pinit;
		p4 = p3 ^ pinit;
		p3 = p2 ^ pinit;
		p2 = p1;
		p1 = pinit;
		gexp[i] = p1 + p2 * 2 + p3 * 4 + p4 * 8 + p5 * 16 + p6 * 32 + p7 * 64 + p8 * 128;
		gexp[i + 255] = gexp[i];
	}

	for (i = 1; i < 256; i++) {
		for (z = 0; z < 256; z++) {
			if (gexp[z] == i) {
				glog[i] = z;
				break;
			}
		}
	}
}

int galois_multiply(int a, int b)
{
	int i, j;
	if (a == 0 || b == 0) return (0);
	i = glog[a];
	j = glog[b];
	return (gexp[i + j]);
}


int ginv(int elt)
{
	return (gexp[255 - glog[elt]]);
}


void
Modified_Berlekamp_Massey(void)
{
	int n, L, L2, k, d, i;
	int psi[MAXDEG], psi2[MAXDEG], D[MAXDEG];
	int gamma[MAXDEG];

	init_gamma(gamma);

	copy_poly(D, gamma);
	mul_z_poly(D);

	copy_poly(psi, gamma);
	k = -1; L = NErasures;

	for (n = NErasures; n < NPAR; n++) {

		d = compute_discrepancy(psi, synBytes, L, n);

		if (d != 0) {

			for (i = 0; i < MAXDEG; i++) psi2[i] = psi[i] ^ galois_multiply(d, D[i]);
			if (L < (n - k)) {
				L2 = n - k;
				k = n - L;
				for (i = 0; i < MAXDEG; i++) D[i] = galois_multiply(psi[i], ginv(d));
				L = L2;
			}

			for (i = 0; i < MAXDEG; i++) psi[i] = psi2[i];
		}

		mul_z_poly(D);
	}

	for (i = 0; i < MAXDEG; i++) Lambda[i] = psi[i];
	compute_modified_omega();


}

void
compute_modified_omega()
{
	int i;
	int product[MAXDEG * 2];

	mult_polys(product, Lambda, synBytes);
	zero_poly(Omega);
	for (i = 0; i < NPAR; i++) Omega[i] = product[i];

}

void
mult_polys(int dst[], int p1[], int p2[])
{
	int i, j;
	int tmp1[MAXDEG * 2];

	for (i = 0; i < (MAXDEG * 2); i++) dst[i] = 0;

	for (i = 0; i < MAXDEG; i++) {
		for (j = MAXDEG; j<(MAXDEG * 2); j++) tmp1[j] = 0;

		for (j = 0; j<MAXDEG; j++) tmp1[j] = galois_multiply(p2[j], p1[i]);
		for (j = (MAXDEG * 2) - 1; j >= i; j--) tmp1[j] = tmp1[j - i];
		for (j = 0; j < i; j++) tmp1[j] = 0;

		for (j = 0; j < (MAXDEG * 2); j++) dst[j] ^= tmp1[j];
	}
}

void
init_gamma(int gamma[])
{
	int e, tmp[MAXDEG];

	zero_poly(gamma);
	zero_poly(tmp);
	gamma[0] = 1;

	for (e = 0; e < NErasures; e++) {
		copy_poly(tmp, gamma);
		scale_poly(gexp[ErasureLocs[e]], tmp);
		mul_z_poly(tmp);
		add_polys(gamma, tmp);
	}
}

void
compute_next_omega(int d, int A[], int dst[], int src[])
{
	int i;
	for (i = 0; i < MAXDEG; i++) {
		dst[i] = src[i] ^ galois_multiply(d, A[i]);
	}
}

int
compute_discrepancy(int lambda[], int S[], int L, int n)
{
	int i, sum = 0;

	for (i = 0; i <= L; i++)
		sum ^= galois_multiply(lambda[i], S[n - i]);
	return (sum);
}

void add_polys(int dst[], int src[])
{
	int i;
	for (i = 0; i < MAXDEG; i++) dst[i] ^= src[i];
}

void copy_poly(int dst[], int src[])
{
	int i;
	for (i = 0; i < MAXDEG; i++) dst[i] = src[i];
}

void scale_poly(int k, int poly[])
{
	int i;
	for (i = 0; i < MAXDEG; i++) poly[i] = galois_multiply(k, poly[i]);
}


void zero_poly(int poly[])
{
	int i;
	for (i = 0; i < MAXDEG; i++) poly[i] = 0;
}

static void mul_z_poly(int src[])
{
	int i;
	for (i = MAXDEG - 1; i > 0; i--) src[i] = src[i - 1];
	src[0] = 0;
}

void
Find_Roots(void)
{
	int sum, r, k;
	NErrors = 0;

	for (r = 1; r < 256; r++) {
		sum = 0;
		for (k = 0; k < NPAR + 1; k++) {
			sum ^= galois_multiply(gexp[(k*r) % 255], Lambda[k]);
		}
		if (sum == 0)
		{
			ErrorLocs[NErrors] = (255 - r); NErrors++;
			if (DEBUG) fprintf(stderr, "Root found at r = %d, (255-r) = %d\n", r, (255 - r));
		}
	}
}

int
correct_errors(unsigned char codeword[],
int csize,
int nerasures,
int erasures[])
{
	int r, i, j, err;

	NErasures = nerasures;
	for (i = 0; i < NErasures; i++) ErasureLocs[i] = erasures[i];

	Modified_Berlekamp_Massey();
	Find_Roots();


	if ((NErrors <= NPAR) && NErrors > 0) {

		for (r = 0; r < NErrors; r++) {
			if (ErrorLocs[r] >= csize) {
				if (DEBUG) fprintf(stderr, "Error loc i=%d outside of codeword length %d\n", i, csize);
				return(0);
			}
		}

		for (r = 0; r < NErrors; r++) {
			int num, denom;
			i = ErrorLocs[r];

			num = 0;
			for (j = 0; j < MAXDEG; j++)
				num ^= galois_multiply(Omega[j], gexp[((255 - i)*j) % 255]);

			denom = 0;
			for (j = 1; j < MAXDEG; j += 2) {
				denom ^= galois_multiply(Lambda[j], gexp[((255 - i)*(j - 1)) % 255]);
			}

			err = galois_multiply(num, ginv(denom));
			if (DEBUG) fprintf(stderr, "Error magnitude %#x at loc %d\n", err, csize - i);

			codeword[csize - i - 1] ^= err;
		}
		return(1);
	}
	else {
		if (DEBUG && NErrors) fprintf(stderr, "Uncorrectable codeword\n");
		return(0);
	}
}

/* 
*lab6 
*
*/

void inode_manager::log(std::string nlog, uint32_t& num){
    pthread_mutex_lock(&loglock);
    std::stringstream ss;
    ss << nlog;
    uint32_t cmt_num;
    std::string action;
    ss >> cmt_num >> action;
    if(action == "put" || action == "remove"){
        extent_protocol::extentid_t id;
        ss >> id;
        char *buf_out = NULL;
        int size;
        read_file(id, &buf_out, &size);
        std::string buf;
        buf.assign(buf_out, size);
        std::stringstream ts;
        ts << nlog << ' ' << size << ' ' << buf;
        nlog = ts.str();
        logs.push_back(nlog);
    }
    pthread_mutex_unlock(&loglock);
}

void inode_manager::commit(){
    std::stringstream ss;
    ss << trans_id << " commit " << version++;
    logs.push_back(ss.str());
}

void inode_manager::undo(){
    pthread_mutex_lock(&loglock);
    uint32_t n=logs.size();
    if(position == 0){
        position = n-1;
    }
    while(position >= 0){
        std::stringstream ss;
        uint32_t transaction_id, version_id;
        std::string action;
        ss << logs[position];
        ss >> transaction_id >> action;
        if(action == "commit"){
            ss >> version_id;
            if(version_id == version-1){
                version--;
                break;
            }
        }
        if(action == "create"){
            extent_protocol::extentid_t id;
            ss >> id;
            remove_file(id);
        }
        if(action == "put"){
            extent_protocol::extentid_t id;
            uint32_t size;
            ss >> id >> size;
            for(uint32_t j=0; j<size+2; j++)
                ss.get();
            ss >> size;
            ss.get();
            char cbuf[size];
            for(uint32_t j=0; j<size; j++)
                ss.get(cbuf[j]);
            write_file(id, cbuf, size);
        }
        if(action == "remove"){
            extent_protocol::extentid_t id;
            uint32_t size;
            ss >> id >> size;
            ss.get();
            char cbuf[size];
            for(uint32_t j=0; j<size; j++)
                ss.get(cbuf[j]);
            inode_used[id] = 1;
            write_file(id, cbuf, size);
        }
        position--;
    }
    pthread_mutex_unlock(&loglock);
}

void inode_manager::redo(){
    pthread_mutex_lock(&loglock);
    uint32_t n=logs.size();
    extent_protocol::extentid_t id;
    while(position < n){
        std::stringstream ss;
        uint32_t transaction_id, version_id;
        std::string action;
        ss << logs[position];
        ss >> transaction_id >> action;
        if(action == "commit"){
            ss >> version_id;
            if(version_id == version+1){
                version++;
                break;
            }
        }
        ss >> id;
        if(action == "create"){
            inode_used[id] = 1;
            write_file(id, "", 0);
        }
        if(action == "put"){
            uint32_t size;
            std::string buf;
            ss >> size;
            ss.get();
            char cbuf[size];
            for(uint32_t j=0; j<size; j++)
                ss.get(cbuf[j]);
            write_file(id, cbuf, size);
        }
        if(action == "remove"){
            remove_file(id);
        }
        position++;
    }
    pthread_mutex_unlock(&loglock);
}