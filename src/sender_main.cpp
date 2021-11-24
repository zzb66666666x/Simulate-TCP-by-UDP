/* 
 * File:   sender_main.c
 * Author: 
 *
 * Created on 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>
#include <math.h>
#include <iterator>
#include <list>
#include <iostream>

#include "common.h"

using namespace std;

// MACROS
// #define DEBUG_ENV
#define COPY_METHOD 2

#define DEFAULT_SST 64
#define MINIMUM_SST 2
#define STARTER_CW 4
#define MINIMUM_CW 4.0
#define FILLER_WORD 0

#define EMPTY_PACKET Packet()

// #define TIME_ALPHA 0.8
// #define TIME_DELTA_FACTOR 4.0

// file scope variables
struct sockaddr_in si_other;
int s, slen;
// struct sockaddr_storage their_addr;
socklen_t addr_len;
timeval TIMEOUT_CONST = {0,80000};
timeval TIMEVAL_INIT = {0,0};
// timeval TIME_RTT = {0,30000};
// timeval TIME_DELTA = {0,0};

// enum
typedef enum{
    SLOW_START, CONJEST_CONTROL, FAST_RECOVER
}states_t;

// helpers
void diep(const char *s) {
    perror(s);
    exit(1);
}

// FROM GNU C std definition
//  struct timeval {
//    long tv_sec;    /* seconds */
//    long tv_usec;   /* microseconds 10^-6 */
// };
timeval delta_time(timeval& later, timeval& former){
    uint64_t t1_micros = later.tv_sec * 1000000 + later.tv_usec;
    uint64_t t2_micros = former.tv_sec * 1000000 + former.tv_usec;
    if (t1_micros < t2_micros)
        diep("timing error");
    uint64_t delta_time = t1_micros - t2_micros;
    timeval ret;
    ret.tv_sec = (long)(delta_time / 1000000);
    ret.tv_usec = delta_time % 1000000;
    return ret;
}

// timeval delta_time_abs(timeval& later, timeval& former)
// {
//     int64_t t1_micros = later.tv_sec * 1000000 + later.tv_usec;
//     int64_t t2_micros = former.tv_sec * 1000000 + former.tv_usec;
//     uint64_t delta_time = abs(t1_micros - t2_micros);
//     timeval ret;
//     ret.tv_sec = (long)(delta_time / 1000000);
//     ret.tv_usec = delta_time % 1000000;
//     return ret;
// }

// timeval add_time(timeval& later, timeval& former)
// {
//     uint64_t t1_micros = later.tv_sec * 1000000 + later.tv_usec;
//     uint64_t t2_micros = former.tv_sec * 1000000 + former.tv_usec;
//     uint64_t add_time = t1_micros + t2_micros;
//     timeval ret;
//     ret.tv_sec = (long)(add_time / 1000000);
//     ret.tv_usec = add_time % 1000000;
//     return ret;
// }

// timeval multiply_time(timeval& later, float multiplier)
// {
//     uint64_t t1_micros = later.tv_sec * 1000000 + later.tv_usec;
//     uint64_t multiply_time = t1_micros * multiplier;
//     timeval ret;
//     ret.tv_sec = (long)(multiply_time / 1000000);
//     ret.tv_usec = multiply_time % 1000000;
//     return ret;
// }

// uint64_t delta_time_micros(timeval& later, timeval& former){
//     uint64_t t1_micros = later.tv_sec * 1000000 + later.tv_usec;
//     uint64_t t2_micros = former.tv_sec * 1000000 + former.tv_usec;
//     if (t1_micros < t2_micros)
//         diep("timing error");
//     return t1_micros - t2_micros;
// }

// return 0 is a is bigger, return 1 if b is bigger, return 2 if tied
int compare_time(timeval& a, timeval& b){
    uint64_t ta_micros = a.tv_sec * 1000000 + a.tv_usec;
    uint64_t tb_micros = b.tv_sec * 1000000 + b.tv_usec;
    if (ta_micros > tb_micros){
        return 0;
    }else if(ta_micros < tb_micros){
        return 1;
    }else{
        return 2;
    }
}

// data structures
class Packet{
public:
    Packet(): sent_out(false), sent_time({0,0}), timeout({0,0}){
        data_pkt.ID = 0;
        data_pkt.datasize = 0;
        bzero((void*)data_pkt.data, MAX_DATA_SIZE);
    }
    bool sent_out;
    timeval sent_time;
    timeval timeout;
    data_pkt_t data_pkt;
};

// TCP State Machine
class StateMachine{
public:
    StateMachine(unsigned long long int _bytesToTransfer): 
        SST(DEFAULT_SST), 
        CW(STARTER_CW), 
        dupACK_cnt(0),
        window_base(1),
        pkt_loaded(0), 
        bytesToTransfer(_bytesToTransfer),
        cur_state(SLOW_START),
        transmit_done(false), 
        fill_nonsense(false){
        // make space for the window with default cw
        pkt_window.resize(STARTER_CW);
    }

    // load the next packet from file, inc the pkt_loaded field
    // return 0 for success, -1 for failure, -2 for end of read 
    // make sure that when you call this, the list has been adjusted (by the CW and received ACK)
    int load_packets(FILE* fp){
        if (fp == NULL)
            return FAILURE;
        // unsigned long int window_tail = window_base + floor(CW) - 1;
#ifdef DEBUG_ENV
        if ((window_base -1 + pkt_window.size()) * MAX_DATA_SIZE < bytesToTransfer)
        {
            assert(pkt_window.size() == floor(CW));
        }
#endif
        unsigned long int packet_slot_idx = 0;
        for (auto window_iter = pkt_window.begin(); window_iter != pkt_window.end(); window_iter++){
            // find out which buffer should I load data
            // cout<<"packet_slot: "<<packet_slot_idx<<" --> ";
            // find unloaded packet in window, load them from either disk or memory
            unsigned long int new_ID = window_base + packet_slot_idx;
            // cout<<"new_ID: "<<new_ID<<" --> ";
            if (window_iter->data_pkt.ID != 0){
                packet_slot_idx ++;
                continue;
            }
            if (pkt_loaded > 0 && pkt_loaded >= new_ID){
                // then there must be data inside the backup list
#ifdef DEBUG_ENV
                assert(pkt_backup.size()>0);
#endif
                // METHOD1
                // -- copy data from memory to memory, much faster than file operation
#if 1 == COPY_METHOD
                window_iter->data_pkt = pkt_backup.begin()->data_pkt;
                window_iter->sent_out = false;
                window_iter->sent_time = TIMEVAL_INIT;
                window_iter->timeout = TIMEOUT_CONST;

#ifdef DEBUG_ENV
                assert(window_iter->data_pkt.ID == new_ID);
#endif
                pkt_backup.erase(pkt_backup.begin());
#endif
                // METHOD2
                // -- list splice can make it even faster
#if 2 == COPY_METHOD
                pkt_window.splice(window_iter, pkt_backup, pkt_backup.begin());
                auto new_node_iter = prev(window_iter);
                pkt_window.erase(window_iter);
                new_node_iter->sent_out = false;
                new_node_iter->sent_time = TIMEVAL_INIT;
                new_node_iter->timeout = TIMEOUT_CONST;
                window_iter = new_node_iter;
#ifdef DEBUG_ENV
                assert(new_node_iter->data_pkt.ID == new_ID);
#endif
#endif
            }else{
                // load from disk
                unsigned long long int data_loaded = pkt_loaded*MAX_DATA_SIZE;
                if (data_loaded < bytesToTransfer){
#ifdef DEBUG_ENV
                    assert(pkt_backup.size() == 0);
#endif
                    int size_to_load = (data_loaded+MAX_DATA_SIZE > bytesToTransfer)? bytesToTransfer-data_loaded : MAX_DATA_SIZE;
                    // load from file into this packet memory space
                    // exceed filelen, or reading from stream like /dev/null
                    if (fill_nonsense){
                        for (int i=0; i < size_to_load; i++){
                            window_iter->data_pkt.data[i] = FILLER_WORD;
                        }
                        pkt_loaded ++;
                        window_iter->data_pkt.ID = pkt_loaded;
                        window_iter->data_pkt.datasize = size_to_load;
                        packet_slot_idx ++;
                        continue;
                    }
                    int ret = fread((void*)(window_iter->data_pkt.data), sizeof(char), size_to_load, fp);
                    pkt_loaded++;
                    window_iter->data_pkt.ID = pkt_loaded;
#ifdef DEBUG_ENV
                    assert(pkt_loaded == new_ID);
#endif
                    window_iter->data_pkt.datasize = size_to_load;
                    if (ret != size_to_load || pkt_loaded * MAX_DATA_SIZE >= bytesToTransfer)
                    {
                        // no longer need to read: exceed bytesToTransfer or get EOF
                        fill_nonsense = true;
                        int start = (ret < 0)? 0 : ret;
                        for (int i = start; i<size_to_load; i++){
                            window_iter->data_pkt.data[i] = FILLER_WORD;
                        }
                        return END_READ;
                    }
                }
            }
            packet_slot_idx ++;
        }
        return SUCCESS;
    }

    // send out all unsent packets within the window
    int send_packets(){
        for (auto it = pkt_window.begin(); it != pkt_window.end(); it++){
            if ((!it->sent_out)&&(it->data_pkt.ID != 0)&&(it->data_pkt.datasize>0)){
                int data_to_send = sizeof(data_header_t) + it->data_pkt.datasize;
                it->data_pkt.ID = htonl(it->data_pkt.ID);
                it->data_pkt.datasize = htons(it->data_pkt.datasize);
                if (sendto(s, (void *)&(it->data_pkt), data_to_send, 0,(struct sockaddr *) &si_other, slen)>=0){
                    it->data_pkt.ID = ntohl(it->data_pkt.ID);
                    it->data_pkt.datasize = ntohs(it->data_pkt.datasize);
                    it->timeout = TIMEOUT_CONST;
                    timeval sent_time_tmp;
                    gettimeofday(&sent_time_tmp, NULL);
                    it->sent_out = true;
                    it->sent_time = sent_time_tmp;
                }else{
                    diep("sendto() failed");
                }
            }
        }
        return SUCCESS;
    }

    // recv ACKS: dupACK, newACK, don't care ACK, timeout ...
    int recv_packets(){
        // set up timer
        auto it = pkt_window.begin();
        // sanity check
        if (!it->sent_out)
            diep("trying to receive a unsent packet, bug occurs");
        timeval cur_time;
        gettimeofday(&cur_time, NULL);
        timeval time_elapsed = delta_time(cur_time, it->sent_time);
        bool already_timeout = false;
        timeval wait_time = {0,0};
        if (compare_time(it->timeout, time_elapsed) != 0){
            already_timeout = true;
        }else{
            wait_time = delta_time(it->timeout, time_elapsed);
        }
        ack_pkt_t recv_ack;
        int ret;
        if (!already_timeout){
            int setopt_ret = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &wait_time, sizeof(timeval));
            if (setopt_ret < 0){
                diep("failed to set socket option");
                // printf("WARNING: INVALID SOCKET OPTION\n");
            }   
            // ret = recvfrom(s, (void*)&recv_ack, sizeof(ack_pkt_t), 0, (struct sockaddr *)&their_addr, &addr_len);
            ret = recvfrom(s, (void*)&recv_ack, sizeof(ack_pkt_t), 0, (struct sockaddr *)&si_other, (socklen_t *)&slen);
            if (ret < 0){
                already_timeout = true;
            }
            // else
            // {
            //     gettimeofday(&cur_time, NULL);
            //     time_elapsed = delta_time(cur_time, it->sent_time);
            //     timeval time_difference = delta_time_abs(TIME_RTT, time_elapsed);
            //     timeval time_temp_1, time_temp_2;
            //     // printf("time_elapsed: %ld, previous TIMEOUT_CONST: %ld, RTT: %ld, DELTA: %ld, ", time_elapsed.tv_usec, TIMEOUT_CONST.tv_usec, TIME_RTT.tv_usec, TIME_DELTA.tv_usec);
            //     time_temp_1 = multiply_time(TIME_RTT, TIME_ALPHA);
            //     time_temp_2 = multiply_time(time_elapsed, 1 - TIME_ALPHA);
            //     TIME_RTT = add_time(time_temp_1, time_temp_2);
            //     time_temp_1 = multiply_time(TIME_DELTA, TIME_ALPHA);
            //     time_temp_2 = multiply_time(time_difference, 1 - TIME_ALPHA);
            //     TIME_DELTA = add_time(time_temp_1, time_temp_2);
            //     time_temp_2 = multiply_time(TIME_DELTA, TIME_DELTA_FACTOR);
            //     TIMEOUT_CONST = add_time(TIME_RTT, time_temp_2);
            //     // printf("time_elapsed: %ld, current TIMEOUT_CONST: %ld, RTT: %ld, DELTA: %ld\n", time_elapsed.tv_usec, TIMEOUT_CONST.tv_usec, TIME_RTT.tv_usec, TIME_DELTA.tv_usec);
            //     // if (0 != TIMEOUT_CONST.tv_sec)
            //     // {
            //     //     printf("TIMEOUT_CONST.tv_sec is not 0!\n");
            //     // }
            // }
        }
        if (already_timeout){
            // timeout handle
            // cout<<"timeout!!!"<<endl;
            SST = CW/2.0f;
            if (SST < MINIMUM_SST)
                SST = MINIMUM_SST;
            CW = 1.0f;
            dupACK_cnt = 0;
            cur_state = SLOW_START; // transition from FAST_RECOVER and CONJEST_CONTROL
            // make the timeout packet as not sent
            it->sent_out = false;
            it->sent_time = TIMEVAL_INIT;
            it->timeout = TIMEOUT_CONST;
        }else{
            // no timeout, but maybe newACK / dupACK / useless ACK
            if (ret != sizeof(ack_pkt_t))
                diep("received incomplete ack");
            unsigned long int ack_id = ntohl(recv_ack.ID);
            if (ack_id * MAX_DATA_SIZE >= bytesToTransfer){
                // finish everything
                transmit_done = true;
                return 0;
            }
            // unsigned long int window_tail = window_base + floor(CW) - 1;
            // auto pkt_iter = pkt_window.begin();
            if (ack_id >= window_base){
                // packets to move window, must be new ACK
                int advanced_num = (int)(ack_id - window_base);
                // eg. window[4...7], ack=7, i should be 0, 1, 2, 3
                for (int i=0; i<=advanced_num; i++){
                    switch(cur_state){
                        case SLOW_START:
                            CW ++;
                            dupACK_cnt = 0;
                            deliver_window_base();
                            if (CW >= SST){
                                cur_state = CONJEST_CONTROL;
                            }
                            break;
                        case CONJEST_CONTROL:
                            CW += (1.0f/(float)floor(CW));
                            dupACK_cnt = 0;
                            deliver_window_base();
                            break;
                        case FAST_RECOVER:
                            // move out of fast recover
                            CW = SST;
                            if (CW < MINIMUM_CW)
                            {
                                CW = MINIMUM_CW;
                            }
                            dupACK_cnt = 0;
                            deliver_window_base();
                            cur_state = CONJEST_CONTROL;
                            break;
                        default:
                            break;
                    }
                }
            }else{
                // dupACK or don't care ACK
                if (ack_id != window_base - 1)
                    return 0;
                // dupACK
                dupACK_cnt ++;
                if (cur_state == FAST_RECOVER){
                    CW += 1.0f;
                }
                if (dupACK_cnt == 3){
                    switch(cur_state){
                        case SLOW_START:
                        case CONJEST_CONTROL:
                            SST = CW/2.0f;
                            if (SST < MINIMUM_SST)
                                SST = MINIMUM_SST;
                            CW = SST + 3.0f;
                            // retransmit the hold packet by marking the packet as not transmitted
                            pkt_window.begin()->sent_out = false;
                            pkt_window.begin()->sent_time = TIMEVAL_INIT;
                            pkt_window.begin()->timeout = TIMEOUT_CONST;
                            cur_state = FAST_RECOVER;
                            break;
                        case FAST_RECOVER:
                        default:
                            break;   
                    }
                }
            }
        }
        window_resize(floor(CW));
        return 0;
    }

    float SST;
    float CW;
    int dupACK_cnt;
    unsigned long int window_base;
    unsigned long int pkt_loaded;
    unsigned long long int bytesToTransfer;
    states_t cur_state;
    bool transmit_done;
    bool fill_nonsense;
private:
    int window_resize(int target_size){
        int cur_size = pkt_window.size();
        if (target_size == cur_size)
            return 0;
        if (target_size > cur_size){
            // window becomes larger, insert empty blocks into CW
            for (int i=0;((window_base - 1 + cur_size + i) * MAX_DATA_SIZE < bytesToTransfer) && (i<target_size-cur_size); i++){
                pkt_window.emplace_back();
            }
        }else{
            // window becomes smaller
            auto iter = pkt_window.begin();
            advance(iter, target_size);
            list<Packet> tmplist;
            tmplist.splice(tmplist.begin(), pkt_window, iter, pkt_window.end());
            auto tmpiter = tmplist.begin();
            for (; tmpiter != tmplist.end(); tmpiter++){
                if (tmpiter->data_pkt.ID == 0)
                    break;
            }
            tmplist.erase(tmpiter, tmplist.end());
            pkt_backup.splice(pkt_backup.begin(), tmplist, tmplist.begin(), tmplist.end());
        }
        return 0;
    }

    int deliver_window_base(){
        window_base ++;
        if (pkt_window.size() > 0) // newACK might be larger than the window tail, so list can be emptied early
        {
            pkt_window.erase(pkt_window.begin());
        }
        else
        {
            pkt_backup.erase(pkt_backup.begin());
        }
        return 0;
    }

    list<Packet> pkt_window;
    // if the cw shrink, use this list to avoid loading from disk again
    list<Packet> pkt_backup;
};

void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer) {
    //Open the file
    FILE *fp;
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("Could not open file to send.");
        exit(1);
    }

	/* Determine how many bytes to transfer */

    slen = sizeof (si_other);

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_other, 0, sizeof (si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(hostUDPport);
    if (inet_aton(hostname, &si_other.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }

    // fseek(fp, 0, SEEK_END);
	// unsigned long long int filelen = (unsigned long long int)ftell(fp);	
	// fseek (fp, 0, SEEK_SET);

    // bytesToTransfer = (bytesToTransfer > filelen)? filelen : bytesToTransfer;

	/* Send data and receive acknowledgements on s*/
    StateMachine state_machine(bytesToTransfer);
    // cout<<"state machine status: CW="<<floor(state_machine.CW)<<"    "<<"SST="<<state_machine.SST<<"    "<<"window_base="<<state_machine.window_base<<endl;
    while (!state_machine.transmit_done){
        // cout<<"loading packets --> \n";
        state_machine.load_packets(fp);
        // cout<<"sending packets --> \n";
        state_machine.send_packets();
        // cout<<"receiving packets -->\n";
        state_machine.recv_packets();
        // cout<<"state machine status: state="<<state_machine.cur_state<<"    "<<"CW="<<state_machine.CW<<"    "<<"SST="<<state_machine.SST<<"    "<<"window_base="<<state_machine.window_base<<endl;
    }
    cout<<"transmission finished"<<endl;
    data_pkt_t finish_pack;
    finish_pack.ID = htonl(PKT_FINISH);
    ack_pkt_t fin_ack;
    for (int _i=0; _i<10; _i++){
        if (sendto(s, (void *)&finish_pack, sizeof(data_pkt_t), 0,(struct sockaddr *) &si_other, slen) < 0){
            diep("internal error in sending\n");
        }
        timeval wait_time = TIMEOUT_CONST;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &wait_time, sizeof(timeval)) < 0){
            diep("failed to set socket option");
        } 
        // if (recvfrom(s, (void*)&fin_ack, sizeof(ack_pkt_t), 0, (struct sockaddr *)&their_addr, &addr_len) < 0){
        //     diep("internal error in recvfrom\n");
        // }
        if (recvfrom(s, (void*)&fin_ack, sizeof(ack_pkt_t), 0, (struct sockaddr *)&si_other,(socklen_t *)&slen) < 0){
            diep("internal error in recvfrom\n");
        }
        if (ntohl(fin_ack.ID) == PKT_FINISH){
            break;
        }
    }


    printf("Closing the socket\n");
    close(s);
    return;

}

/*
 * 
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;
    unsigned long long int numBytes;

    if (argc != 5) {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    udpPort = (unsigned short int) atoi(argv[2]);
    numBytes = atoll(argv[4]);



    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);


    return (EXIT_SUCCESS);
}


