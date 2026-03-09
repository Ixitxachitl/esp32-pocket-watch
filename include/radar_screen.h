#pragma once
/*
 *  radar_screen.h – BLE + WiFi proximity radar display
 *
 *  Shows nearby BLE devices and WiFi APs as coloured blips on a
 *  green radar grid.  RSSI maps to distance from centre (stronger
 *  signal = closer to centre).  No sweep animation – static grid
 *  with nodes that update on each scan cycle.
 */

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum tracked devices */
#define RADAR_MAX_NODES  32

/* Node type */
typedef enum {
    RADAR_NODE_BLE  = 0,
    RADAR_NODE_WIFI = 1,
} radar_node_type_t;

/* A single radar node (one detected device / AP) */
typedef struct {
    radar_node_type_t type;
    int8_t   rssi;              /* dBm, e.g. -30 to -100        */
    char     name[24];          /* short label (truncated)       */
    uint32_t hash;              /* hash of MAC – used for angle  */
    bool     active;            /* seen in most recent scan?     */
} radar_node_t;

/* Create radar UI on the given tile parent. */
void radar_screen_create(lv_obj_t *parent, int diameter);

/* Replace the full node list (call after each scan cycle).
   The screen makes its own copy; caller may free / reuse the array. */
void radar_screen_update(const radar_node_t *nodes, int count);

#ifdef __cplusplus
}
#endif
