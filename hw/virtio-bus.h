/*
 * VirtioBus
 *
 *  Copyright (C) 2012 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef VIRTIO_BUS_H
#define VIRTIO_BUS_H

#include "qdev.h"
#include "sysemu.h"
#include "virtio.h"

#define TYPE_VIRTIO_BUS "virtio-bus"
#define VIRTIO_BUS(obj) OBJECT_CHECK(VirtioBus, (obj), TYPE_VIRTIO_BUS)

typedef struct VirtioBus VirtioBus;
typedef struct VirtioBusInfo VirtioBusInfo;

struct VirtioBusInfo {
    void (*init_cb)(DeviceState *dev);
    void (*exit_cb)(DeviceState *dev);
    VirtIOBindings virtio_bindings;
};

struct VirtioBus {
    BusState qbus;
    bool bus_in_use;
    /* Only one VirtIODevice can be plugged on the bus. */
    VirtIODevice *vdev;
    const VirtioBusInfo *info;
};

VirtioBus *virtio_bus_new(DeviceState *host, const VirtioBusInfo *info);
void virtio_bus_bind_device(VirtioBus *bus);
int virtio_bus_plug_device(VirtioBus *bus, VirtIODevice *vdev);
void virtio_bus_exit_cb(VirtioBus *bus);
uint16_t get_virtio_device_id(VirtioBus *bus);

#endif /* VIRTIO_BUS_H */
