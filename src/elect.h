/*
 * Copyright (c) 2017 HAW Hamburg
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    vslab-riot
 * @brief       VS practical exercise with RIOT-OS
 * @{
 *
 * @file
 * @brief       Leader Election
 *
 * @author      Sebastian Meiling <s@mlng.net>
 */

#ifndef ELECT_H
#define ELECT_H

#include <stdbool.h>

#include "net/ipv6/addr.h"
#include "xtimer.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ELECT_NODES_NUM
/**
 * @brief Number of nodes in the system, needed for static memory allocation
 */
#define ELECT_NODES_NUM         (8)
#endif

/**
 * @name Parameters for the election algorithm
 * @{
 */
#define ELECT_MSG_INTERVAL      (2U * MS_PER_SEC)           /**< periodic election interval in ms */
#define ELECT_LEADER_THRESHOLD  (5U * ELECT_MSG_INTERVAL)   /**< interval after which a leader is identified */
#define ELECT_LEADER_TIMEOUT    (7U * ELECT_MSG_INTERVAL)   /**< timeout after which a leader is dead */
/** @} */

/**
 * @brief Weight for exponentially weighted moving average
 */
#define ELECT_WEIGHT            (16)

/**
 * @name Broadcast configuration for IDs
 * @{
 */
#define ELECT_BC_NODEID_ADDR    IPV6_ADDR_ALL_NODES_LINK_LOCAL
#define ELECT_BC_NODEID_PORT    (2409U)
#define ELECT_BC_NODEID_WAIT    (5000U)
/** @} */

/**
 * @name Broadcast configuration for sensor values
 * @{
 */
#define ELECT_BC_SENSOR_ADDR    {{  0xff, 0x02, 0x00, 0x00, \
                                    0x00, 0x00, 0x00, 0x00, \
                                    0x00, 0x00, 0x00, 0x00, \
                                    0x00, 0x00, 0x20, 0x17 }}
#define ELECT_BC_SENSOR_PORT    (2410U)
#define ELECT_BC_SENSOR_LEN     (8U)
/** @} */

/**
 * @name IPC message types for events
 * @{
 */
#define ELECT_BROADCAST_EVENT           (0x0815)
#define ELECT_INTERVAL_EVENT            (0x0816)
#define ELECT_LEADER_ALIVE_EVENT        (0x0817)
#define ELECT_LEADER_THRESHOLD_EVENT    (0x0818)
#define ELECT_LEADER_TIMEOUT_EVENT      (0x0819)
#define ELECT_NODES_EVENT               (0x0820)
#define ELECT_SENSOR_EVENT              (0x0821)
#define ELECT_NEW_SENSOR_VALUE_EVENT    (0x0822) //new event to start GET for client sensor values

/** @} */

/**
 * @brief Init CoAP handlers
 *
 * @param[in] main  process ID of main thread, needed for IPC
 *
 * @returns 0 on success, error otherwise
 */
int coap_init(kernel_pid_t main);

/**
 * @brief Start broadcast listener
 *
 * @returns 0 on success, error otherwise
 */
int listen_init(kernel_pid_t main);

/**
 * @brief Init network interface
 *
 * Set channel, TX power, PAN ID, IP addresses as necessary. Can be empty,
 * if no specific configuration is required.
 *
 * @param[in] main  process ID of main thread, needed for IPC
 *
 * @returns 0 on success, error otherwise
 */
int net_init(kernel_pid_t main);

/**
 * @brief Init sensor
 *
 * @returns 0 on success, error otherwise
 */
int sensor_init(void);

/**
 * @brief Get temperature sensor value
 *
 * @returns Temperature value as degree Celsius x100
 */
int16_t sensor_read(void);

/**
 * @brief Send IP address via IPv6 multicast to `ff02::1`
 *
 * @param[in] ip    IP address
 *
 * @returns 0 on success, or error otherwise
 */
int broadcast_id(const ipv6_addr_t *ip);

/**
 * @brief Send value via IPv6 multicast to `ff02::2017`
 *
 * @param[in] value Sensor value
 *
 * @returns 0 on success, or error otherwise
 */
int broadcast_sensor(int16_t value);

/**
 * @brief Send IP address of node to leader node using CoAP PUT
 *
 * @param[in] addr  IP address of leader node
 * @param[in] node  IP address of local node
 *
 * @returns 0 on succes, error otherwise
 */
int coap_put_node(ipv6_addr_t addr, ipv6_addr_t node);

/**
 * @brief Get sensor reading from a node
 *
 * @param[in] addr  IP address of node
 *
 * @returns 0 on success, error otherwise
 */
int coap_get_sensor(ipv6_addr_t addr);

/**
 * @brief Get link local IP address as string of this node
 *
 * @param[out] addr     Node link local IP address as binary,
 *                      IPV6_ADDR_UNSPECIFIED on error
 */
void get_node_ip_addr(ipv6_addr_t *addr);

/**
 * @brief Compare two IP addresses
 *
 * @param[in] ip1   First IP address
 * @param[in] ip2   Second IP address
 *
 * @returns     <0, if ip1 < ip2
 * @returns      0, if ip1 == ip2
 * @returns     >0, if ip1 > ip2
 */
int ipv6_addr_cmp(const ipv6_addr_t *ip1, const ipv6_addr_t *ip2);

#ifdef __cplusplus
}
#endif

#endif /* ELECT_H */
/** @} */
