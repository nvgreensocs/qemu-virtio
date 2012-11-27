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
#define VIRTIO_BUS_GET_CLASS(obj) \
        OBJECT_GET_CLASS(VirtioBusClass, obj, TYPE_VIRTIO_BUS)
#define VIRTIO_BUS_CLASS(klass) \
        OBJECT_CLASS_CHECK(VirtioBusClass, klass, TYPE_VIRTIO_BUS)
#define VIRTIO_BUS(obj) OBJECT_CHECK(VirtioBusState, (obj), TYPE_VIRTIO_BUS)

typedef struct VirtioBusState VirtioBusState;

typedef struct VirtioBusClass {
    /* This is what a VirtioBus must implement */
    BusClass parent;
    void (*notify)(void *opaque, uint16_t vector);
    void (*save_config)(void *opaque, QEMUFile *f);
    void (*save_queue)(void *opaque, int n, QEMUFile *f);
    int (*load_config)(void *opaque, QEMUFile *f);
    int (*load_queue)(void *opaque, int n, QEMUFile *f);
    int (*load_done)(void *opaque, QEMUFile *f);
    unsigned (*get_features)(void *opaque);
    bool (*query_guest_notifiers)(void *opaque);
    int (*set_guest_notifiers)(void *opaque, bool assigned);
    int (*set_host_notifier)(void *opaque, int n, bool assigned);
    void (*vmstate_change)(void *opaque, bool running);
    /*
     * transport independent init function.
     * This is called by virtio-bus just after the device is plugged.
     */
    void (*device_plugged)(void *opaque);
    /*
     * transport independent exit function.
     * This is called by virtio-bus just before the device is unplugged.
     */
    void (*device_unplug)(void *opaque);
} VirtioBusClass;

struct VirtioBusState {
    BusState parent_obj;
    /*
     * Only one VirtIODevice can be plugged on the bus.
     */
    VirtIODevice *vdev;
    /*
     * This should be removed when we refactor virtio-device.
     */
    VirtIOBindings bindings;
};

int virtio_bus_plug_device(VirtIODevice *vdev);
void virtio_bus_reset(VirtioBusState *bus);
void virtio_bus_destroy_device(VirtioBusState *bus);
uint16_t get_virtio_device_id(VirtioBusState *bus);
#endif /* VIRTIO_BUS_H */
