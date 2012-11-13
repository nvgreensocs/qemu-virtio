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

#include "hw.h"
#include "qemu-error.h"
#include "qdev.h"
#include "virtio-bus.h"
#include "virtio.h"

#define DEBUG_VIRTIO_BUS

#ifdef DEBUG_VIRTIO_BUS

#define DPRINTF(fmt, ...) \
do { printf("virtio_bus: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

static char *virtio_bus_get_fw_dev_path(DeviceState *dev);

static void virtio_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);
    k->get_fw_dev_path = virtio_bus_get_fw_dev_path;
}

static const TypeInfo virtio_bus_info = {
    .name = TYPE_VIRTIO_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(VirtioBus),
    .class_init = virtio_bus_class_init,
};

/* VirtioBus */

static int next_virtio_bus;

/* Create a virtio bus, and attach to transport.  */
void virtio_bus_new(VirtioBus *bus, DeviceState *host,
                    const VirtioBusInfo *info)
{
    /*
     * Setting name to NULL return always "virtio.0"
     * as bus name in info qtree.
     */
    char *bus_name = g_strdup_printf("%s.%d", BUS_NAME, next_virtio_bus);
    qbus_create_inplace(&bus->qbus, TYPE_VIRTIO_BUS, host, bus_name);
    bus->busnr = next_virtio_bus++;
    bus->info = info;
    /* no hotplug for the moment ? */
    bus->qbus.allow_hotplug = 0;
    bus->bus_in_use = false;
    DPRINTF("bus %s created\n", bus_name);
}

/* Bind the VirtIODevice to the VirtioBus. */
void virtio_bus_bind_device(VirtioBus *bus)
{
    assert(bus != NULL);
    assert(bus->vdev != NULL);
    virtio_bind_device(bus->vdev, &(bus->info->virtio_bindings),
                       bus->qbus.parent);
}

/* This must be called to when the VirtIODevice init */
int virtio_bus_init_cb(VirtioBus *bus, VirtIODevice *vdev)
{
    if (bus->bus_in_use == true) {
        error_report("%s in use.\n", bus->qbus.name);
        return -1;
    }
    assert(bus->info->init_cb != NULL);
    /* keep the VirtIODevice in the VirtioBus */
    bus->vdev = vdev;
    bus->info->init_cb(bus->qbus.parent);

    bus->bus_in_use = true;
    return 0;
}

/* This must be called when the VirtIODevice exit */
void virtio_bus_exit_cb(VirtioBus *bus)
{
    assert(bus->info->exit_cb != NULL);
    bus->info->exit_cb(bus->qbus.parent);
    bus->bus_in_use = false;
}

static char *virtio_bus_get_fw_dev_path(DeviceState *dev)
{
    return g_strdup_printf("%s", qdev_fw_name(dev));
}


static void virtio_register_types(void)
{
    type_register_static(&virtio_bus_info);
}

type_init(virtio_register_types)
