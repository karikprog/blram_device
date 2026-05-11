// SPDX-License-Identifier: GPL-2.0-only
/*
* A very simple RAM block device: Karamanov Karim <karamanov2007@gmail.com>
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/hdreg.h>

static int major_num = 0;
module_param(major_num, int, 0);

#define KERNEL_SECTOR_SIZE 512
#define BLKRAM_NAME "my_blkram_dev"
#define DEVICE_CAPACITY 16777216 // 16mb

struct blk_dev {
	u8 *data;
	sector_t capacity;
	struct gendisk *disk;
	struct blk_mq_tag_set tag_set;

	spinlock_t lock;
};

static struct blk_dev *device = NULL;

static int blk_dev_open(struct gendisk *disk, blk_mode_t mode)
{
	return 0;
}

static void blk_dev_release(struct gendisk *disk)
{
	pr_info("Disk closed");
}

static int blk_dev_ioctl(struct block_device *bdev, blk_mode_t mode,
			 unsigned cmd, unsigned long arg)
{
	if (cmd == HDIO_GETGEO) {
		struct hd_geometry geo;
		geo.heads = 4;
		geo.sectors = 16;
		geo.cylinders = (device->capacity & ~0x3f) >> 6;
		geo.start = 4;
		if (copy_to_user((void __user *)arg, &geo, sizeof(geo)))
			return -EFAULT;

		return 0;
	}

	return -ENOTTY;
}

static blk_status_t blk_dev_queue_rq(struct blk_mq_hw_ctx *hctx,
				     const struct blk_mq_queue_data *bd)
{
	struct request *rq = bd->rq;
	struct blk_dev *dev = hctx->queue->queuedata;

	struct req_iterator iter;
	struct bio_vec bvec;

	loff_t pos = blk_rq_pos(rq) << SECTOR_SHIFT;
	unsigned int total_bytes = 0;

	blk_mq_start_request(rq);

	spin_lock(&dev->lock);

	rq_for_each_segment(bvec, rq, iter) {
		void *buffer = page_address(bvec.bv_page) + bvec.bv_offset;
		unsigned int len = bvec.bv_len;

		if (pos + len > DEVICE_CAPACITY) {
			pr_err("Access out of bounds: pos %llu, len %u\n", pos,
			       len);
			spin_unlock(&dev->lock);
			goto io_error;
		}

		if (rq_data_dir(rq) == WRITE)
			memcpy(dev->data + pos, buffer, len);
		else
			memcpy(buffer, dev->data + pos, len);

		pos += len;
		total_bytes += len;
	}

	spin_unlock(&dev->lock);

	if (blk_update_request(rq, BLK_STS_OK, total_bytes))
		return BLK_STS_OK;

	__blk_mq_end_request(rq, BLK_STS_OK);

	return BLK_STS_OK;

io_error:
	__blk_mq_end_request(rq, BLK_STS_IOERR);
	return BLK_STS_IOERR;
}

static const struct blk_mq_ops blk_dev_mq_ops = {
	.queue_rq = blk_dev_queue_rq,
};

static const struct block_device_operations blk_dev_ops = {
	.owner = THIS_MODULE,
	.open = blk_dev_open,
	.release = blk_dev_release,
	.ioctl = blk_dev_ioctl,
};

static struct queue_limits lim = {
	.logical_block_size = KERNEL_SECTOR_SIZE,
	.physical_block_size = KERNEL_SECTOR_SIZE,
};

static int __init ramdisk_driver_init(void)
{
	int err;

	pr_info("Initializing RAM disk module\n");

	device = kzalloc(sizeof(struct blk_dev), GFP_KERNEL);
	if (device == NULL) {
		pr_err("Faild to allocate struct dev\n");
		return -ENOMEM;
	}

	device->capacity = DEVICE_CAPACITY >> SECTOR_SHIFT;
	device->data = vzalloc(DEVICE_CAPACITY);
	if (device->data == NULL) {
		pr_err("Faild to allocate disk storage\n");
		err = -ENOMEM;
		goto err_free_dev;
	}

	spin_lock_init(&device->lock);

	major_num = register_blkdev(major_num, BLKRAM_NAME);
	if (major_num < 0) {
		pr_err("Falid to get major number\n");
		err = major_num;
		goto err_free_data;
	}

	device->tag_set.ops = &blk_dev_mq_ops;
	device->tag_set.nr_hw_queues = 1;
	device->tag_set.queue_depth = 128;
	device->tag_set.numa_node = NUMA_NO_NODE;
	device->tag_set.flags = 0;
	device->tag_set.driver_data = device;

	err = blk_mq_alloc_tag_set(&device->tag_set);
	if (err) {
		pr_err("Faild to allocate tag_set\n");
		goto err_uneregister_blk_dev;
	}

	device->disk = blk_mq_alloc_disk(&device->tag_set, &lim, device);
	if (IS_ERR(device->disk)) {
		pr_err("Faild to allocate gendisk\n");
		err = PTR_ERR(device->disk);
		goto err_free_tag_set;
	}

	device->disk->major = major_num;
	device->disk->first_minor = 0;
	device->disk->minors = 1;
	device->disk->fops = &blk_dev_ops;
	device->disk->private_data = device;
	set_capacity(device->disk, device->capacity);
	snprintf(device->disk->disk_name, DISK_NAME_LEN, BLKRAM_NAME);

	err = add_disk(device->disk);
	if (err) {
		pr_err("Faild to add disk\n");
		goto err_free_gendisk;
	}

	pr_info("Module loaded seccessfully");
	return 0;

err_free_gendisk:
	put_disk(device->disk);
err_free_tag_set:
	blk_mq_free_tag_set(&device->tag_set);
err_uneregister_blk_dev:
	unregister_blkdev(major_num, BLKRAM_NAME);
err_free_data:
	vfree(device->data);
err_free_dev:
	kfree(device);
	return err;
}

static void __exit ramdisk_driver_exit(void)
{
	pr_info("Module RAM disk exit\n");

	if (device) {
		if (device->disk) {
			del_gendisk(device->disk);
			put_disk(device->disk);
		}

		blk_mq_free_tag_set(&device->tag_set);

		if (major_num > 0)
			unregister_blkdev(major_num, BLKRAM_NAME);

		if (device->data) {
			vfree(device->data);
		}

		kfree(device);
	}
}

module_init(ramdisk_driver_init);
module_exit(ramdisk_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Karamanov Karim");
MODULE_DESCRIPTION("Smple RAM block device");