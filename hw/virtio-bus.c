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

#include "hw.h"
#include "qemu-error.h"
#include "qdev.h"
#include "virtio-bus.h"
#include "virtio.h"

#define DEBUG_VIRTIO_BUS 1

#define DPRINTF(fmt, ...) if (DEBUG_VIRTIO_BUS) {                        \
                            printf("virtio_bus: " fmt , ## __VA_ARGS__); \
                          }

static void virtio_bus_init_cb(VirtioBus *bus);
static int virtio_bus_reset(BusState *qbus);

static void virtio_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);
    k->reset = virtio_bus_reset;
}

static TypeInfo virtio_bus_info = {
    .name = TYPE_VIRTIO_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(VirtioBus),
    .class_init = virtio_bus_class_init,
};

/* Reset the bus */
static int virtio_bus_reset(BusState *qbus)
{
    VirtioBus *bus = VIRTIO_BUS(qbus);
    if (bus->bus_in_use) {
        virtio_reset(bus->vdev);
    }
    return 1;
}

/* Plug the VirtIODevice */
int virtio_bus_plug_device(VirtioBus *bus, VirtIODevice *vdev)
{
    BusState *qbus = BUS(bus);
    /*
     * This is a problem, when bus= option is not set, the last created
     * virtio-bus is used. So it give the following error.
     */
    DPRINTF("plug device into %s.\n", qbus->name);
    if (bus->bus_in_use) {
        error_report("%s in use.\n", qbus->name);
        return -1;
    }
    bus->bus_in_use = true;

    /* keep the VirtIODevice in the VirtioBus. */
    bus->vdev = vdev;

    /* call the "transport" callback. */
    virtio_bus_init_cb(bus);
    return 0;
}

/* Create a virtio bus.  */
VirtioBus *virtio_bus_new(DeviceState *host, const VirtioBusInfo *info)
{
    /*
     * This is needed, as we want to have different names for each virtio-bus.
     * If we don't do that, we can't add more than one VirtIODevice.
     */
    static int next_virtio_bus;
    char *bus_name = g_strdup_printf("virtio-bus.%d", next_virtio_bus++);

    BusState *qbus = qbus_create(TYPE_VIRTIO_BUS, host, bus_name);
    VirtioBus *bus = VIRTIO_BUS(qbus);
    bus->info = info;
    qbus->allow_hotplug = 0;
    bus->bus_in_use = false;
    DPRINTF("%s bus created\n", bus_name);
    return bus;
}

/* Bind the VirtIODevice to the VirtioBus. */
void virtio_bus_bind_device(VirtioBus *bus)
{
    BusState *qbus = BUS(bus);
    assert(bus != NULL);
    assert(bus->vdev != NULL);
    virtio_bind_device(bus->vdev, &(bus->info->virtio_bindings), qbus->parent);
}

/*
 * Transport independent init.
 * This must be called after VirtIODevice initialization.
 */
static void virtio_bus_init_cb(VirtioBus *bus)
{
    BusState *qbus = BUS(bus);
    assert(bus->info->init_cb != NULL);
    bus->info->init_cb(qbus->parent);
}

/*
 * Transport independent exit.
 * This must be called by the VirtIODevice before destroying it.
 */
void virtio_bus_exit_cb(VirtioBus *bus)
{
    BusState *qbus = BUS(bus);
    assert(bus->info->exit_cb != NULL);
    bus->info->exit_cb(qbus->parent);
    bus->bus_in_use = false;
}

/* Return the virtio device id of the plugged device. */
uint16_t get_virtio_device_id(VirtioBus *bus)
{
    return bus->vdev->device_id;
}

static void virtio_register_types(void)
{
    type_register_static(&virtio_bus_info);
}

type_init(virtio_register_types)
