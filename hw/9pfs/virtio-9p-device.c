/*
 * Virtio 9p backend
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "hw/virtio.h"
#include "hw/pc.h"
#include "qemu_socket.h"
#include "hw/virtio-pci.h"
#include "hw/virtio-transport.h"
#include "hw/virtio-pci.h"
#include "virtio-9p.h"
#include "fsdev/qemu-fsdev.h"
#include "virtio-9p-xattr.h"
#include "virtio-9p-coth.h"

static uint32_t virtio_9p_get_features(VirtIODevice *vdev, uint32_t features)
{
    features |= 1 << VIRTIO_9P_MOUNT_TAG;
    return features;
}

static V9fsState *to_virtio_9p(VirtIODevice *vdev)
{
    return (V9fsState *)vdev;
}

static void virtio_9p_get_config(VirtIODevice *vdev, uint8_t *config)
{
    int len;
    struct virtio_9p_config *cfg;
    V9fsState *s = to_virtio_9p(vdev);

    len = strlen(s->tag);
    cfg = g_malloc0(sizeof(struct virtio_9p_config) + len);
    stw_raw(&cfg->tag_len, len);
    /* We don't copy the terminating null to config space */
    memcpy(cfg->tag, s->tag, len);
    memcpy(config, cfg, s->config_size);
    g_free(cfg);
}

VirtIODevice *virtio_9p_init(DeviceState *dev, V9fsConf *conf)
{
    V9fsState *s;
    int i, len;
    struct stat stat;
    FsDriverEntry *fse;
    V9fsPath path;

    s = (V9fsState *)virtio_common_init("virtio-9p",
                                    VIRTIO_ID_9P,
                                    sizeof(struct virtio_9p_config)+
                                    MAX_TAG_LEN,
                                    sizeof(V9fsState));
    /* initialize pdu allocator */
    QLIST_INIT(&s->free_list);
    QLIST_INIT(&s->active_list);
    for (i = 0; i < (MAX_REQ - 1); i++) {
        QLIST_INSERT_HEAD(&s->free_list, &s->pdus[i], next);
    }

    s->vq = virtio_add_queue(&s->vdev, MAX_REQ, handle_9p_output);

    fse = get_fsdev_fsentry(conf->fsdev_id);

    if (!fse) {
        /* We don't have a fsdev identified by fsdev_id */
        fprintf(stderr, "Virtio-9p device couldn't find fsdev with the "
                "id = %s\n", conf->fsdev_id ? conf->fsdev_id : "NULL");
        exit(1);
    }

    if (!conf->tag) {
        /* we haven't specified a mount_tag */
        fprintf(stderr, "fsdev with id %s needs mount_tag arguments\n",
                conf->fsdev_id);
        exit(1);
    }

    s->ctx.export_flags = fse->export_flags;
    if (fse->path) {
        s->ctx.fs_root = g_strdup(fse->path);
    } else {
        s->ctx.fs_root = NULL;
    }
    s->ctx.exops.get_st_gen = NULL;
    len = strlen(conf->tag);
    if (len > MAX_TAG_LEN - 1) {
        fprintf(stderr, "mount tag '%s' (%d bytes) is longer than "
                "maximum (%d bytes)", conf->tag, len, MAX_TAG_LEN - 1);
        exit(1);
    }

    s->tag = strdup(conf->tag);
    s->ctx.uid = -1;

    s->ops = fse->ops;
    s->vdev.get_features = virtio_9p_get_features;
    s->config_size = sizeof(struct virtio_9p_config) + len;
    s->vdev.get_config = virtio_9p_get_config;
    s->fid_list = NULL;
    qemu_co_rwlock_init(&s->rename_lock);

    if (s->ops->init(&s->ctx) < 0) {
        fprintf(stderr, "Virtio-9p Failed to initialize fs-driver with id:%s"
                " and export path:%s\n", conf->fsdev_id, s->ctx.fs_root);
        exit(1);
    }
    if (v9fs_init_worker_threads() < 0) {
        fprintf(stderr, "worker thread initialization failed\n");
        exit(1);
    }

    /*
     * Check details of export path, We need to use fs driver
     * call back to do that. Since we are in the init path, we don't
     * use co-routines here.
     */
    v9fs_path_init(&path);
    if (s->ops->name_to_path(&s->ctx, NULL, "/", &path) < 0) {
        fprintf(stderr,
                "error in converting name to path %s", strerror(errno));
        exit(1);
    }
    if (s->ops->lstat(&s->ctx, &path, &stat)) {
        fprintf(stderr, "share path %s does not exist\n", fse->path);
        exit(1);
    } else if (!S_ISDIR(stat.st_mode)) {
        fprintf(stderr, "share path %s is not a directory\n", fse->path);
        exit(1);
    }
    v9fs_path_free(&path);

    return &s->vdev;
}

/******************** VirtIO9P Device **********************/

static int virtio_9pdev_init(DeviceState *dev)
{
    VirtIODevice *vdev;
    VirtIO9PState *s = DO_UPCAST(VirtIO9PState, qdev, dev);

    vdev = virtio_9p_init(dev, &s->v9fs);
    if (!vdev) {
        return -1;
    }

    if (s->trl == NULL) {
        error_report("transport property not set");
        return -1;
    }

    /* Pass default host_features to transport */
    s->trl->host_features = s->host_features;

    if (virtio_call_backend_init_cb(dev, s->trl, vdev) != 0) {
        return -1;
    }

    /* Binding should be ready here, let's get final features */
    if (vdev->binding->get_features) {
        s->host_features = vdev->binding->get_features(vdev->binding_opaque);
    }
    return 0;
}

static Property virtio_9pdev_properties[] = {
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIO9PState, host_features),
    DEFINE_PROP_STRING("mount_tag", VirtIO9PState, v9fs.tag),
    DEFINE_PROP_STRING("fsdev", VirtIO9PState, v9fs.fsdev_id),
    DEFINE_PROP_TRANSPORT("transport", VirtIO9PState, trl),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_9pdev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->init = virtio_9pdev_init;
    dc->props = virtio_9pdev_properties;
}

static TypeInfo virtio_9pdev_info = {
    .name = "virtio-9p",
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(VirtIO9PState),
    .class_init = virtio_9pdev_class_init,
};

/**************** VirtIO9P-PCI Device *******************/
/* This device create virtio-pci and virtio-9p device   */


static int virtio_9p_pcidev_init(DeviceState *dev)
{
    VirtIODevice *vdev;
    VirtIO9PState *s = DO_UPCAST(VirtIO9PState, qdev, dev);
    PCIBus *rootpcibus;
    PCIDevice *vpcidev;

    /* We have to create a new virtio-pci */
    rootpcibus = pci_find_root_bus(0);
    if (rootpcibus == NULL) {
        /* No PCI BUS ? */
        return -1;
    }

    vpcidev = pci_create_simple(rootpcibus, -1, "virtio-pci");
    if (vpcidev == NULL) {
        return -1;
    }

    /* Link Virtio BALLOON to Virtio PCI */
    s->trl = virtio_find_transport_by_device_state(&(vpcidev->qdev));

    vdev = virtio_9p_init(dev, &s->v9fs);
    if (!vdev) {
        return -1;
    }

    /* Pass default host_features to transport */
    s->trl->host_features = s->host_features;

    if (virtio_call_backend_init_cb(dev, s->trl, vdev) != 0) {
        return -1;
    }

    /* Binding should be ready here, let's get final features */
    if (vdev->binding->get_features) {
        s->host_features = vdev->binding->get_features(vdev->binding_opaque);
    }
    return 0;
}

static Property virtio_9p_pci_properties[] = {
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIO9PState, host_features),
    DEFINE_PROP_STRING("mount_tag", VirtIO9PState, v9fs.tag),
    DEFINE_PROP_STRING("fsdev", VirtIO9PState, v9fs.fsdev_id),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_9p_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->init = virtio_9p_pcidev_init;
    dc->props = virtio_9p_pci_properties;
}

static TypeInfo virtio_9p_pci_info = {
    .name = "virtio-9p-pci",
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(VirtIO9PState),
    .class_init = virtio_9p_pci_class_init,
};

/*************************************************************/


static void virtio_9p_register_types(void)
{
    type_register_static(&virtio_9pdev_info);
    type_register_static(&virtio_9p_pci_info);
    virtio_9p_set_fd_limit();
}

type_init(virtio_9p_register_types)
