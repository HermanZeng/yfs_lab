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
#include <fuse/fuse_lowlevel.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/x509.h>  
#include <openssl/x509v3.h>  



yfs_client::yfs_client()
{
  ec = NULL;
  lc = NULL;
}

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


yfs_client::yfs_client(std::string extent_dst, std::string lock_dst, const char* cert_file)
{
  ec = new extent_client(extent_dst);
  lc = new lock_client(lock_dst);
  if (ec->put(1, "") != extent_protocol::OK)
      printf("error init root dir\n"); // XYB: init root dir
  int ret = verify(cert_file, &user_id);
  if(ret != OK) return;
  //fprintf(stderr, "%d\n", user_id);
}

int
yfs_client::verify(const char* name, unsigned short *uid)
{
  	int ret = OK;
    OpenSSL_add_all_algorithms();
    BIO *ca_bio;
    BIO *user_bio;
    X509 *ca_x;
    X509 *user_x;
    ca_bio = BIO_new_file("./cert/ca.pem", "r");
    user_bio =  BIO_new_file(name, "r");
    ca_x = PEM_read_bio_X509(ca_bio, NULL, 0, NULL);
    user_x = PEM_read_bio_X509(user_bio, NULL, 0, NULL);

    if(user_x == NULL)
        return ERRPEM;
    
    STACK_OF(X509) *ca_stack = NULL;
    X509_STORE_CTX *ctx; 
    X509_STORE *ca_store; 
    ctx = X509_STORE_CTX_new();  
    ca_store = X509_STORE_new();
    X509_STORE_add_cert(ca_store, ca_x);
    X509_STORE_CTX_init(ctx, ca_store, user_x, ca_stack);
    int res = X509_verify_cert(ctx);
    /*if ( res != 1 )
    {
        fprintf(stderr, "X509_verify_cert fail, res = %d, error id = %d, %s\n",
                res, ctx->error, X509_verify_cert_error_string(ctx->error));
    }*/
    int errcode = X509_STORE_CTX_get_error(ctx);
    if(errcode == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY) return EINVA;
    if(errcode == X509_V_ERR_CERT_HAS_EXPIRED) return ECTIM;
    if(errcode == X509_V_OK) {
        X509_NAME *pSubName = NULL;
        int iLen = 0;
        char buf[256] = {0};
        pSubName = X509_get_subject_name(user_x);
        //ZeroMemory(buf, 256);  
        iLen = X509_NAME_get_text_by_NID(pSubName, NID_commonName, buf, 256);
        //fprintf(stderr, "%s\n", buf);
        //*uid = 0;

        FILE *pFile=fopen("./etc/passwd","r");
        char *pBuf;
        fseek(pFile,0,SEEK_END);
        int len=ftell(pFile);
        pBuf=new char[len+1];
        rewind(pFile);
        fread(pBuf,1,len,pFile);
        pBuf[len]='\0';
        //MessageBox(pBuf);
        fclose(pFile);
        //fprintf(stderr, "%s\n", pBuf);

        char *ptr = strstr(pBuf, buf);
        if(ptr == NULL) return ENUSE;
        ptr += strlen(buf) + 3;
        //fprintf(stderr, "%d\n", strlen(buf));
        //fprintf(stderr, "%c\n", *ptr);
        char *end = ptr;
        int id_len = 0;
        while(*end != ':'){
            end++;
            id_len++;
        }
        //fprintf(stderr, "%d\n", id_len);
        char user_id[6];
        strncpy(user_id, ptr, id_len);
        user_id[id_len] = '\0';
        *uid = (unsigned short)atoi(user_id);
        //fprintf(stderr, "%d\n", *uid);
    }
	return ret;
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
yfs_client::setattr(inum ino, filestat st, unsigned long toset)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: get the content of inode ino, and modify its content
     * according to the size (<, =, or >) content length.
     */
    int user_type = check_usertype(ino, user_id);
    if(user_type != ROOT && user_type != OWNER)
        return NOPEM;

    

    extent_protocol::attr att;
    if (ec->getattr(ino, att) != extent_protocol::OK) 
        return IOERR;
    
    if((toset & FUSE_SET_ATTR_SIZE)){
        if (st.size == att.size)
        return OK;

        lc->acquire(ino);

        std::string buf;
        if (ec->get(ino, buf) != extent_protocol::OK) {
            lc->release(ino);
            return IOERR;
        }

        if(st.size > att.size)
            buf.resize(st.size, '\0');
        else
            buf.resize(st.size);

        if (ec->put(ino, buf) != extent_protocol::OK) {
            lc->release(ino);
            return IOERR;
        }
        lc->release(ino);
    }

    if(toset & FUSE_SET_ATTR_MODE){
        att.mode = st.mode & 0777;
    }

    if(toset & FUSE_SET_ATTR_UID){
        if(user_type != ROOT)
            return NOPEM;
        else
        {
            att.uid = st.uid;
        }
    }

    if(toset & FUSE_SET_ATTR_GID){
        if(user_type != ROOT)
            return NOPEM;
        else
        {
            att.gid = st.gid;
        }
    }

    printf("\n\nsetattr bug%o %d %d\n\n", att.mode, att.uid, att.gid);
    if (ec->setattr(ino, att) != extent_protocol::OK) 
        return IOERR;

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

    extent_protocol::attr a;
    printf("\nin_create");
    if (ec->getattr(parent, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        lc->release(parent);
        return IOERR;
    }
    printf("\n\n\ndirectory %d %o %d %d\n\n\n", parent, a.mode, a.uid, a.gid);

    bool read_pem = 0, write_pem = 0, exe_pem = 0;
    //check_usertype(ino, user_id);
    check_pem(parent, user_id, read_pem, write_pem, exe_pem);
    printf("\nin_create %o %d %d %d end\n", a.mode, read_pem, write_pem, exe_pem);
    if(!write_pem) {
        lc->release(parent);
        return NOPEM;
    }

    if (!isdir(parent)) {
        lc->release(parent);
        return IOERR;
    }
    bool found;
    lookup(parent, name, found, ino_out);
    /*if (found) {
        lc->release(parent);
        return EXIST;
    }*/
    if (ec->create(extent_protocol::T_FILE, mode, user_id, ino_out) != extent_protocol::OK) {
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
    bool read_pem = 0, write_pem = 0, exe_pem = 0;
    //check_usertype(ino, user_id);
    check_pem(parent, user_id, read_pem, write_pem, exe_pem);
    printf("\nin_mkdir %d %d %d %d", mode, read_pem, write_pem, exe_pem);    
    if(!write_pem) {
        lc->release(parent);
        return NOPEM;
    }

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
    if (ec->create(extent_protocol::T_DIR, mode, user_id, ino_out) != extent_protocol::OK) {
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
    
    bool read_pem = 0, write_pem = 0, exe_pem = 0;
    //check_usertype(ino, user_id);
    check_pem(dir, user_id, read_pem, write_pem, exe_pem);
    if(!read_pem) {
        //lc->release(dir);
        return NOPEM;
    }

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
    bool read_pem = 0, write_pem = 0, exe_pem = 0;
    //check_usertype(ino, user_id);
    check_pem(ino, user_id, read_pem, write_pem, exe_pem);
    if(!read_pem) {
        lc->release(ino);
        return NOPEM;
    }
    if (ec->get(ino, buf) != extent_protocol::OK){
        lc->release(ino);
        return IOERR;        
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

    bool read_pem = 0, write_pem = 0, exe_pem = 0;
    //check_usertype(ino, user_id);
    check_pem(ino, user_id, read_pem, write_pem, exe_pem);
    if(!write_pem) {
        lc->release(ino);
        return NOPEM;
    }

    if (ec->get(ino, buf) != extent_protocol::OK){
        lc->release(ino);
        return IOERR;
    } 

    int file_size = buf.size();
    if ((off+size) >= file_size) {
        buf.resize(off+size, '\0');
    }

    buf.replace(off, size, data, size);
    if (ec->put(ino, buf) != extent_protocol::OK){
        lc->release(ino);
        return IOERR;
    } 
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
        if (ec->put(parent, buf) != extent_protocol::OK){
            lc->release(parent);
            return IOERR;
        }
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
    unsigned long mode = 0777;
    if (ec->create(extent_protocol::T_SYM, mode, user_id, ino_out) != extent_protocol::OK){
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

void yfs_client::check_pem(inum ino, unsigned short userid, bool& read_pem, bool& write_pem, bool& exe_pem){
    int user_type = check_usertype(ino, user_id);
    //return;
    if(user_type == ROOT){
        read_pem = 1;
        write_pem = 1;
        exe_pem = 1;
    }

    extent_protocol::attr a;

    if (ec->getattr(ino, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return;
    }
    unsigned int mode = a.mode & 0777;
    if(user_type == OWNER){
        if(mode & 0400) read_pem = 1;
        if(mode & 0200) write_pem = 1;
        if(mode & 0100) exe_pem = 1;
    }
    if(user_type == GROUP){
        if(mode & 0040) read_pem = 1;
        if(mode & 0020) write_pem = 1;
        if(mode & 0010) exe_pem = 1;
    }
    if(user_type == OTHER){
        if(mode & 0004) read_pem = 1;
        if(mode & 0002) write_pem = 1;
        if(mode & 0001) exe_pem = 1;
    }
}

int yfs_client::check_usertype(inum ino, unsigned short userid){
    if(userid == 0) return ROOT;
    extent_protocol::attr a;

    if (ec->getattr(ino, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return -1;
    }
    if(userid == a.uid) return OWNER;
    if(userid == a.gid) return GROUP;

    char gid_str[8];
    snprintf(gid_str, sizeof(gid_str), "%d", a.uid);

    FILE *pFile=fopen("./etc/group","r");
    char *pBuf;
    fseek(pFile,0,SEEK_END);
    int len=ftell(pFile);
    pBuf=new char[len+1];
    rewind(pFile);
    fread(pBuf,1,len,pFile);
    pBuf[len]='\0';
    //MessageBox(pBuf);
    fclose(pFile);
    //fprintf(stderr, "%s\n", pBuf);

    char *ptr = strstr(pBuf, gid_str);
    if(ptr == NULL) printf("not find %s\n", gid_str);
    ptr += strlen(gid_str) + 1;
    //fprintf(stderr, "%d\n", strlen(buf));
    //fprintf(stderr, "%c\n", *ptr);
    char *end = ptr;
    int id_len = 0;
    //printf("\n\n\nfucking bug");
    while(*end != '\n' && *end != '\0'){
        end++;
        id_len++;
        //printf("%c ", *end);
    }
    //printf("\n\n\n");
    if(id_len == 0) return OTHER;
    char uid_str[8];
    strncpy(uid_str, ptr, id_len);
    uid_str[id_len] = '\0';
    unsigned short uid = get_userid(uid_str);
    //printf("\ncheckfunc %s %s %d\n", gid_str, uid_str, uid);
    if(userid == uid) return GROUP;
    return OTHER;
}

unsigned short yfs_client::get_userid(char *buf){
    unsigned short uid;
    FILE *pFile=fopen("./etc/passwd","r");
    char *pBuf;
    fseek(pFile,0,SEEK_END);
    int len=ftell(pFile);
    pBuf=new char[len+1];
    rewind(pFile);
    fread(pBuf,1,len,pFile);
    pBuf[len]='\0';
    //MessageBox(pBuf);
    fclose(pFile);
    //fprintf(stderr, "%s\n", pBuf);

    char *ptr = strstr(pBuf, buf);
    if(ptr == NULL) printf("not find %s\n", buf);
    ptr += strlen(buf) + 3;
    //fprintf(stderr, "%d\n", strlen(buf));
    //fprintf(stderr, "%c\n", *ptr);
    char *end = ptr;
    int id_len = 0;
    while(*end != ':'){
        end++;
        id_len++;
    }
    //fprintf(stderr, "%d\n", id_len);
    char userid[6];
    strncpy(userid, ptr, id_len);
    userid[id_len] = '\0';
    uid = (unsigned short)atoi(userid);
}