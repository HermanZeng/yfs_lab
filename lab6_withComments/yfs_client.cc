// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

std::string inum2s(yfs_client::inum in)
{
	char* ptr = new char[sizeof(yfs_client::inum)];
	memcpy(ptr, &in, sizeof(yfs_client::inum));
	std::string temp;
	temp.assign(ptr, sizeof(yfs_client::inum));
	delete[]ptr;
	return temp;
}

yfs_client::inum s2inum(std::string str)
{
	char* ptr = new char[sizeof(yfs_client::inum)];
	memcpy(ptr, str.c_str(), sizeof(yfs_client::inum));
	yfs_client::inum res = *(yfs_client::inum*)ptr;
	delete[]ptr;
	return res;
}
std::string itos(size_t size)
{
	char* ptr = new char[sizeof(size_t)];
	memcpy(ptr, &size, sizeof(size_t));
	std::string temp;
	temp.assign(ptr, sizeof(size_t));
	delete[]ptr;
	return temp;
}

size_t stoi(std::string str)
{
	char* ptr = new char[sizeof(size_t)];
	memcpy(ptr, str.c_str(), sizeof(size_t));
	size_t res = *(size_t*)ptr;
	delete[]ptr;
	return res;
}


yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  lc = new lock_client(lock_dst);
  if (ec->put(1, "") != extent_protocol::OK)
      printf("error init root dir\n"); // XYB: init root dir
}


yfs_client::inum
yfs_client::n2i(std::string n)
{
    std::istringstream ist(n);
    unsigned long long finum;
    ist >> finum;
    return finum;
}

std::string
yfs_client::filename(inum inum)
{
    std::ostringstream ost;
    ost << inum;
    return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
    extent_protocol::attr a;

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    if (a.type == extent_protocol::T_FILE) {
        printf("isfile: %lld is a file\n", inum);
        return true;
    } 

    printf("isfile: %lld is not a file\n", inum);
    return false;
}
/** Your code here for Lab...
 * You may need to add routines such as
 * readlink, issymlink here to implement symbolic link.
 * 
 * */

bool
yfs_client::issymlink(inum inum)
{
    extent_protocol::attr a;

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    if (a.type == extent_protocol::T_SYM) {
        printf("issym: %lld is a sym\n", inum);
        return true;
    } 
    printf("issym: %lld is not a sym\n", inum);
}

bool
yfs_client::isdir(inum inum)
{

    extent_protocol::attr a;

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    if (a.type == extent_protocol::T_DIR) {
        printf("isdir: %lld is a dir\n", inum);
        return true;
    } 
    printf("isdir: %lld is not a dir\n", inum);
    return false;
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
    int r = OK;

    printf("getfile %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    fin.atime = a.atime;
    fin.mtime = a.mtime;
    fin.ctime = a.ctime;
    fin.size = a.size;
    printf("getfile %016llx -> sz %llu\n", inum, fin.size);

release:
    return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
    int r = OK;

    printf("getdir %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }
    din.atime = a.atime;
    din.mtime = a.mtime;
    din.ctime = a.ctime;

release:
    return r;
}

int
yfs_client::getsym(inum inum, syminfo &sin)
{
    int r = OK;

    printf("getsym %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }
    sin.atime = a.atime;
    sin.mtime = a.mtime;
    sin.ctime = a.ctime;
    sin.size = a.size;

release:
    return r;
}


#define EXT_RPC(xx) do { \
    if ((xx) != extent_protocol::OK) { \
        printf("EXT_RPC Error: %s:%d \n", __FILE__, __LINE__); \
        r = IOERR; \
        goto release; \
    } \
} while (0)

// Only support set size of attr
int
yfs_client::setattr(inum ino, size_t size)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: get the content of inode ino, and modify its content
     * according to the size (<, =, or >) content length.
     */

    extent_protocol::attr att;
    if (ec->getattr(ino, att) != extent_protocol::OK) 
        return IOERR;
    if (size == att.size)
        return OK;

    std::string buf;
    if (ec->get(ino, buf) != extent_protocol::OK) 
        return IOERR;

    if(size > att.size)
        buf.resize(size, '\0');
    else
        buf.resize(size);

    //ec->log("begin");
    if (ec->put(ino, buf) != extent_protocol::OK) 
        return IOERR;
    //ec->log("end");
    return OK;
}

int
yfs_client::create(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: lookup is what you need to check if file exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */

    lc->acquire(parent);
    if (!isdir(parent)) {
        lc->release(parent);
        return IOERR;
    }
    bool found;
    lookup(parent, name, found, ino_out);
    if (found) {
        lc->release(parent);
        return EXIST;
    }
    //ec->log("begin");
    if (ec->create(extent_protocol::T_FILE, ino_out) != extent_protocol::OK) {
        printf("error creating file\n");
        lc->release(parent);
        return IOERR;
    }
    std::string buf;
    if (ec->get(parent, buf) != extent_protocol::OK) {
        printf("error with get\n");
        lc->release(parent);
        return IOERR;
    }
    size_t n = strlen(name);
    buf = buf + itos(n) + std::string(name) + inum2s(ino_out);
    if (ec->put(parent, buf) != extent_protocol::OK) {
        printf("error with put\n");
        lc->release(parent);
        return IOERR;
    }
    //ec->log("end");
    lc->release(parent);
    return OK;
}

int
yfs_client::mkdir(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: lookup is what you need to check if directory exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */

    lc->acquire(parent);
    if (!isdir(parent)) {
        lc->release(parent);
        return IOERR;
    }
    bool found;
    lookup(parent, name, found, ino_out);
    if (found) {
        lc->release(parent);
        return EXIST;
    }
    //ec->log("begin");
    if (ec->create(extent_protocol::T_DIR, ino_out) != extent_protocol::OK) {
        printf("error creating file\n");
        lc->release(parent);
        return IOERR;
    }
    std::string buf;
    if (ec->get(parent, buf) != extent_protocol::OK) {
        printf("error with get\n");
        lc->release(parent);
        return IOERR;
    }
    size_t n = strlen(name);
    buf = buf + itos(n) + std::string(name) + inum2s(ino_out);
    if (ec->put(parent, buf) != extent_protocol::OK) {
        printf("error with put\n");
        lc->release(parent);
        return IOERR;
    }
    //ec->log("end");
    lc->release(parent);
    return OK;
}

int
yfs_client::lookup(inum parent, const char *name, bool &found, inum &ino_out)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: lookup file from parent dir according to name;
     * you should design the format of directory content.
     */
    
    if(!isdir(parent))
        return IOERR;
    
    std::list<dirent> list;
    if(readdir(parent, list) != OK)
        return IOERR;
        
    found = 0;
    std::list<dirent>::iterator iter = list.begin();
    std::string file_name;
    file_name.assign(name);
    for(; iter != list.end(); iter++){
        if(iter->name.compare(file_name) == 0){
            found = 1;
            ino_out = iter->inum;
            return OK;
        }
    }
    
    return r;
}

int
yfs_client::readdir(inum dir, std::list<dirent> &list)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: you should parse the dirctory content using your defined format,
     * and push the dirents to the list.
     */

    if(!isdir(dir)) 
        return IOERR;
    
    std::string file_content;
    if(ec->get(dir, file_content) != extent_protocol::OK) {
        printf("readdir error: get file error in lab1");
        return IOERR;
    }
    
    int max_length = file_content.size();
    int temp = 0;
    while(temp < max_length){
        dirent entry;
        int name_length = stoi(file_content.substr(temp, sizeof(size_t)));
        temp += sizeof(size_t);
        entry.name = file_content.substr(temp, name_length);
        temp += name_length;
        entry.inum = s2inum(file_content.substr(temp, sizeof(inum)));
        temp += sizeof(inum);
        list.push_back(entry);
    }
    
    return r;
}

int
yfs_client::read(inum ino, size_t size, off_t off, std::string &data)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: read using ec->get().
     */

    lc->acquire(ino);
    std::string buf;
    if (ec->get(ino, buf) != extent_protocol::OK){
        return IOERR;
        lc->release(ino);
    } 
        
    int file_size = buf.size();
    if (off >= file_size) {
        data = "";
        lc->release(ino);
        return OK;
    }
    else if((off+size)<=file_size){
        data = buf.substr(off, size);
        lc->release(ino);
        return OK;
    }
    else if((off+size)>file_size){
        data = buf.substr(off, file_size-off);
        lc->release(ino);
        return OK;
    }

    lc->release(ino);
}

int
yfs_client::write(inum ino, size_t size, off_t off, const char *data,
        size_t &bytes_written)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: write using ec->put().
     * when off > length of original file, fill the holes with '\0'.
     */

    lc->acquire(ino);
    std::string buf;
    if (ec->get(ino, buf) != extent_protocol::OK){
        lc->release(ino);
        return IOERR;
    } 

    int file_size = buf.size();
    if ((off+size) >= file_size) {
        buf.resize(off+size, '\0');
    }

    buf.replace(off, size, data, size);
    //ec->log("begin");
    if (ec->put(ino, buf) != extent_protocol::OK){
        lc->release(ino);
        return IOERR;
    } 
    //ec->log("end");    
    lc->release(ino);
    return OK;
}

int yfs_client::unlink(inum parent,const char *name)
{
    
    lc->acquire(parent);
    int r = OK;

    bool found;
    inum ino_out;
    if(lookup(parent, name, found, ino_out) != OK)
        return IOERR;

    if (found) {
        std::string buf;
        if(ec->get(parent, buf) != extent_protocol::OK){
            lc->release(parent);
            return IOERR;
        }
            
        ec->remove(ino_out);
        int pos = buf.find(name);

        int length = stoi(buf.substr(pos-sizeof(size_t), sizeof(size_t)));

        buf.erase(pos-sizeof(size_t), sizeof(size_t)+length+sizeof(inum));
        //ec->log("begin");
        if (ec->put(parent, buf) != extent_protocol::OK){
            lc->release(parent);
            return IOERR;
        }
        //ec->log("end");
        lc->release(parent);    
        return OK;
    }
    else{
        lc->release(parent);
        return NOENT;
    }
        
}

int yfs_client::symlink(const char *link, inum parent, const char *name, inum &ino_out)
{
    lc->acquire(parent);
    if (!isdir(parent)){
        lc->release(parent);
        return IOERR;
    } 
        
    bool found;
    if(lookup(parent, name, found, ino_out) != OK){
        lc->release(parent);
        return IOERR;
    } 
    if (found){
        lc->release(parent);
        return EXIST;
    }
    //ec->log("begin"); 
    if (ec->create(extent_protocol::T_SYM, ino_out) != extent_protocol::OK){
        lc->release(parent);
        return IOERR;
    } 
    if (ec->put(ino_out, std::string(link)) != extent_protocol::OK){
        lc->release(parent);
        return IOERR;
    } 

    std::string buf;
    if (ec->get(parent, buf) != extent_protocol::OK){
        lc->release(parent);
        return IOERR;
    } 
    int n = strlen(name);
    buf = buf + itos(n) + std::string(name) + inum2s(ino_out);
    if (ec->put(parent, buf) != extent_protocol::OK){
        lc->release(parent);
        return IOERR;
    } 
    //ec->log("end");
    lc->release(parent);
    return OK;
}

int yfs_client::readlink(inum ino, std::string &link)
{
    lc->acquire(ino);
    if (!issymlink(ino)){
        lc->release(ino);
        return IOERR;
    } 
    if (ec->get(ino, link) != extent_protocol::OK){
        lc->release(ino);
        return IOERR;
    } 
    lc->release(ino);
    return OK;
}

int yfs_client::commit(){
    ec->commit();
    return 0;
}

int yfs_client::undo(){
    ec->undo();
    return 0;
}

int yfs_client::redo(){
    ec->redo();
    return 0;
}