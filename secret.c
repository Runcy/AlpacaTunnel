#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "secret.h"
#include "log.h"
#include "ip.h"

struct peer_profile_t* add_peer()
{
    struct peer_profile_t * p = (struct peer_profile_t *)malloc(sizeof(struct peer_profile_t));
    if(p == NULL)
    {
        ERROR(errno, "add_peer: malloc failed");
        return NULL;
    }
    else
        bzero(p, sizeof(struct peer_profile_t));

    struct sockaddr_in * peeraddr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    if(peeraddr == NULL)
    {
        ERROR(errno, "add_peer: malloc failed");
        delete_peer(p);
        return NULL;
    }
    else
    {
        bzero(peeraddr, sizeof(struct sockaddr_in));
        p->peeraddr = peeraddr;
    }

    uint32_t * pkt_index_array_pre = (uint32_t *)malloc(SEQ_LEVEL_1*sizeof(uint32_t));
    if(peeraddr == NULL)
    {
        ERROR(errno, "add_peer: malloc failed");
        delete_peer(p);
        return NULL;
    }
    else
    {
        bzero(pkt_index_array_pre, SEQ_LEVEL_1*sizeof(uint32_t));
        p->pkt_index_array_pre = pkt_index_array_pre;
    }

    uint32_t * pkt_index_array_now = (uint32_t *)malloc(SEQ_LEVEL_1*sizeof(uint32_t));
    if(peeraddr == NULL)
    {
        ERROR(errno, "add_peer: malloc failed");
        delete_peer(p);
        return NULL;
    }
    else
    {
        bzero(pkt_index_array_now, SEQ_LEVEL_1*sizeof(uint32_t));
        p->pkt_index_array_now = pkt_index_array_now;
    }

    p->tcp_info = (struct tcp_info_t *)malloc(TCP_SESSION_CNT * sizeof(struct tcp_info_t));
    if(p->tcp_info == NULL)
    {
        ERROR(errno, "add_peer: malloc failed");
        delete_peer(p);
        return NULL;
    }
    else
        bzero(p->tcp_info, TCP_SESSION_CNT * sizeof(struct tcp_info_t));
    p->tcp_cnt = 0;

    p->timerfd_info = (struct timerfd_info_t *)malloc(sizeof(struct timerfd_info_t));
    if(p->timerfd_info == NULL)
    {
        ERROR(errno, "add_peer: malloc failed");
        delete_peer(p);
        return NULL;
    }
    else
        bzero(p->timerfd_info, sizeof(struct timerfd_info_t));

    p->timerfd_info->fd_max_cnt = TIMER_CNT;
    
    p->timerfd_info->ack_array_size = SEQ_LEVEL_1;

    p->timerfd_info->ack_array_pre = (struct ack_info_t *)malloc((p->timerfd_info->ack_array_size + 1) * sizeof(struct ack_info_t));
    if(p->timerfd_info->ack_array_pre == NULL)
    {
        ERROR(errno, "add_peer: malloc failed");
        delete_peer(p);
        return NULL;
    }
    else
        bzero(p->timerfd_info->ack_array_pre, (p->timerfd_info->ack_array_size + 1) * sizeof(struct ack_info_t));

    p->timerfd_info->ack_array_now = (struct ack_info_t *)malloc((p->timerfd_info->ack_array_size + 1) * sizeof(struct ack_info_t));
    if(p->timerfd_info->ack_array_now == NULL)
    {
        ERROR(errno, "add_peer: malloc failed");
        delete_peer(p);
        return NULL;
    }
    else
        bzero(p->timerfd_info->ack_array_now, (p->timerfd_info->ack_array_size +1 ) * sizeof(struct ack_info_t));

    p->flow_src = (struct flow_profile_t *)malloc(sizeof(struct flow_profile_t));
    if(p->flow_src == NULL)
    {
        ERROR(errno, "add_peer: malloc failed");
        delete_peer(p);
        return NULL;
    }
    else
        bzero(p->flow_src, sizeof(struct flow_profile_t));

    p->flow_src->ba_pre = bit_array_create(SEQ_LEVEL_1);
    if(p->flow_src->ba_pre == NULL)
    {
        ERROR(errno, "add_peer: malloc failed");
        delete_peer(p);
        return NULL;
    }
    else
        bit_array_clearall(p->flow_src->ba_pre);

    p->flow_src->ba_now = bit_array_create(SEQ_LEVEL_1);
    if(p->flow_src->ba_now == NULL)
    {
        ERROR(errno, "add_peer: malloc failed");
        delete_peer(p);
        return NULL;
    }
    else
        bit_array_clearall(p->flow_src->ba_now);

    p->valid = true;

    return p;
}

int close_all_timerfd(struct ack_info_t timerfd[], int num)
{
    int i;
    for(i = 0; i < num; i++)
    {
        if(timerfd[i].fd != 0)
        {
            close(timerfd[i].fd);
            timerfd[i].fd = 0;
            timerfd[i].cnt = 0;
        }
    }
    return 0;
}

int delete_peer(struct peer_profile_t* p)
{
    if(p == NULL)
        return 0;

    if(p->peeraddr != NULL)
        free(p->peeraddr);

    if(p->pkt_index_array_pre != NULL)
        free(p->pkt_index_array_pre);

    if(p->pkt_index_array_now != NULL)
        free(p->pkt_index_array_now);

    if(p->tcp_info != NULL)
        free(p->tcp_info);

    if(p->timerfd_info != NULL)
    {
        if(p->timerfd_info->ack_array_pre != NULL)
        {
            close_all_timerfd(p->timerfd_info->ack_array_pre, (SEQ_LEVEL_1+1));
            free(p->timerfd_info->ack_array_pre);
        }
    
        if(p->timerfd_info->ack_array_now != NULL)
        {
            close_all_timerfd(p->timerfd_info->ack_array_now, (SEQ_LEVEL_1+1));
            free(p->timerfd_info->ack_array_now);
        }
        free(p->timerfd_info);
    }

    if(p->flow_src != NULL)
    {
        bit_array_destroy(p->flow_src->ba_pre);
        bit_array_destroy(p->flow_src->ba_now);
        free(p->flow_src);
    }

    free(p);

    return 0;
}

//note: file descriptors in dst will/must be closed, FD in src will be changed to NUL.
int copy_peer(struct peer_profile_t* dst, struct peer_profile_t* src)
{
    if(dst == NULL || src == NULL)
    {
        ERROR(0, "copy_peer: dst or src is NULL");
        return -1;
    }

    if(dst->peeraddr == NULL || src->peeraddr == NULL)
    {
        ERROR(0, "copy_peer: dst->peeraddr or src->peeraddr is NULL");
        return -1;
    }

    if(dst->pkt_index_array_pre == NULL || src->pkt_index_array_pre == NULL)
    {
        ERROR(0, "copy_peer: dst->pkt_index_array_pre or src->pkt_index_array_pre is NULL");
        return -1;
    }

    if(dst->pkt_index_array_now == NULL || src->pkt_index_array_now == NULL)
    {
        ERROR(0, "copy_peer: dst->pkt_index_array_now or src->pkt_index_array_now is NULL");
        return -1;
    }

    if(dst->timerfd_info == NULL || src->timerfd_info == NULL)
    {
        ERROR(0, "copy_peer: dst->timerfd_info or src->timerfd_info is NULL");
        return -1;
    }

    if(dst->timerfd_info->ack_array_pre == NULL || src->timerfd_info->ack_array_pre == NULL)
    {
        ERROR(0, "copy_peer: dst->ack_array_pre or src->ack_array_pre is NULL");
        return -1;
    }

    if(dst->timerfd_info->ack_array_now == NULL || src->timerfd_info->ack_array_now == NULL)
    {
        ERROR(0, "copy_peer: dst->ack_array_now or src->ack_array_now is NULL");
        return -1;
    }

    if(dst->flow_src == NULL || src->flow_src == NULL)
    {
        ERROR(0, "copy_peer: dst->flow_src or src->flow_src is NULL");
        return -1;
    }

    dst->id             = src->id;
    dst->valid          = src->valid;
    dst->discard        = src->discard;
    dst->restricted     = src->restricted;
    dst->dup            = src->dup;
    dst->srtt           = src->srtt;
    dst->total_pkt_cnt  = src->total_pkt_cnt;
    dst->local_seq      = src->local_seq;
    dst->involve_cnt    = src->involve_cnt;
    dst->port           = src->port;
    dst->vip            = src->vip;
    dst->rip            = src->rip;

    memcpy(dst->psk, src->psk, 2*AES_TEXT_LEN);
    memcpy(dst->peeraddr, src->peeraddr, sizeof(struct sockaddr_in));
    
    uint32_t * index_array_tmp;
    index_array_tmp = dst->pkt_index_array_pre;
    dst->pkt_index_array_pre = src->pkt_index_array_pre;
    src->pkt_index_array_pre = index_array_tmp;
    index_array_tmp = dst->pkt_index_array_now;
    dst->pkt_index_array_now = src->pkt_index_array_now;
    src->pkt_index_array_now = index_array_tmp;

    dst->timerfd_info->time_pre = src->timerfd_info->time_pre;
    dst->timerfd_info->time_now = src->timerfd_info->time_now;

    struct ack_info_t * timerfd_tmp;
    timerfd_tmp = dst->timerfd_info->ack_array_pre;
    dst->timerfd_info->ack_array_pre = src->timerfd_info->ack_array_pre;
    src->timerfd_info->ack_array_pre = timerfd_tmp;
    timerfd_tmp = dst->timerfd_info->ack_array_now;
    dst->timerfd_info->ack_array_now = src->timerfd_info->ack_array_now;
    src->timerfd_info->ack_array_now = timerfd_tmp;
    //FD in src must be changed to NUL, to avoid close in accident.
    close_all_timerfd(src->timerfd_info->ack_array_pre, (SEQ_LEVEL_1+1));
    close_all_timerfd(src->timerfd_info->ack_array_now, (SEQ_LEVEL_1+1));

    dst->flow_src->time_pre    = src->flow_src->time_pre;
    dst->flow_src->time_now    = src->flow_src->time_now;
    dst->flow_src->dup_cnt     = src->flow_src->dup_cnt;
    dst->flow_src->delay_cnt   = src->flow_src->delay_cnt;
    dst->flow_src->replay_cnt  = src->flow_src->replay_cnt;
    dst->flow_src->jump_cnt    = src->flow_src->jump_cnt;
    dst->flow_src->time_min    = src->flow_src->time_min;
    dst->flow_src->time_max    = src->flow_src->time_max;

    bit_array_copy(dst->flow_src->ba_pre, src->flow_src->ba_pre);
    bit_array_copy(dst->flow_src->ba_now, src->flow_src->ba_now);

    return 0;
}

struct peer_profile_t** init_peer_table(FILE *secrets_file, int max_id)
{
    if(NULL == secrets_file)
        return NULL;

    struct peer_profile_t ** peer_table = (struct peer_profile_t **)malloc((max_id+1) * sizeof(struct peer_profile_t*));
    if(peer_table == NULL)
    {
        ERROR(errno, "init_peer_table: malloc failed");
        return NULL;
    }
    else
        bzero(peer_table, (max_id+1) * sizeof(struct peer_profile_t*));

    if(update_peer_table(peer_table, secrets_file, max_id) < 0)
    {
        ERROR(0, "init_peer_table: update_peer_table failed");
        destroy_peer_table(peer_table, max_id); 
        return NULL;
    }

    return peer_table;
}

//there should be lock handle, such as global_stat_spin, but I just don't want to implement it now.
int update_peer_table(struct peer_profile_t** peer_table, FILE *secrets_file, int max_id)
{
    if(NULL == peer_table || NULL == secrets_file)
    {
        ERROR(0, "update_peer_table: peer_table or secrets_file is NULL");
        return -1;
    }
    
    int i;
    for(i = 0; i < max_id+1; i++)
        if(peer_table[i] != NULL)
            peer_table[i]->discard = true;

    size_t len = 1024;
    char *line = (char *)malloc(len);
    if(line == NULL)
    {
        ERROR(errno, "update_peer_table: malloc failed");
        return -1;
    }
    else
        bzero(line, len);

    while(-1 != getline(&line, &len, secrets_file))  //why line is an array of char*, not a char* ?
    {
        int id = 0;
        char *id_str = NULL;
        char *psk_str = NULL;
        char *ip_name_str = NULL;
        char *ip6_str = NULL;
        char *port_str = NULL;

        if(shrink_line(line) <= 1)
            continue;
        id_str = strtok(line, " ");
        psk_str = strtok(NULL, " ");
        ip_name_str = strtok(NULL, " ");
        ip6_str = strtok(NULL, " ");
        port_str = strtok(NULL, " ");

        if(NULL == id_str)
            continue;
        if(NULL == psk_str)
        {
            WARNING("PSK of ID %s not found, ignore this peer!", id_str);
            continue;
        }
        id = inet_ptons(id_str);
        if(0 == id || id > max_id)
        {
            WARNING("The ID of %s may be wrong, ignore this peer!", id_str);
            continue;
        }
        
        struct peer_profile_t * tmp_peer = add_peer();
        if(tmp_peer == NULL)
        {
            ERROR(errno, "update_peer_table: add_peer failed.");
            return -1;
        }
        if(peer_table[id] != NULL)
            if(copy_peer(tmp_peer, peer_table[id]) < 0)
                ERROR(0, "Copy the ID of %s failed.", id_str);

        tmp_peer->id = id;
        tmp_peer->discard = false;

        if(strlen(psk_str) > 2*AES_TEXT_LEN)
            WARNING("PSK of ID %s is longer than %d, ignore some bytes.", id_str, 2*AES_TEXT_LEN);
        strncpy((char*)tmp_peer->psk, psk_str, 2*AES_TEXT_LEN);

        if(port_str != NULL) //port_str must be parsed before ip, because servaddr.sin_port uses it.
        {
            int port = atoi(port_str);
            if(port < 1)
                WARNING("Invalid PORT of peer: %s, ingore it's port value!", id_str);
            tmp_peer->port = port;
        }

        if(ip_name_str != NULL && strcmp(ip_name_str, "none") != 0)
        {
            char ip_str[IPV4_LEN] = "\0";
            if(hostname_to_ip(ip_name_str, ip_str) < 0)
                WARNING("Invalid host of peer: %s, %s nslookup failed, ingore it's IP/Port value!", id_str, ip_name_str);
            else
            {
                inet_pton(AF_INET, ip_str, &(tmp_peer->peeraddr->sin_addr));
                tmp_peer->peeraddr->sin_family = AF_INET;
                tmp_peer->peeraddr->sin_port = htons(tmp_peer->port);
                tmp_peer->restricted = true;
            }
        }

        if(ip6_str != NULL && strcmp(ip6_str, "none") != 0)
            WARNING("IPv6 not supported now, ignore it!");

        tmp_peer->vip = htonl(id); //0.0.x.x in network byte order, used inside tunnel.
        //tmp_peer->rip = (global_tunif.addr & global_tunif.mask) | htonl(id); //in network byte order.

        if(peer_table[id] != NULL &&
            tmp_peer->peeraddr->sin_addr.s_addr == peer_table[id]->peeraddr->sin_addr.s_addr &&
            tmp_peer->peeraddr->sin_port == peer_table[id]->peeraddr->sin_port &&
            strncmp((char *)(tmp_peer->psk), (char *)(peer_table[id]->psk), 2*AES_TEXT_LEN) == 0)
        {
            //the peer does not change
            peer_table[id]->discard = false;
            delete_peer(tmp_peer);
            tmp_peer = NULL;
            continue;
        }

        if(peer_table[id] != NULL)
        {
            INFO("update the ID of %s.", id_str);
            if(copy_peer(peer_table[id], tmp_peer) < 0)
                ERROR(0, "Update the ID of %s failed", id_str);
            delete_peer(tmp_peer);
            tmp_peer = NULL;
        }
        else
        {
            INFO("Add the ID of %s.", id_str);
            peer_table[id] = tmp_peer;
        }
    }

    free(line);

    for(i = 0; i < max_id+1; i++)
        if(peer_table[i] != NULL)
            if(peer_table[i]->discard)
            {
                WARNING("Delete the ID of %d.%d.", i/256, i%256);
                delete_peer(peer_table[i]);
                peer_table[i] = NULL;
            }

    return 0;
}

int shrink_line(char *line)
{
    int n = strlen(line);
    int i;
    for(i=0; i<n; i++)
        if(isspace(line[i]))
            line[i] = ' ';
        else if('#' == line[i])
            for( ; i<n; i++)
                line[i] = '\0';
    return strlen(line);
}


int destroy_peer_table(struct peer_profile_t **peer_table, int max_id)
{
    if(NULL == peer_table)
        return 0;

    int peer_num = max_id+1;
    
    int i;
    for(i = 0; i < peer_num; i++)
    {
        delete_peer(peer_table[i]);
        peer_table[i] = NULL;
    }

    free(peer_table);
    return 0;
}
