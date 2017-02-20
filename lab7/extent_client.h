// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "extent_server.h"

class extent_client {
 private:
  rpcc *cl;
  uint32_t transnum;

 public:
  extent_client(std::string dst);

  extent_protocol::status create(uint32_t type, unsigned int mode, unsigned short user_id, extent_protocol::extentid_t &eid);
  extent_protocol::status get(extent_protocol::extentid_t eid, 
			                        std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid, 
				                          extent_protocol::attr &a);
  extent_protocol::status setattr(extent_protocol::extentid_t eid, 
				                          extent_protocol::attr a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);
  extent_protocol::status log(std::string log);
  extent_protocol::status commit();
  extent_protocol::status undo();
  extent_protocol::status redo();
};

#endif 

