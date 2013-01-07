/*
 * Virtio Network Device
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef _QEMU_VIRTIO_NET_H
#define _QEMU_VIRTIO_NET_H

#include "virtio.h"
#include "pci/pci.h"

#define TYPE_VIRTIO_NET "virtio-net"
#define VIRTIO_NET(obj) \
        OBJECT_CHECK(VirtIONet, (obj), TYPE_VIRTIO_NET)

#define ETH_ALEN    6

/* from Linux's virtio_net.h */

/* The ID for virtio_net */
#define VIRTIO_ID_NET   1

/* The feature bitmap for virtio net */
#define VIRTIO_NET_F_CSUM       0       /* Host handles pkts w/ partial csum */
#define VIRTIO_NET_F_GUEST_CSUM 1       /* Guest handles pkts w/ partial csum */
#define VIRTIO_NET_F_MAC        5       /* Host has given MAC address. */
#define VIRTIO_NET_F_GSO        6       /* Host handles pkts w/ any GSO type */
#define VIRTIO_NET_F_GUEST_TSO4 7       /* Guest can handle TSOv4 in. */
#define VIRTIO_NET_F_GUEST_TSO6 8       /* Guest can handle TSOv6 in. */
#define VIRTIO_NET_F_GUEST_ECN  9       /* Guest can handle TSO[6] w/ ECN in. */
#define VIRTIO_NET_F_GUEST_UFO  10      /* Guest can handle UFO in. */
#define VIRTIO_NET_F_HOST_TSO4  11      /* Host can handle TSOv4 in. */
#define VIRTIO_NET_F_HOST_TSO6  12      /* Host can handle TSOv6 in. */
#define VIRTIO_NET_F_HOST_ECN   13      /* Host can handle TSO[6] w/ ECN in. */
#define VIRTIO_NET_F_HOST_UFO   14      /* Host can handle UFO in. */
#define VIRTIO_NET_F_MRG_RXBUF  15      /* Host can merge receive buffers. */
#define VIRTIO_NET_F_STATUS     16      /* virtio_net_config.status available */
#define VIRTIO_NET_F_CTRL_VQ    17      /* Control channel available */
#define VIRTIO_NET_F_CTRL_RX    18      /* Control channel RX mode support */
#define VIRTIO_NET_F_CTRL_VLAN  19      /* Control channel VLAN filtering */
#define VIRTIO_NET_F_CTRL_RX_EXTRA 20   /* Extra RX mode control support */

#define VIRTIO_NET_S_LINK_UP    1       /* Link is up */

#define TX_TIMER_INTERVAL 150000 /* 150 us */

/* Limit the number of packets that can be sent via a single flush
 * of the TX queue.  This gives us a guaranteed exit condition and
 * ensures fairness in the io path.  256 conveniently matches the
 * length of the TX queue and shows a good balance of performance
 * and latency. */
#define TX_BURST 256

typedef struct virtio_net_conf
{
    uint32_t txtimer;
    int32_t txburst;
    char *tx;
} virtio_net_conf;

/* Maximum packet size we can receive from tap device: header + 64k */
#define VIRTIO_NET_MAX_BUFSIZE (sizeof(struct virtio_net_hdr) + (64 << 10))

struct virtio_net_config
{
    /* The config defining mac address ($ETH_ALEN bytes) */
    uint8_t mac[ETH_ALEN];
    /* See VIRTIO_NET_F_STATUS and VIRTIO_NET_S_* above */
    uint16_t status;
} QEMU_PACKED;

/*
 * Control virtqueue data structures
 *
 * The control virtqueue expects a header in the first sg entry
 * and an ack/status response in the last entry.  Data for the
 * command goes in between.
 */
struct virtio_net_ctrl_hdr {
    uint8_t class;
    uint8_t cmd;
};

typedef uint8_t virtio_net_ctrl_ack;

#define VIRTIO_NET_OK     0
#define VIRTIO_NET_ERR    1

/*
 * Control the RX mode, ie. promisucous, allmulti, etc...
 * All commands require an "out" sg entry containing a 1 byte
 * state value, zero = disable, non-zero = enable.  Commands
 * 0 and 1 are supported with the VIRTIO_NET_F_CTRL_RX feature.
 * Commands 2-5 are added with VIRTIO_NET_F_CTRL_RX_EXTRA.
 */
#define VIRTIO_NET_CTRL_RX_MODE    0
 #define VIRTIO_NET_CTRL_RX_MODE_PROMISC      0
 #define VIRTIO_NET_CTRL_RX_MODE_ALLMULTI     1
 #define VIRTIO_NET_CTRL_RX_MODE_ALLUNI       2
 #define VIRTIO_NET_CTRL_RX_MODE_NOMULTI      3
 #define VIRTIO_NET_CTRL_RX_MODE_NOUNI        4
 #define VIRTIO_NET_CTRL_RX_MODE_NOBCAST      5

/*
 * Control the MAC filter table.
 *
 * The MAC filter table is managed by the hypervisor, the guest should
 * assume the size is infinite.  Filtering should be considered
 * non-perfect, ie. based on hypervisor resources, the guest may
 * received packets from sources not specified in the filter list.
 *
 * In addition to the class/cmd header, the TABLE_SET command requires
 * two out scatterlists.  Each contains a 4 byte count of entries followed
 * by a concatenated byte stream of the ETH_ALEN MAC addresses.  The
 * first sg list contains unicast addresses, the second is for multicast.
 * This functionality is present if the VIRTIO_NET_F_CTRL_RX feature
 * is available.
 */
struct virtio_net_ctrl_mac {
    uint32_t entries;
    uint8_t macs[][ETH_ALEN];
};

typedef struct VirtIONet {
    VirtIODevice parent_obj;
    uint8_t mac[ETH_ALEN];
    uint16_t status;
    VirtQueue *rx_vq;
    VirtQueue *tx_vq;
    VirtQueue *ctrl_vq;
    NICState *nic;
    QEMUTimer *tx_timer;
    QEMUBH *tx_bh;
    uint32_t tx_timeout;
    int32_t tx_burst;
    int tx_waiting;
    uint32_t has_vnet_hdr;
    size_t host_hdr_len;
    size_t guest_hdr_len;
    uint8_t has_ufo;
    struct {
        VirtQueueElement elem;
        ssize_t len;
    } async_tx;
    int mergeable_rx_bufs;
    uint8_t promisc;
    uint8_t allmulti;
    uint8_t alluni;
    uint8_t nomulti;
    uint8_t nouni;
    uint8_t nobcast;
    uint8_t vhost_started;
    struct {
        int in_use;
        int first_multi;
        uint8_t multi_overflow;
        uint8_t uni_overflow;
        uint8_t *macs;
    } mac_table;
    uint32_t *vlans;
    virtio_net_conf net_conf;
    NICConf nic_conf;
    DeviceState *qdev;
} VirtIONet;

#define VIRTIO_NET_CTRL_MAC    1
 #define VIRTIO_NET_CTRL_MAC_TABLE_SET        0

/*
 * Control VLAN filtering
 *
 * The VLAN filter table is controlled via a simple ADD/DEL interface.
 * VLAN IDs not added may be filterd by the hypervisor.  Del is the
 * opposite of add.  Both commands expect an out entry containing a 2
 * byte VLAN ID.  VLAN filterting is available with the
 * VIRTIO_NET_F_CTRL_VLAN feature bit.
 */
#define VIRTIO_NET_CTRL_VLAN       2
 #define VIRTIO_NET_CTRL_VLAN_ADD             0
 #define VIRTIO_NET_CTRL_VLAN_DEL             1

#define DEFINE_VIRTIO_NET_FEATURES(_state, _field) \
        DEFINE_VIRTIO_COMMON_FEATURES(_state, _field), \
        DEFINE_PROP_BIT("csum", _state, _field, VIRTIO_NET_F_CSUM, true), \
        DEFINE_PROP_BIT("guest_csum", _state, _field, VIRTIO_NET_F_GUEST_CSUM, true), \
        DEFINE_PROP_BIT("gso", _state, _field, VIRTIO_NET_F_GSO, true), \
        DEFINE_PROP_BIT("guest_tso4", _state, _field, VIRTIO_NET_F_GUEST_TSO4, true), \
        DEFINE_PROP_BIT("guest_tso6", _state, _field, VIRTIO_NET_F_GUEST_TSO6, true), \
        DEFINE_PROP_BIT("guest_ecn", _state, _field, VIRTIO_NET_F_GUEST_ECN, true), \
        DEFINE_PROP_BIT("guest_ufo", _state, _field, VIRTIO_NET_F_GUEST_UFO, true), \
        DEFINE_PROP_BIT("host_tso4", _state, _field, VIRTIO_NET_F_HOST_TSO4, true), \
        DEFINE_PROP_BIT("host_tso6", _state, _field, VIRTIO_NET_F_HOST_TSO6, true), \
        DEFINE_PROP_BIT("host_ecn", _state, _field, VIRTIO_NET_F_HOST_ECN, true), \
        DEFINE_PROP_BIT("host_ufo", _state, _field, VIRTIO_NET_F_HOST_UFO, true), \
        DEFINE_PROP_BIT("mrg_rxbuf", _state, _field, VIRTIO_NET_F_MRG_RXBUF, true), \
        DEFINE_PROP_BIT("status", _state, _field, VIRTIO_NET_F_STATUS, true), \
        DEFINE_PROP_BIT("ctrl_vq", _state, _field, VIRTIO_NET_F_CTRL_VQ, true), \
        DEFINE_PROP_BIT("ctrl_rx", _state, _field, VIRTIO_NET_F_CTRL_RX, true), \
        DEFINE_PROP_BIT("ctrl_vlan", _state, _field, VIRTIO_NET_F_CTRL_VLAN, true), \
        DEFINE_PROP_BIT("ctrl_rx_extra", _state, _field, VIRTIO_NET_F_CTRL_RX_EXTRA, true)

#define DEFINE_VIRTIO_NET_PROPERTIES(_state)                        \
        DEFINE_NIC_PROPERTIES(_state, nic_conf),                    \
        DEFINE_PROP_UINT32("x-txtimer", _state, net_conf.txtimer,   \
                           TX_TIMER_INTERVAL),                      \
        DEFINE_PROP_INT32("x-txburst", _state, net_conf.txburst,    \
                          TX_BURST),                                \
        DEFINE_PROP_STRING("tx", _state, net_conf.tx)

void virtio_net_set_conf(DeviceState *dev, virtio_net_conf *net_conf,
                         NICConf *nic_conf);

#endif
