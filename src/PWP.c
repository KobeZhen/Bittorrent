/*
 * this file includes the implements of PWP
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include "PWP.h"
#include "list.h"
#include "global.h"


int first_request = 1;
static char set_bit[8] = {1,2,4,8,16,32,64,128};

static int readn( int fd, void *bp, size_t len)
{
    int cnt;
    int rc;

    cnt = len;
    while ( cnt > 0 )
    {
        rc = recv( fd, bp, cnt, 0 );
        if ( rc < 0 )               /* read error? */
        {
            if ( errno == EINTR )   /* interrupted? */
                continue;           /* restart the read */
            return -1;              /* return error */
        }
        if ( rc == 0 )              /* EOF? */
            return len - cnt;       /* return short count */
        bp += rc;
        cnt -= rc;
    }
    return len;
}

// get/set the bit value at the index
static inline int get_bit_at_index(char *info, int index){
    unsigned char ch = info[index/8];
    int offset = 7 - index%8;
    return (ch >> offset) & 1;
}
static inline void set_bit_at_index(char *info, int index, int bit){
    assert(bit == 0 || bit == 1);
    int offset = 7 - index%8;
    if(bit)
        info[index/8] = info[index/8] | set_bit[offset];
    else
        info[index/8] = info[index/8] & (~set_bit[offset]);
}

//when error ,drop connection
static inline void drop_conn(P2PCB *currP2P){
    list_del(&currP2P->list);
    free(currP2P->oppsite_piece_info);
    close(currP2P->connfd);
    free(currP2P);
}


//whether a bitfield is empty or complete
static inline int is_bitfield_empty(char *bitfield,int len){
    int flag = 1;
    for(int i = 0; i < len; i++){
        if(bitfield[i] != 0){
            flag = 0;
            break;
        }
    }
    return flag;
}

static inline int is_bitfield_complete(char *bitfield,int len){
    int flag = 1;
    for(int i = 0; i < len; i++){
        if(~bitfield[i] != 0){
            flag = 0;
            break;
        }
    }
    return flag;
}

//select next piece/sub-piece
int select_next_piece(){
    int index = -1;

    return index;
}

int select_next_subpiece(int index,int *begin,int *length){
    int flag = 1;

    return flag;
}



// init p2p control block
void init_p2p_block(P2PCB *node){
    node->am_choking = 1;
    node->am_interested = 0;
    node->peer_choking = 1;
    node->peer_interested = 0;
    list_init(&node->list);
}

//whether a bitfield is interested about another
int is_interested_bitfield(char *bitfield1, char *bitfield2, int len){
    char interest_bitfield[len];
    for(int i = 0; i < len; i++){
        interest_bitfield[i] = (~bitfield1[i]) & bitfield2[i]; 
    }
    if(is_bitfield_empty(interest_bitfield,len))
        return 0;
    else
        return 1;
}

//listening PEER_PORT for handshake,this function returns the listening sockfd 
int generate_listenfd(){ 
	int listenfd,connfd;
	struct sockaddr_in servaddr;

	if((listenfd = socket(AF_INET,SOCK_STREAM,0)) < 0){
		perror("Problem in creating the listening socket");
		exit(1);		
	}
    
    memset(&servaddr,0,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(PEER_PORT);

	bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr));
	listen(listenfd,8);

	return listenfd;
}

void* process_p2p_conn(void *param){
    //process param
    p2p_thread_param *currParam = (p2p_thread_param *)param;
    int connfd = currParam->connfd;
    int is_connecter = currParam->is_connecter;
    free(currParam);

    //init P2PCB
    P2PCB *currP2P = (P2PCB *)malloc(sizeof(P2PCB));
    memset(currP2P,0,sizeof(P2PCB));
    currP2P->connfd = connfd;
    init_p2p_block(currP2P); 
    
    int piece_info_len = globalInfo.g_torrentmeta->num_pieces/8 + (globalInfo.g_torrentmeta->num_pieces%8 != 0);
    currP2P->oppsite_piece_info = (char *)malloc(piece_info_len);
    memset(currP2P->oppsite_piece_info,0,piece_info_len);
    list_add_before(&P2PCB_head,&currP2P->list);
    
    //init variables
    char prefix[5];
    handshake_msg msg;

    //readn handshake msg
    if(readn(connfd,&msg,sizeof(handshake_msg)) > 0){
        printf("handshake recieved\n");
        if(strncmp(msg.info_hash, (char *)globalInfo.g_torrentmeta->info_hash,20) != 0){//wrong hash_info 
            printf("wrong handshake msg\n");
            drop_conn(currP2P);
            return NULL;
        }
        else{
            memcpy(currP2P->oppsite_peer_id,msg.peer_id,20);
            if(!is_connecter)
                send(connfd,&msg,sizeof(handshake_msg),0);//send back with local peer_id
        }
    }

    //send bitfield msg
    if(is_bitfield_empty(currTorrent.piece_info,piece_info_len)){
        char bitfield_msg[5+piece_info_len];
        *(int*)bitfield_msg = htonl(1+piece_info_len);
        bitfield_msg[4] = 5;
        strncpy(bitfield_msg+5,currTorrent.piece_info,piece_info_len);
        send(connfd,bitfield_msg,5+piece_info_len,0);
    }

    //process msgs
    while(readn(connfd, prefix, 4) > 0){
        int len = ntohl(*(int *)prefix);
        if(len == 0){//keep-alive
            continue;
        }
        else{
            readn(connfd,prefix+4,1);
        }
        switch(prefix[4]){
            case 0 : currP2P->peer_choking = 1;break;//choke
            case 1 : currP2P->peer_choking = 0;break;//unchoke
            case 2 : {//interested
                         currP2P->am_interested = 1;
                         send_unchoke_msg(connfd);
                         break;
                     }
            case 3 : currP2P->am_interested = 0;break;//not interested
            case 4 : {//have  
                         int index;
                         readn(connfd,&index,4);
                         index = ntohl(index);
                         set_bit_at_index(currP2P->oppsite_piece_info,index,1);
                         if(get_bit_at_index(currTorrent.piece_info,index) == 0){//send interest
                             send_interest_msg(connfd);
                             currP2P->peer_interested = 1;
                             if(first_request == 1){
                                 if(currP2P->am_choking == 0 && currP2P->peer_interested == 1){
                                     int begin;
                                     int length;
                                     select_next_subpiece(index,&begin,&length);
                                     send_request_msg(connfd,index,begin,length);
                                     first_request = 0;
                                 }
                             }
                         }
                         break;
                     }
            case 5 : {//bitfield
                         char bitfield[len-1];
                         readn(connfd,bitfield,len-1);
                         if(len - 1 != piece_info_len){
                            //wrong length of bitfield
                            printf("wrong length of bitfield\n");
                            drop_conn(currP2P);
                            return NULL;
                         }
                         //check idle bits
                         unsigned char ch = bitfield[len-2];
                         int offset = 8 - globalInfo.g_torrentmeta->num_pieces%8;
                         while(offset >= 1){
                             if(((ch >> (offset-1)) & 1) == 1){//wrong idle bits 
                                 printf("wrong idle bits\n");
                                 drop_conn(currP2P);
                                 return NULL;
                             }
                             offset--;
                         }
                         //set oppsite piece info
                         memcpy(currP2P->oppsite_piece_info,bitfield,len-1);

                         //to see whether interedted about oppsite peer
                         char *bitfield1 = currTorrent.piece_info;
                         char *bitfield2 = currP2P->oppsite_piece_info;
                         if(is_interested_bitfield(bitfield1,bitfield2,piece_info_len)){
                             send_interest_msg(connfd);
                             if(first_request == 1){//find the first interested piece then request
                                 for(int index = 0; index < globalInfo.g_torrentmeta->num_pieces; index++){
                                     if(get_bit_at_index(bitfield1,index) == 0 && 
                                        get_bit_at_index(bitfield2,index) == 1){
                                         if(currP2P->am_choking == 0 && currP2P->peer_interested == 1){
                                             int begin;
                                             int length;
                                             select_next_subpiece(index,&begin,&length);
                                             send_request_msg(connfd,index,begin,length);
                                             first_request = 0;
                                             break;
                                         }
                                     }
                                 }
                             }
                         }
                         break;
                     }
            case 6 : {//request
                        int payload[3];//<index><begin><length>
                        readn(connfd,payload,12);
                        int index = ntohl(payload[0]);
                        int begin = ntohl(payload[1]);
                        int length = ntohl(payload[2]);
                        if(length > (1 << 17)){
                            printf("the length in request is larger than 2^17\n");
                            drop_conn(currP2P);
                            return NULL ;
                        }
                        if(currP2P->am_interested == 1 && currP2P->peer_choking == 0){
                            //send the block
                            send_piece_msg(connfd,index,begin,length);
                        }
                        else{
                            printf("the request is invalid,aninterested = %d,peer_choking = %d\n",
                                    currP2P->am_interested,currP2P->peer_choking);
                        }
                        break;
                     }
            case 7 : {//piece
                        char payload[len-1];
                        readn(connfd,payload,len-1);
                        int index = ntohl(*(int*)payload);
                        int begin = ntohl(*(int*)(payload+4));
                        int length = len - 9;
                        //set_block(index,begin,length,payload+8);
                        if(!select_next_subpiece(index,&begin,&length)){//the piece is downloaded completely
                            set_bit_at_index(currTorrent.piece_info,index,1);//update local bitfield
                            if(is_bitfield_complete(currTorrent.piece_info,piece_info_len)){
                                //the file has been dowmloaded completely
                                printf("File has been downloaded completely\n");
                                ListHead *ptr_;
                                list_foreach(ptr_,&P2PCB_head){
                                    P2PCB *tmpP2P = list_entry(ptr_,P2PCB,list);
                                    if(tmpP2P->peer_interested == 1){
                                        tmpP2P->peer_interested = 0;
                                        send_not_interest_msg(tmpP2P->connfd);
                                    }
                                }
                                continue;
                            }

                            ListHead *ptr;
                            int next_index = select_next_piece();
                            int count = 0;
                            //update peer_interested,send not interested msg,request for next piece
                            list_foreach(ptr,&P2PCB_head){
                                P2PCB *tmpP2P = list_entry(ptr,P2PCB,list);
                                char *bitfield1 = currTorrent.piece_info;
                                char *bitfield2 = tmpP2P->oppsite_piece_info;
                                if(!is_interested_bitfield(bitfield1,bitfield2,piece_info_len) && 
                                    tmpP2P->peer_interested == 1){//change to be not interested
                                    send_not_interest_msg(tmpP2P->connfd);
                                    tmpP2P->peer_interested = 0;
                                }
                                //request for next piece
                                if(count < 5 && get_bit_at_index(bitfield2,next_index) == 1){
                                    if(tmpP2P->am_choking == 0 && tmpP2P->peer_interested == 1){
                                        int begin_;
                                        int length_;
                                        if(!select_next_subpiece(next_index,&begin_,&length_))
                                            break;//no more sub piece
                                        send_request_msg(tmpP2P->connfd,next_index,begin_,length_);
                                        count++;
                                    }   
                                }   
                            }
                        }
                        else{// request for next sub piece
                            if(currP2P->peer_interested == 1 && currP2P->am_choking == 0)
                                send_request_msg(connfd,index,begin,length);
                        }
                        break;
                     }
            case 8 : {//cancel
                        break;
                     }
        }
    }

    printf("exit the p2p msg process\n");
    return NULL;
}

void send_have_msg(int connfd,int index){
    char have_msg[9];
    *(int*)have_msg = htonl(5);
    have_msg[4] = 4;
    *(int*)(have_msg+5) = htonl(index);
    send(connfd,have_msg,9,0);
}

void send_request_msg(int connfd,int index,int begin,int length){
    char request_msg[17];
    *(int*)request_msg = htonl(13);
    request_msg[4] = 6;
    *(int*)(request_msg+5) = htonl(index);
    *(int*)(request_msg+9) = htonl(begin);
    *(int*)(request_msg+13) = htonl(length);
    send(connfd,request_msg,17,0);
}

void send_interest_msg(int connfd){
    char interest_msg[5];
    *(int*)interest_msg = htonl(1);
    interest_msg[4] = 2;
    send(connfd,interest_msg,5,0);
}

void send_choke_msg(int connfd){
    char choke_msg[5];
    *(int*)choke_msg = htonl(1);
    choke_msg[4] = 0;
    send(connfd,choke_msg,5,0);
}

void send_not_interest_msg(int connfd){
    char not_interest_msg[5];
    *(int*)not_interest_msg = htonl(1);
    not_interest_msg[4] = 2;
    send(connfd,not_interest_msg,5,0);
}

void send_unchoke_msg(int connfd){
    char unchoke_msg[5];
    *(int*)unchoke_msg = htonl(1);
    unchoke_msg[4] = 0;
    send(connfd,unchoke_msg,5,0);
}   

void send_piece_msg(int connfd, int index, int begin, int length){
    char block[length];
    //get_block(index,begin,length,block);
    char piece_msg[13 + length];
    *(int *)piece_msg = 9 + length;
    piece_msg[4] = 7;
    *(int *)(piece_msg+5) = index;
    *(int *)(piece_msg+9) = begin;
    memcpy(piece_msg+13,block,length);
    send(connfd,piece_msg,13+length,0);
}



