#ifndef _COMMON_H
#define _COMMON_H

#define MAX_DATA_SIZE 1466

#define SUCCESS 0
#define FAILURE -1
#define END_READ -2

#define PKT_FINISH 0xffffffff

typedef struct{
    // if ID = 0, then the packet is not loaded from disk
    unsigned long int ID;
    unsigned short int datasize;
}data_header_t;

typedef struct{
    unsigned long int ID;
    unsigned short int datasize;
    char data[MAX_DATA_SIZE];
}data_pkt_t;

typedef struct{
    unsigned long int ID;
}ack_pkt_t;


#endif
