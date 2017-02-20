// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

extent_client::extent_client(std::string dst)
{
  sockaddr_in dstsock;
  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    printf("extent_client: bind failed\n");
  }
}

// a demo to show how to use RPC
extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
  extent_protocol::status ret = extent_protocol::OK;
  ret = cl->call(extent_protocol::getattr, eid, attr);
  printf("extent_client_get %d %o %d %d", eid, attr.mode, attr.uid, attr.gid);
  return ret;
}

extent_protocol::status
extent_client::setattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr attr)
{
  int i;
  extent_protocol::status ret = extent_protocol::OK;
  ret = cl->call(extent_protocol::setattr, eid, attr, i);
  return ret;
}

extent_protocol::status
extent_client::create(uint32_t type, unsigned int mode, unsigned short user_id, extent_protocol::extentid_t &id)
{
  extent_protocol::status ret = extent_protocol::OK;
  // Your lab3 code goes here
  ret = cl->call(extent_protocol::create, type, mode, user_id, id);
  std::stringstream ss;
  ss << transnum << " create " << id;
  std::string nlog;
  ss >> nlog;
  nlog = ss.str();
  log(nlog);
  return ret;
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  // Your lab3 code goes here
  ret = cl->call(extent_protocol::get, eid, buf);
  return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
  extent_protocol::status ret = extent_protocol::OK;
  // Your lab3 code goes here
  int i;
  std::stringstream ss;
  ss << transnum << " put " << eid << " " << buf.size() << ' ' << buf;
  std::string nlog;
  nlog = ss.str();
  ret = log(nlog);
  ret = cl->call(extent_protocol::put, eid, buf, i);  
  return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  extent_protocol::status ret = extent_protocol::OK;
  // Your lab3 code goes here
  int i;
  std::stringstream ss;
  ss << transnum << " remove " << eid;
  std::string nlog;
  nlog = ss.str();
  ret = log(nlog);
  ret = cl->call(extent_protocol::remove, eid, i);  
  return ret;
}

extent_protocol::status
extent_client::log(std::string log)
{
    extent_protocol::status ret = extent_protocol::OK;
    int n;
    ret = cl->call(extent_protocol::log, log, n);
    return ret;
}

extent_protocol::status extent_client::commit(){
    int i, j;
    cl->call(extent_protocol::commit, i, j);
    return extent_protocol::OK;
}
extent_protocol::status extent_client::undo(){
    int i, j;
    cl->call(extent_protocol::undo, i, j);
    return extent_protocol::OK;
}
extent_protocol::status extent_client::redo(){
    int i, j;
    cl->call(extent_protocol::redo, i, j);
    return extent_protocol::OK;
}
