/*
 * Virtio MMIO bindings
 *
 * Copyright (c) 2011 Linaro Limited
 *
 * Author:
 *  Peter Maydell <peter.maydell@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* TODO:
 *  * save/load support
 */

#include "sysbus.h"
#include "virtio.h"
#include "virtio-blk.h"
#include "virtio-net.h"
#include "virtio-serial.h"
#include "qemu/host-utils.h"
#include "virtio-bus.h"

/* #define DEBUG_VIRTIO_MMIO */

#ifdef DEBUG_VIRTIO_MMIO

#define DPRINTF(fmt, ...) \
do { printf("virtio_mmio: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

/* QOM macros */
/* virtio-mmio-bus */
#define TYPE_VIRTIO_MMIO_BUS "virtio-mmio-bus"
#define VIRTIO_MMIO_BUS_GET_CLASS(obj) \
        OBJECT_GET_CLASS(VirtioBusClass, obj, TYPE_VIRTIO_MMIO_BUS)
#define VIRTIO_MMIO_BUS_CLASS(klass) \
        OBJECT_CLASS_CHECK(VirtioBusClass, klass, TYPE_VIRTIO_MMIO_BUS)
#define VIRTIO_MMIO_BUS(obj) \
        OBJECT_CHECK(VirtioBusState, (obj), TYPE_VIRTIO_MMIO_BUS)

/* virtio-mmio */
#define TYPE_VIRTIO_MMIO "virtio-mmio"
#define VIRTIO_MMIO(obj) \
        OBJECT_CHECK(VirtIOMMIOProxy, (obj), TYPE_VIRTIO_MMIO)

/* Memory mapped register offsets */
#define VIRTIO_MMIO_MAGIC 0x0
#define VIRTIO_MMIO_VERSION 0x4
#define VIRTIO_MMIO_DEVICEID 0x8
#define VIRTIO_MMIO_VENDORID 0xc
#define VIRTIO_MMIO_HOSTFEATURES 0x10
#define VIRTIO_MMIO_HOSTFEATURESSEL 0x14
#define VIRTIO_MMIO_GUESTFEATURES 0x20
#define VIRTIO_MMIO_GUESTFEATURESSEL 0x24
#define VIRTIO_MMIO_GUESTPAGESIZE 0x28
#define VIRTIO_MMIO_QUEUESEL 0x30
#define VIRTIO_MMIO_QUEUENUMMAX 0x34
#define VIRTIO_MMIO_QUEUENUM 0x38
#define VIRTIO_MMIO_QUEUEALIGN 0x3c
#define VIRTIO_MMIO_QUEUEPFN 0x40
#define VIRTIO_MMIO_QUEUENOTIFY 0x50
#define VIRTIO_MMIO_INTERRUPTSTATUS 0x60
#define VIRTIO_MMIO_INTERRUPTACK 0x64
#define VIRTIO_MMIO_STATUS 0x70
/* Device specific config space starts here */
#define VIRTIO_MMIO_CONFIG 0x100

#define VIRT_MAGIC 0x74726976 /* 'virt' */
#define VIRT_VERSION 1
#define VIRT_VENDOR 0x554D4551 /* 'QEMU' */

typedef struct {
    /* Generic */
    SysBusDevice busdev;
    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t int_enable;
    uint32_t host_features;
    uint32_t host_features_sel;
    uint32_t guest_features_sel;
    uint32_t guest_page_shift;
    /* virtio-bus */
    VirtioBusState *bus;
} VirtIOMMIOProxy;

static VirtioBusState *virtio_mmio_bus_new(VirtIOMMIOProxy *dev);

static uint64_t virtio_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    VirtIOMMIOProxy *proxy = (VirtIOMMIOProxy *)opaque;
    VirtIODevice *vdev = proxy->bus->vdev;
    DPRINTF("virtio_mmio_read offset 0x%x\n", (int)offset);
    if (offset >= VIRTIO_MMIO_CONFIG) {
        offset -= VIRTIO_MMIO_CONFIG;
        switch (size) {
        case 1:
            return virtio_config_readb(vdev, offset);
        case 2:
            return virtio_config_readw(vdev, offset);
        case 4:
            return virtio_config_readl(vdev, offset);
        default:
            abort();
        }
    }
    if (size != 4) {
        DPRINTF("wrong size access to register!\n");
        return 0;
    }
    switch (offset) {
    case VIRTIO_MMIO_MAGIC:
        return VIRT_MAGIC;
    case VIRTIO_MMIO_VERSION:
        return VIRT_VERSION;
    case VIRTIO_MMIO_DEVICEID:
        return vdev->device_id;
    case VIRTIO_MMIO_VENDORID:
        return VIRT_VENDOR;
    case VIRTIO_MMIO_HOSTFEATURES:
        if (proxy->host_features_sel) {
            return 0;
        }
        return proxy->host_features;
    case VIRTIO_MMIO_QUEUENUMMAX:
        return VIRTQUEUE_MAX_SIZE;
    case VIRTIO_MMIO_QUEUEPFN:
        return virtio_queue_get_addr(vdev, vdev->queue_sel)
            >> proxy->guest_page_shift;
    case VIRTIO_MMIO_INTERRUPTSTATUS:
        return vdev->isr;
    case VIRTIO_MMIO_STATUS:
        return vdev->status;
    case VIRTIO_MMIO_HOSTFEATURESSEL:
    case VIRTIO_MMIO_GUESTFEATURES:
    case VIRTIO_MMIO_GUESTFEATURESSEL:
    case VIRTIO_MMIO_GUESTPAGESIZE:
    case VIRTIO_MMIO_QUEUESEL:
    case VIRTIO_MMIO_QUEUENUM:
    case VIRTIO_MMIO_QUEUEALIGN:
    case VIRTIO_MMIO_QUEUENOTIFY:
    case VIRTIO_MMIO_INTERRUPTACK:
        DPRINTF("read of write-only register\n");
        return 0;
    default:
        DPRINTF("bad register offset\n");
        return 0;
    }
    return 0;
}

static void virtio_mmio_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    VirtIOMMIOProxy *proxy = (VirtIOMMIOProxy *)opaque;
    VirtIODevice *vdev = proxy->bus->vdev;
    DPRINTF("virtio_mmio_write offset 0x%x value 0x%" PRIx64 "\n",
            (int)offset, value);
    if (offset >= VIRTIO_MMIO_CONFIG) {
        offset -= VIRTIO_MMIO_CONFIG;
        switch (size) {
        case 1:
            virtio_config_writeb(vdev, offset, value);
            break;
        case 2:
            virtio_config_writew(vdev, offset, value);
            break;
        case 4:
            virtio_config_writel(vdev, offset, value);
            break;
        default:
            abort();
        }
        return;
    }
    if (size != 4) {
        DPRINTF("wrong size access to register!\n");
        return;
    }
    switch (offset) {
    case VIRTIO_MMIO_HOSTFEATURESSEL:
        proxy->host_features_sel = value;
        break;
    case VIRTIO_MMIO_GUESTFEATURES:
        if (!proxy->guest_features_sel) {
            virtio_set_features(vdev, value);
        }
        break;
    case VIRTIO_MMIO_GUESTFEATURESSEL:
        proxy->guest_features_sel = value;
        break;
    case VIRTIO_MMIO_GUESTPAGESIZE:
        proxy->guest_page_shift = ctz32(value);
        if (proxy->guest_page_shift > 31) {
            proxy->guest_page_shift = 0;
        }
        DPRINTF("guest page size %" PRIx64 " shift %d\n", value,
                proxy->guest_page_shift);
        break;
    case VIRTIO_MMIO_QUEUESEL:
        if (value < VIRTIO_PCI_QUEUE_MAX) {
            vdev->queue_sel = value;
        }
        break;
    case VIRTIO_MMIO_QUEUENUM:
        DPRINTF("mmio_queue write %d max %d\n", (int)value, VIRTQUEUE_MAX_SIZE);
        if (value <= VIRTQUEUE_MAX_SIZE) {
            DPRINTF("calling virtio_queue_set_num\n");
            virtio_queue_set_num(vdev, vdev->queue_sel, value);
        }
        break;
    case VIRTIO_MMIO_QUEUEALIGN:
        virtio_queue_set_align(vdev, vdev->queue_sel, value);
        break;
    case VIRTIO_MMIO_QUEUEPFN:
        if (value == 0) {
            virtio_reset(vdev);
        } else {
            virtio_queue_set_addr(vdev, vdev->queue_sel,
                                  value << proxy->guest_page_shift);
        }
        break;
    case VIRTIO_MMIO_QUEUENOTIFY:
        if (value < VIRTIO_PCI_QUEUE_MAX) {
            virtio_queue_notify(vdev, value);
        }
        break;
    case VIRTIO_MMIO_INTERRUPTACK:
        vdev->isr &= ~value;
        virtio_update_irq(vdev);
        break;
    case VIRTIO_MMIO_STATUS:
        virtio_set_status(vdev, value & 0xff);
        if (vdev->status == 0) {
            virtio_reset(vdev);
        }
        break;
    case VIRTIO_MMIO_MAGIC:
    case VIRTIO_MMIO_VERSION:
    case VIRTIO_MMIO_DEVICEID:
    case VIRTIO_MMIO_VENDORID:
    case VIRTIO_MMIO_HOSTFEATURES:
    case VIRTIO_MMIO_QUEUENUMMAX:
    case VIRTIO_MMIO_INTERRUPTSTATUS:
        DPRINTF("write to readonly register\n");
        break;

    default:
        DPRINTF("bad register offset\n");
    }
}

static const MemoryRegionOps virtio_mem_ops = {
    .read = virtio_mmio_read,
    .write = virtio_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void virtio_mmio_update_irq(void *opaque, uint16_t vector)
{
    VirtIOMMIOProxy *proxy = opaque;
    int level = (proxy->bus->vdev->isr != 0);
    DPRINTF("virtio_mmio setting IRQ %d\n", level);
    qemu_set_irq(proxy->irq, level);
}

static unsigned int virtio_mmio_get_features(void *opaque)
{
    VirtIOMMIOProxy *proxy = opaque;
    return proxy->host_features;
}

static int virtio_mmio_load_config(void *opaque, QEMUFile *f)
{
    VirtIOMMIOProxy *proxy = opaque;
    proxy->int_enable = qemu_get_be32(f);
    proxy->host_features = qemu_get_be32(f);
    proxy->host_features_sel = qemu_get_be32(f);
    proxy->guest_features_sel = qemu_get_be32(f);
    proxy->guest_page_shift = qemu_get_be32(f);
    return 0;
}

static void virtio_mmio_save_config(void *opaque, QEMUFile *f)
{
    VirtIOMMIOProxy *proxy = opaque;
    qemu_put_be32(f, proxy->int_enable);
    qemu_put_be32(f, proxy->host_features);
    qemu_put_be32(f, proxy->host_features_sel);
    qemu_put_be32(f, proxy->guest_features_sel);
    qemu_put_be32(f, proxy->guest_page_shift);
}

static void virtio_mmio_reset(DeviceState *d)
{
    VirtIOMMIOProxy *proxy = FROM_SYSBUS(VirtIOMMIOProxy, sysbus_from_qdev(d));
    virtio_bus_reset(proxy->bus);
}

/* virtio-mmio device */

/* This is called by virtio-bus just after the device is plugged. */
static void virtio_mmio_device_plugged(void *opaque)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(opaque);
    set_virtio_device_nvectors(proxy->bus, 0);
    proxy->host_features |= (0x1 << VIRTIO_F_NOTIFY_ON_EMPTY);
    proxy->host_features = get_virtio_device_features(proxy->bus,
                                                      proxy->host_features);
}

static int virtio_mmio_init(SysBusDevice *sbdev)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(sbdev);
    proxy->bus = virtio_mmio_bus_new(proxy);
    sysbus_init_irq(&proxy->busdev, &proxy->irq);
    memory_region_init_io(&proxy->iomem, &virtio_mem_ops, proxy,
                          TYPE_VIRTIO_MMIO, 0x1000);
    sysbus_init_mmio(&proxy->busdev, &proxy->iomem);
    return 0;
}

static void virtio_mmio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = virtio_mmio_init;
    dc->reset = virtio_mmio_reset;
}

static const TypeInfo virtio_mmio_info = {
    .name          = TYPE_VIRTIO_MMIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VirtIOMMIOProxy),
    .class_init    = virtio_mmio_class_init,
};

/* virtio-mmio-bus. */

static VirtioBusState *virtio_mmio_bus_new(VirtIOMMIOProxy *dev)
{
    DeviceState *qdev = DEVICE(dev);
    BusState *qbus = qbus_create(TYPE_VIRTIO_MMIO_BUS, qdev, NULL);
    VirtioBusState *bus = VIRTIO_BUS(qbus);
    qbus->allow_hotplug = 0;
    qbus->max_dev = 1;
    return bus;
}

static void virtio_mmio_bus_class_init(ObjectClass *klass, void *data)
{
    VirtioBusClass *k = VIRTIO_BUS_CLASS(klass);
    k->notify = virtio_mmio_update_irq;
    k->save_config = virtio_mmio_save_config;
    k->load_config = virtio_mmio_load_config;
    k->get_features = virtio_mmio_get_features;
    k->device_plugged = virtio_mmio_device_plugged;
}

static const TypeInfo virtio_mmio_bus_info = {
    .name          = TYPE_VIRTIO_MMIO_BUS,
    .parent        = TYPE_VIRTIO_BUS,
    .instance_size = sizeof(VirtioBusState),
    .class_init    = virtio_mmio_bus_class_init,
};

static void virtio_mmio_register_types(void)
{
    type_register_static(&virtio_mmio_bus_info);
    type_register_static(&virtio_mmio_info);
}

type_init(virtio_mmio_register_types)
