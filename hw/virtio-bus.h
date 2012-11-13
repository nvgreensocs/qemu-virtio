/*
 * VirtioBus
 *
 *  Copyright (C) 2012 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef _VIRTIO_BUS_H_
#define _VIRTIO_BUS_H_

#include "qdev.h"
#include "sysemu.h"
#include "virtio.h"

#define TYPE_VIRTIO_BUS "VIRTIO"
#define BUS_NAME "virtio"

typedef struct VirtioBus VirtioBus;
typedef struct VirtioBusInfo VirtioBusInfo;

struct VirtioBusInfo {
    void (*init_cb)(DeviceState *dev);
    void (*exit_cb)(DeviceState *dev);
    VirtIOBindings virtio_bindings;
};

struct VirtioBus {
    BusState qbus;
    int busnr;
    bool bus_in_use;
    uint16_t pci_device_id;
    uint16_t pci_class;
    VirtIODevice *vdev;
    const VirtioBusInfo *info;
};

void virtio_bus_new(VirtioBus *bus, DeviceState *host,
                    const VirtioBusInfo *info);
void virtio_bus_bind_device(VirtioBus *bus);
int virtio_bus_init_cb(VirtioBus *bus, VirtIODevice *dev);
void virtio_bus_exit_cb(VirtioBus *bus);

#endif
