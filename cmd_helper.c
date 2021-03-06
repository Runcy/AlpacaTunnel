#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net/if.h>

#include "cmd_helper.h"
#include "log.h"
#include "ip.h"

static int iptables_nat_set = 0;
static int iptables_tcpmss_set = 0;

int run_cmd_list(ll_node_t ** cmd_list)
{
    char *cmd;
    int rc;
    int flag = 0;
    while( (cmd = shift_ll(cmd_list) ) != NULL)
    {
        rc = system(cmd);
        if(rc != 0)
        {
            flag = -1;
            WARNING("run cmd %s failed.", cmd);
        }
    }

    return flag;
}


int enable_ip_forward()
{
    char cmd[] = "sysctl net.ipv4.ip_forward=1 > /dev/null";
    return system(cmd);
}


// Caller must free the output latter.
int get_popen(const char * cmd, char ** output)
{
    // Open the command for reading.
    FILE * fp = popen(cmd, "r");
    if(fp == NULL)
    {
        ERROR(0, "Failed to run command %s", cmd);
        return -1;
    }
    
    int block_size = 1024;

    *output = malloc(block_size);
    if(*output == NULL)
    {
        ERROR(errno, "malloc failed");
        return -1;
    }
    (*output)[0] = '\0';

    char * buffer = malloc(block_size);
    if(buffer == NULL)
    {
        ERROR(errno, "malloc failed");
        free(*output);
        return -1;
    }
    
    int total_size = block_size;

    // Read the output a line at a time.
    while(fgets(buffer, block_size-1, fp) != NULL)
    {
        int len_buffer = strlen(buffer);
        int len_occupied = strlen(*output);
        if(len_buffer >= total_size - len_occupied)
        {
            total_size *= 2;
            char * tmp = realloc(*output, total_size);
            if(tmp == NULL)
            {
                ERROR(errno, "realloc failed");
                free(*output);
                free(buffer);
                return -1;
            }
            *output = tmp;
        }
        strcat(*output, buffer);
    }
    
    pclose(fp);
    free(buffer);

    return 0;
}


int get_default_route(char default_gw_ip[], char default_gw_dev[])
{
    char cmd[] = "ip route show 0/0";  // default via 10.0.2.2 dev enp0s3
    char * route;
    
    if(get_popen(cmd, &route) == 0)
    {
        char delim[] = "\t ";
        char * c;
        c = strtok(route, delim);
        c = strtok(NULL, delim);
        c = strtok(NULL, delim);
        if(c)
            strncpy(default_gw_ip, c, 17);
        else
            return -1;

        c = strtok(NULL, delim);
        c = strtok(NULL, delim);
        if(c && default_gw_dev)
            strncpy(default_gw_dev, c, IFNAMSIZ);
        else
            return -1;
    }

    return 0;
}


int change_route(const char * dst, const char * gw_ip, const char * table, int action)
{
    if(dst == NULL)
        return -1;

    if(action == 0 && gw_ip == NULL)
        return -1;

    int rc = 0;
    char * cmd = (char *)malloc(100);
    if(cmd == NULL)
    {
        ERROR(errno, "malloc failed");
        return -1;
    }

    if(action == 0)
        if(table)
            sprintf(cmd, "ip route add %s via %s table %s", dst, gw_ip, table);
        else
            sprintf(cmd, "ip route add %s via %s", dst, gw_ip);
    else
        if(table)
            sprintf(cmd, "ip route del %s table %s", dst, table);
        else
            sprintf(cmd, "ip route del %s", dst);

    if(system(cmd) != 0)
    {
        WARNING("change route for %s failed.", dst);
        rc = -1;
    }

    free(cmd);

    return rc;
}

int add_iproute(const char * dst, const char * gw_ip, const char * table)
{
    return change_route(dst, gw_ip, table, 0);
}

int del_iproute(const char * dst, const char * table)
{
    return change_route(dst, NULL, table, 1);
}


int change_default_route(const char gw_ip[])
{
    if(gw_ip == NULL || gw_ip[0] == '\0')
        return -1;

    int rc = 0;
    
    if(add_iproute("default", gw_ip, "default") == 0)
    {
        if(del_iproute("default", "main") != 0)
            rc = -1;
    }
    else
    {
        WARNING("add default route to table default failed.");
        rc = -1;
    }

    return rc;
}


int restore_default_route(const char gw_ip[])
{
    if(gw_ip == NULL || gw_ip[0] == '\0')
        return -1;

    int rc = 0;
    
    if(add_iproute("default", gw_ip, "main") != 0)
    {
        WARNING("resrote default route failed.");
        rc = -1;
    }

    if(del_iproute("default", "default") != 0)
        rc = -1;

    return rc;
}


int change_iptables_nat(const char * source, int action)
{
    if(source == NULL)
        return -1;

    int rc = 0;
    char * cmd = (char *)malloc(100 + IP_LEN);
    if(cmd == NULL)
    {
        ERROR(errno, "malloc failed");
        return -1;
    }

    if(action == 0)
        sprintf(cmd, "iptables -t nat -A POSTROUTING -s %s -j MASQUERADE", source);
    else
        sprintf(cmd, "iptables -t nat -D POSTROUTING -s %s -j MASQUERADE", source);

    if(system(cmd) != 0)
    {
        WARNING("change iptables nat rule failed.");
        rc = -1;
    }

    free(cmd);

    return rc;
}

int add_iptables_nat(const char * source)
{
    if(change_iptables_nat(source, 0) == 0)
    {
        iptables_nat_set = 1;
        return 0;
    }
    else
        return -1;
}

int del_iptables_nat(const char * source)
{
    if(iptables_nat_set == 1)
        return change_iptables_nat(source, 1);
    else
        return 0;
}


int change_iptables_tcpmss(int mss, int action)
{
    if(mss < TCPMSS_MIN || mss > TCPMSS_MAX)
        return -1;

    int rc = 0;
    char * cmd = (char *)malloc(100);
    if(cmd == NULL)
    {
        ERROR(errno, "malloc failed");
        return -1;
    }

    if(action == 0)
        sprintf(cmd, "iptables -t mangle -A POSTROUTING -p tcp --tcp-flags SYN,RST SYN -j TCPMSS --set-mss %d", mss);
    else
        sprintf(cmd, "iptables -t mangle -D POSTROUTING -p tcp --tcp-flags SYN,RST SYN -j TCPMSS --set-mss %d", mss);

    if(system(cmd) != 0)
    {
        WARNING("change iptables tcpmss rule failed.");
        rc = -1;
    }

    free(cmd);

    return rc;
}

int add_iptables_tcpmss(int mss)
{
    if(change_iptables_tcpmss(mss, 0) == 0)
    {
        iptables_tcpmss_set = 1;
        return 0;
    }
    else
        return -1;
}

int del_iptables_tcpmss(int mss)
{
    if(iptables_tcpmss_set == 1)
        return change_iptables_tcpmss(mss, 1);
    else
        return 0;
}


