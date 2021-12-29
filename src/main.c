/*
 * Copyright (c) 2017 HAW Hamburg
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     vslab-riot
 * @{
 *
 * @file
 * @brief       Leader Election Application
 *
 * @author      Sebastian Meiling <s@mlng.net>
 *
 * @}
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "log.h"

#include "net/gcoap.h"
#include "kernel_types.h"
#include "random.h"

#include "msg.h"
#include "evtimer_msg.h"
#include "xtimer.h"

#include "elect.h"

static msg_t _main_msg_queue[ELECT_NODES_NUM];

/**
 * @name list of clients
 */
ipv6_addr_t clients[ELECT_NODES_NUM];
int clientCount = 0;

/**
 * @name storage for received sensor values
 */
int16_t sensor_values[ELECT_NODES_NUM];
int sensor_val_received = 0;

/**
 * @name event time configuration
 * @{
 */
static evtimer_msg_t evtimer;
static evtimer_msg_event_t interval_event = {
    .event  = { .offset = ELECT_MSG_INTERVAL },
    .msg    = { .type = ELECT_INTERVAL_EVENT}};
static evtimer_msg_event_t leader_timeout_event = {
    .event  = { .offset = ELECT_LEADER_TIMEOUT },
    .msg    = { .type = ELECT_LEADER_TIMEOUT_EVENT}};
static evtimer_msg_event_t leader_threshold_event = {
    .event  = { .offset = ELECT_LEADER_THRESHOLD },
    .msg    = { .type = ELECT_LEADER_THRESHOLD_EVENT}};
//new event to start GET for client sensor values
static evtimer_msg_event_t new_sensor_value_event = {
        .event = { .offset = ELECT_MSG_INTERVAL},
        .msg   = { .type = ELECT_NEW_SENSOR_VALUE_EVENT}};
/** @} */

static int is_already_saved(char *ip_str) {
    int result = 0;

    /*converts string to ipv6_addr_t */
    ipv6_addr_t ip;
    ipv6_addr_from_str(&ip, ip_str);

    for (int i = 0; i < clientCount; i++) {
        if (ipv6_addr_cmp(&ip, &clients[i]) == 0) {
            LOG_DEBUG("Client IP already saved. Skipping...");
            result = 1;
            break;
        }
    }
    return result;
}

/**
 * @brief   Initialise network, coap, and sensor functions
 *
 * @note    This function should be called first to init the system!
 */
int setup(void)
{
    LOG_DEBUG("%s: begin\n", __func__);
    /* avoid unused variable error */
    (void) interval_event;
    (void) leader_timeout_event;
    (void) leader_threshold_event;

    msg_init_queue(_main_msg_queue, ELECT_NODES_NUM);
    kernel_pid_t main_pid = thread_getpid();

    if (net_init(main_pid) != 0) {
        LOG_ERROR("init network interface!\n");
        return 2;
    }
    if (coap_init(main_pid) != 0) {
        LOG_ERROR("init CoAP!\n");
        return 3;
    }
    if (sensor_init() != 0) {
        LOG_ERROR("init sensor!\n");
        return 4;
    }
    if (listen_init(main_pid) != 0) {
        LOG_ERROR("init listen!\n");
        return 5;
    }
    LOG_DEBUG("%s: done\n", __func__);
    evtimer_init_msg(&evtimer);
    /* send initial `TICK` to start eventloop */
    msg_send(&interval_event.msg, main_pid);
    return 0;
}


int main(void)
{
    /* this should be first */
    if (setup() != 0) {
        return 1;
    }

    kernel_pid_t main_pid = thread_getpid();
    ipv6_addr_t biggestIP;
    ipv6_addr_t myIP;
    ipv6_addr_t otherNode;
    get_node_ip_addr(&myIP);

    int isLeaderThresholdSet = 0;
    int isLeaderFound = 0;
    int isBiggestIP = 1; /*assume us as biggest at the start */
    int isClient = 0;
    int isLeader = 0;

    int16_t sensor_mean = sensor_read();

    (void)clients;
    (void)main_pid;
    (void)isClient;
    (void)isLeader;

    while(true) {
        msg_t m;
        msg_receive(&m);
        switch (m.type) {
        case ELECT_INTERVAL_EVENT:
            LOG_DEBUG("+ interval event.\n");

            /**
             * if leader is not found, send broadcast every ELECT_MSG_INTERVAL
             * ->staert timer leader_threshold event
             */
             if(isBiggestIP && !isLeaderFound) {
                 broadcast_id(&myIP);

                 /*offset needs to be reset before reusing the timer*/
                 interval_event.event.offset = ELECT_MSG_INTERVAL;
                 evtimer_add_msg(&evtimer, &interval_event, main_pid);
             }
             if (isLeaderThresholdSet == 0) {
                 isLeaderThresholdSet = 1;
                 leader_threshold_event.event.offset = ELECT_LEADER_THRESHOLD;
                 evtimer_add_msg(&evtimer, &leader_threshold_event, main_pid);
             }
            break;
        case ELECT_BROADCAST_EVENT:
            LOG_DEBUG("+ broadcast event, from [%s]", (char *)m.content.ptr);
            /**
             * triggered everytime the node gets a broadcast IP address
             *
             * delete saved leader and clients
             * compare ip addresses
             * if localIP < receivedIP
             * ->stop broadcast
             */
             /*only leader can restart broadcast for all other nodes in the network*/
             if (isLeaderFound) {
                 LOG_DEBUG("Got new broadcast altough leader is found already. Restarting broadcast from this node!\n");
                 //reset all flags
                 isLeaderThresholdSet = 0;
                 isLeaderFound = 0;
                 //isBiggestIP = 1;    //not sure if this is needed in this event
                 memcpy(&biggestIP, &myIP, sizeof(ipv6_addr_t));
                 memset(&clients, 0, sizeof(clients));
                 clientCount = 0;

                 evtimer_del(&evtimer, &leader_timeout_event.event);

                 interval_event.event.offset = ELECT_MSG_INTERVAL;
                 evtimer_add_msg(&evtimer, &interval_event, main_pid);
                 break;
             }
             ipv6_addr_from_str(&otherNode, (char *)m.content.ptr);
             int result = ipv6_addr_cmp(&myIP, &otherNode);
             if (result < 0) {
                 evtimer_del(&evtimer, &interval_event.event);
                 memcpy(&biggestIP, &otherNode, sizeof(ipv6_addr_t));
                 isBiggestIP = 0;
             }
            break;
        case ELECT_LEADER_ALIVE_EVENT:
            LOG_DEBUG("+ leader event.\n");
            /**
             * client gets request from leader for sensor data
             * 0 -> sensor values already sent
             * 1 -> client reset timeout
             */
            /**
             * Here the sensor value is already sent, refer to _sensor_handler
             * in coap.c. The call to gcoap_finish() sends response to GET
             * request with value returned by sensor_read().
             *
             * We only need to cancel old leader_timeout timer and start a
             * new one.
             */
            evtimer_del(&evtimer, &leader_timeout_event.event);
            leader_timeout_event.event.offset = ELECT_LEADER_TIMEOUT;
            evtimer_add_msg(&evtimer, &leader_timeout_event, main_pid);
            break;
        case ELECT_LEADER_TIMEOUT_EVENT:
            LOG_DEBUG("+ leader timeout event.\n");
            /**
             * reset everything and go to phase 1
             */
            isLeaderThresholdSet = 0;
            isLeaderFound = 0;
            isBiggestIP = 1;
            memcpy(&biggestIP, &myIP, sizeof(ipv6_addr_t));
            //reset client list
            memset(&clients, 0, sizeof(clients));
            clientCount = 0;
            //reset sensor values
            memset(&sensor_values, 0, sizeof(sensor_values));
            sensor_val_received = 0;

            //interval_event.event.offset = ELECT_MSG_INTERVAL;
            //evtimer_add_msg(&evtimer, &interval_event, main_pid);

            LOG_DEBUG("LEADER TIMES OUT. Restarting to phase 1.\n");
            msg_send(&interval_event.msg, main_pid);
            break;
        case ELECT_NEW_SENSOR_VALUE_EVENT:
            LOG_DEBUG("Time to get new sensor values\n");
            /**
             * the leader gets this event when there is no new client registered
             * for ELECT_MGS_INTERVAL from last registration
             * leader has to send a get to all clients to ask for new sensor values
             */
             if (isClient && !isLeader) {
                 LOG_WARNING("I' a client and should not get this msg! break!\n");
                 break;
             }
             //reset value count and vlaues
             sensor_val_received = 0;
             for (int i = 0; i < ELECT_NODES_NUM; i++) {
                 sensor_values[i] = 0;
             }
             for (int i = 0; i < clientCount; i++) {
                 char c[IPV6_ADDR_MAX_STR_LEN];
                 ipv6_addr_to_str(c, &clients[i], IPV6_ADDR_MAX_STR_LEN);
                 LOG_DEBUG("Sending GET to receive sensor values to %s\n", c);
                 coap_get_sensor(clients[i]);
             }
             break;
        case ELECT_NODES_EVENT:
            LOG_DEBUG("+ nodes event, from [%s].\n", (char *)m.content.ptr);
            /**
             * Coordinator event
             * 1. save IP Address from client
             * 2. if leader/coordinator gets all ip addresses from clients, request sensor values
             */
            if(isClient && !isLeader) {
                LOG_WARNING("I' a client and should not get this msg! break!\n");
                break;
            }
            if (clientCount < ELECT_NODES_NUM && !is_already_saved((char *)m.content.ptr)) {
                ipv6_addr_from_str(&clients[clientCount++], (char *)m.content.ptr);
            }

            /**
             * start a timer with ELECT_MSG_INTERVAL
             * when timer times out assume that all clients are already registered
             *
             * this triggers first get to all clients for sensor values
             * next trigger should come from ELECT_SENSOR_EVENT
             */
             LOG_DEBUG("Starting new timer for ELECT_MSG_INTERVAL, if timeout: assume that all clientsare registered}\n");
             evtimer_del(&evtimer, &new_sensor_value_event.event);
             new_sensor_value_event.event.offset = ELECT_MSG_INTERVAL;
             evtimer_add_msg(&evtimer, &new_sensor_value_event, main_pid);

             evtimer_print(&evtimer);
            break;
        case ELECT_SENSOR_EVENT:
            LOG_DEBUG("+ sensor event, value=%s\n",  (char *)m.content.ptr);
            /**
             * leader gets this event if client has sent values
             *
             * 1. calc if there are new values
             * 2. send average value through multicast to all clients with address ff02::2017
             * 3. pause for ELECT_MSG_INTERVAL, request sensor values again
             */
             if (!isLeader) {
                 LOG_WARNING("I' a client and should not get this msg! break!\n");
                 break;
             }
             //if value received before adding new one -> ERROR!
             if (sensor_val_received == ELECT_NODES_NUM) {
                 LOG_WARNING("Received more seniore values than expected, HELP ME CANT HANDLE THIS\n");
                 break;
             }
             //get sensor value from msg content
             int16_t val = (int16_t)strtol((char *)m.content.ptr, NULL, 10);
             memcpy(&sensor_values[sensor_val_received++], &val, sizeof(int16_t));

             LOG_DEBUG("old sensr mean: %d\n", sensor_mean);
            sensor_mean = (int16_t) ((15.0/16.0) * (double) sensor_mean) + ((1.0/16.0) * (double)(val));
            LOG_DEBUG("new sesnor mean: %d\n", sensor_mean);

            //set timer to ask for next round of sensor values
            LOG_DEBUG("Broadcasted sensor avg %d. Start timer for next sensor GET\n", sensor_mean);
            evtimer_del(&evtimer, &new_sensor_value_event.event);
            new_sensor_value_event.event.offset = ELECT_MSG_INTERVAL;
            evtimer_add_msg(&evtimer, &new_sensor_value_event, main_pid);
            break;
        case ELECT_LEADER_THRESHOLD_EVENT:
            LOG_DEBUG("+ leader threshold event.\n");
            /**
             * 1. if (count received IP == 1)
             * 1.1 get biggest ip
             * 1.2 if own ip is biggest ip, set as leader
             * 1.3. else set as client and register to leader
             * 2. reset timer , count = 0
             */
             //check if leader is found
             //TODO: implement check if leader is found
             if (ipv6_addr_cmp(&myIP, &otherNode) != 0) {
                 LOG_DEBUG("Leader found! Stop broadcasting\n");
                 isLeaderFound = 1;
                 isLeaderThresholdSet = 1;

                 if (isBiggestIP) {
                     LOG_DEBUG("I have the biggest IP! Set myself as leader!\n");
                     isLeader = 1;

                     //stop broadcasting
                     evtimer_del(&evtimer, &interval_event.event);

                     //adds own address to client list
                     LOG_DEBUG("Waiting for clients to send IPs\n");
                     memcpy(&clients[clientCount++], &myIP, sizeof(ipv6_addr_t));
                     //coap_put_node(biggestIP, myIP); //cannot use this because of gcoap_timeout when sending to itself
                     LOG_DEBUG("If no other clients, use own value\n");
                     evtimer_add_msg(&evtimer, &new_sensor_value_event, main_pid);
                 } else {
                     LOG_DEBUG("I don't have the biggest IP. Set myself as client!\n");
                     isClient = 1;

                     //adds ip to client list
                     coap_put_node(biggestIP, myIP);
                 }
             } else {
                 LOG_DEBUG("Leader still not found! Continue broadcasting/listening\n");
                 isLeaderThresholdSet = 0;
             }
            break;
        default:
            LOG_WARNING("??? invalid event (%x) ???\n", m.type);
            break;
        }
        /* !!! DO NOT REMOVE !!! */
        if ((m.type != ELECT_INTERVAL_EVENT) &&
            (m.type != ELECT_LEADER_TIMEOUT_EVENT) &&
            (m.type != ELECT_LEADER_THRESHOLD_EVENT) &&
            (m.type != ELECT_NEW_SENSOR_VALUE_EVENT)) {
            msg_reply(&m, &m);
        }
    }
    /* should never be reached */
    return 0;
}

