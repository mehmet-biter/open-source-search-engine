#ifndef GB_MSGTYPE_H
#define GB_MSGTYPE_H

enum msg_type_t {
	msg_type_0 = 0x00,	//getListFromRdb
	msg_type_1 = 0x01,
	msg_type_4 = 0x04,	//data replication
	msg_type_7 = 0x07,	//inject web page
	msg_type_c = 0x0c,	//get IP
	msg_type_11 = 0x11,	//pinginfo
	msg_type_13 = 0x13,	//download a url
	msg_type_1f = 0x1f,	//Get remote log
	msg_type_20 = 0x20,	//summary+inlinks
	msg_type_22 = 0x22,	//get titlerec
	msg_type_25 = 0x25,	//get linkinfo
	msg_type_39 = 0x39,	//query/docids
	msg_type_3e = 0x3e,	//sync parameters
	msg_type_3f = 0x3f,	//update parameters
	msg_type_54 = 0x54,	//do msg13 via a proxy
	msg_type_56 = 0x56,	//watchdog
	msg_type_c1 = 0xc1,	//crawlinfo
	msg_type_fd = 0xfd,
	msg_type_dns
};


#endif // GB_MSGTYPE_H
