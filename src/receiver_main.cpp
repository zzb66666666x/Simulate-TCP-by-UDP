/* 
 * File:   receiver_main.c
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

#include "common.h"


struct sockaddr_in si_me, si_other;
int s, slen;

void diep(const char *s) {
    perror(s);
    exit(1);
}

struct receiver_buffer_struct
{
    data_pkt_t pkt;
    struct receiver_buffer_struct* next;
    struct receiver_buffer_struct* prev;
};

typedef struct receiver_buffer_struct receiver_buffer_t;


void reliablyReceive(unsigned short int myUDPport, char* destinationFile) {
    
    slen = sizeof (si_other);


    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        diep("socket");
    }

    memset((char *) &si_me, 0, sizeof (si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(myUDPport);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("Now binding\n");
    if (bind(s, (struct sockaddr*) &si_me, sizeof (si_me)) == -1)
    {
        diep("bind");
    }

	/* Now receive data and send acknowledgements */
    unsigned long int window_base = 1;
    receiver_buffer_t* receiver_buffer = NULL;
    unsigned long int ack_ID = 0;
    ack_pkt_t ack_pkt;
    ack_pkt.ID = htonl(ack_ID);
    data_pkt_t pkt_current;

    FILE *fptr = fopen(destinationFile, "w");
    if (fptr == NULL)
    {
        close(s);
        diep("cannot write to file output\n");
    }

    //socklen_t addr_len;
    while(1)
    {
        recvfrom(s, (void *)&(pkt_current), sizeof(data_pkt_t), 0, (struct sockaddr *)&si_other, (socklen_t*)&slen);
        pkt_current.ID = ntohl(pkt_current.ID);
        pkt_current.datasize = ntohs(pkt_current.datasize);
        if (window_base > pkt_current.ID)
        {
            //printf("receive previous packet %ld\n", pkt_current.ID);
            sendto(s, (void *)&(ack_pkt), sizeof(ack_pkt_t), 0, (struct sockaddr *)&si_other, slen);
        }
        else if (window_base == pkt_current.ID)
        {
            fwrite((void*)&(pkt_current.data), sizeof(char), pkt_current.datasize, fptr);
            //printf("receive and write in-order packet %ld\n", pkt_current.ID);
            window_base++;
            ack_ID++;
            while ((NULL != receiver_buffer) && (window_base == receiver_buffer->pkt.ID))
            {
                fwrite((void *)&(receiver_buffer->pkt.data), sizeof(char), receiver_buffer->pkt.datasize, fptr);
                //printf("write out of order packet %ld\n", receiver_buffer->pkt.ID);
                window_base++;
                ack_ID++;
                if (NULL != receiver_buffer->next)
                {
                    receiver_buffer = receiver_buffer->next;
                    free(receiver_buffer->prev);
                    receiver_buffer->prev = NULL;
                }
                else
                {
                    free(receiver_buffer);
                    receiver_buffer = NULL;
                }
            }
            ack_pkt.ID = htonl(ack_ID);
            sendto(s, (void *)&(ack_pkt), sizeof(ack_pkt_t), 0, (struct sockaddr *)&si_other, slen);
        }
        else if ((window_base < pkt_current.ID) && (PKT_FINISH != pkt_current.ID))
        {
            if (NULL == receiver_buffer)
            {
                receiver_buffer = (receiver_buffer_t *)malloc(sizeof(receiver_buffer_t));
                receiver_buffer->pkt = pkt_current;
                receiver_buffer->next = NULL;
                receiver_buffer->prev = NULL;
                //printf("receive and buffer out of order packet %ld\n", pkt_current.ID);
            }
            else
            {
                receiver_buffer_t *buffer_iter = receiver_buffer;
                while ((buffer_iter->pkt.ID < pkt_current.ID) && (NULL != buffer_iter->next))
                {
                    buffer_iter = buffer_iter->next;
                }
                if (buffer_iter->pkt.ID == pkt_current.ID)
                {
                    //printf("receive already-received out of order packet %ld\n", pkt_current.ID);
                }
                else if (buffer_iter->pkt.ID > pkt_current.ID)
                {
                    receiver_buffer_t *current_buffer = (receiver_buffer_t *)malloc(sizeof(receiver_buffer_t));
                    current_buffer->pkt = pkt_current;
                    current_buffer->next = buffer_iter;
                    current_buffer->prev = buffer_iter->prev;
                    if (NULL != buffer_iter->prev)
                    {
                        buffer_iter->prev->next = current_buffer;
                    }
                    else
                    {
                        receiver_buffer = current_buffer;
                    }
                    buffer_iter->prev = current_buffer;
                    //printf("receive and buffer out of order packet %ld\n", pkt_current.ID);
                }
                else
                {
                    receiver_buffer_t *current_buffer = (receiver_buffer_t *)malloc(sizeof(receiver_buffer_t));
                    current_buffer->pkt = pkt_current;
                    current_buffer->next = NULL;
                    current_buffer->prev = buffer_iter;
                    buffer_iter->next = current_buffer;
                    //printf("receive and buffer out of order packet %ld\n", pkt_current.ID);
                }
            }
            sendto(s, (void *)&(ack_pkt), sizeof(ack_pkt_t), 0, (struct sockaddr *)&si_other, slen);
        }
        else
        {
            ack_ID = PKT_FINISH;
            ack_pkt.ID = htonl(ack_ID);
            sendto(s, (void *)&(ack_pkt), sizeof(ack_pkt_t), 0, (struct sockaddr *)&si_other, slen);
            break;
        }
    }

    fclose(fptr);
    close(s);
	printf("%s received\n", destinationFile);
    return;
}

/*
 * 
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;

    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (unsigned short int) atoi(argv[1]);

    reliablyReceive(udpPort, argv[2]);
}

