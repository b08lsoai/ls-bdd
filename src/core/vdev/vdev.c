// SPDX-License-Identifier: GPL-2.0-only

#include <linux/err.h>
#include <linux/file.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "core/vdev/vdev.h"

struct lsv_member_io
{
	struct lsv_vio *io;
	struct lsv_back *member;
};

extern const struct lsv_layout_ops lsv_layout_linear_ops;
extern const struct lsv_layout_ops lsv_layout_raid0_ops;
extern const struct lsv_layout_ops lsv_layout_raid1_ops;

static const struct lsv_layout_ops *const g_layouts[] = {
	&lsv_layout_linear_ops,
	&lsv_layout_raid0_ops,
	&lsv_layout_raid1_ops,
};

const struct lsv_layout_ops *lsv_layout_lookup(const char *name)
{
	u32 i;

	if (!name)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(g_layouts); i++)
		if (!strcmp(g_layouts[i]->name, name))
			return g_layouts[i];

	return NULL;
}

static s32 lsv_member_open(struct lsv_back *member, const char *path)
{
	struct file *bd_file;
	char *member_path;

	member_path = kstrdup(path, GFP_KERNEL);
	if (!member_path)
		return -ENOMEM;

	bd_file = bdev_file_open_by_path(path, BLK_OPEN_READ | BLK_OPEN_WRITE, NULL, NULL);
	if (IS_ERR(bd_file))
	{
		kfree(member_path);
		return PTR_ERR(bd_file);
	}

	member->path = member_path;
	member->bd_file = bd_file;
	member->bd = file_bdev(bd_file);

	return 0;
}

static void lsv_member_close(struct lsv_back *member)
{
	if (member->bd_file)
		fput(member->bd_file);

	member->bd_file = NULL;
	member->bd = NULL;
	kfree(member->path);
	member->path = NULL;
}

static sector_t lsv_member_usable_sectors(const struct lsv_vdev *vdev, const struct lsv_back *member)
{
	sector_t sectors = bdev_nr_sectors(member->bd);

	if (sectors <= LSV_MEMBER_META_SECTORS)
		return 0;

	return round_down(sectors - LSV_MEMBER_META_SECTORS, vdev->cell_sectors);
}

s32 lsv_vdev_create(const struct lsv_vdev_params *params, struct lsv_vdev **out)
{
	struct lsv_vdev *vdev;
	s32 rc;
	u32 i, j;

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev)
		return -ENOMEM;

	vdev->members = kcalloc(params->nr_members, sizeof(*vdev->members), GFP_KERNEL);
	if (!vdev->members)
	{
		rc = -ENOMEM;
		goto free_vdev;
	}

	vdev->ops = params->ops;
	vdev->nr_members = params->nr_members;
	vdev->cell_sectors = params->cell_sectors;
	vdev->chunk_sectors = params->chunk_sectors;

	for (i = 0; i < vdev->nr_members; i++)
	{
		rc = lsv_member_open(&vdev->members[i], params->paths[i]);
		if (rc)
			goto close_members;

		vdev->members[i].usable_sectors = lsv_member_usable_sectors(vdev, &vdev->members[i]);

		for (j = 0; j < i; j++)
		{
			if (vdev->members[i].bd == vdev->members[j].bd)
			{
				lsv_member_close(&vdev->members[i]);
				rc = -EINVAL;
				goto close_members;
			}
		}
	}

	rc = bioset_init(&vdev->bio_set, BIO_POOL_SIZE, 0, BIOSET_NEED_BVECS);
	if (rc)
		goto close_members;

	rc = vdev->ops->geometry(vdev);
	if (rc)
		goto destroy_layout;

	if (!vdev->capacity_sectors)
	{
		rc = -ENOSPC;
		goto destroy_layout;
	}

	*out = vdev;
	return 0;

destroy_layout:
	/* geometry may have allocated private state before failing. */
	if (vdev->ops->destroy)
		vdev->ops->destroy(vdev);
	bioset_exit(&vdev->bio_set);
close_members:
	while (i > 0)
		lsv_member_close(&vdev->members[--i]);
	kfree(vdev->members);
free_vdev:
	kfree(vdev);
	return rc;
}

void lsv_vdev_destroy(struct lsv_vdev *vdev)
{
	u32 i;

	if (!vdev)
		return;

	if (vdev->ops->destroy)
		vdev->ops->destroy(vdev);

	bioset_exit(&vdev->bio_set);

	for (i = vdev->nr_members; i > 0; i--)
		lsv_member_close(&vdev->members[i - 1]);

	kfree(vdev->members);
	kfree(vdev);
}

sector_t lsv_vdev_min_usable_sectors(const struct lsv_vdev *vdev)
{
	sector_t min_usable = vdev->members[0].usable_sectors;
	u32 i;

	for (i = 1; i < vdev->nr_members; i++)
		min_usable = min(min_usable, vdev->members[i].usable_sectors);

	return min_usable;
}

sector_t lsv_vdev_capacity_sectors(const struct lsv_vdev *vdev)
{
	return vdev->capacity_sectors;
}

s32 lsv_vdev_submit(struct lsv_vdev *vdev, struct bio *piece, sector_t vsector)
{
	struct lsv_vio *vio;
	unsigned long flags;
	s32 rc;

	vio = kzalloc(sizeof(*vio), GFP_NOIO);
	if (!vio)
		return -ENOMEM;

	vio->bio = piece;
	vio->vdev = vdev;
	vio->vsector = vsector;
	vio->status = BLK_STS_OK;
	spin_lock_init(&vio->lock);
	atomic_set(&vio->pending, 1);

	rc = vdev->ops->submit(vdev, vio);
	if (rc < 0)
	{
		spin_lock_irqsave(&vio->lock, flags);
		if (vio->status == BLK_STS_OK)
			vio->status = errno_to_blk_status(rc);
		spin_unlock_irqrestore(&vio->lock, flags);
	}

	if (atomic_dec_and_test(&vio->pending))
		lsv_vio_complete(vio);

	return 0;
}

s32 lsv_vdev_flush(struct lsv_vdev *vdev, struct bio *piece)
{
	struct lsv_vio *vio;
	unsigned long flags;
	s32 rc;
	u32 i;

	vio = kzalloc(sizeof(*vio), GFP_NOIO);
	if (!vio)
		return -ENOMEM;

	vio->bio = piece;
	vio->vdev = vdev;
	vio->status = BLK_STS_OK;
	spin_lock_init(&vio->lock);
	atomic_set(&vio->pending, 1);

	for (i = 0; i < vdev->nr_members; i++)
	{
		struct lsv_vdev_target target = {
			.member = &vdev->members[i],
			.member_sector = 0,
		};

		rc = lsv_vdev_member_submit(vdev, vio, &target);
		if (rc < 0)
		{
			spin_lock_irqsave(&vio->lock, flags);
			if (vio->status == BLK_STS_OK)
				vio->status = errno_to_blk_status(rc);
			spin_unlock_irqrestore(&vio->lock, flags);
		}
	}

	if (atomic_dec_and_test(&vio->pending))
		lsv_vio_complete(vio);

	return 0;
}

/*
 * Synchronous read or write at vsector, waiting for completion.
 */
s32 lsv_vdev_io_sync(struct lsv_vdev *vdev, enum req_op op, sector_t vsector, void *buf, size_t len)
{
	return -ENOSYS;
}

s32 lsv_vdev_member_submit(struct lsv_vdev *vdev, struct lsv_vio *io, const struct lsv_vdev_target *t)
{
	struct lsv_member_io *member_io;
	struct bio *clone;

	member_io = kmalloc(sizeof(*member_io), GFP_NOIO);
	if (!member_io)
		return -ENOMEM;

	clone = bio_alloc_clone(t->member->bd, io->bio, GFP_NOIO, &vdev->bio_set);
	if (!clone)
	{
		kfree(member_io);
		return -ENOMEM;
	}

	member_io->io = io;
	member_io->member = t->member;

	clone->bi_iter.bi_sector = t->member_sector;
	clone->bi_private = member_io;
	clone->bi_end_io = lsv_member_endio;

	atomic_inc(&io->pending);
	submit_bio_noacct(clone);
	return 0;
}

/*
 * Complete one member bio (assigned to clone->bi_end_io).
 * Linear/RAID0 policy: preserve the first error. RAID1 will need
 * layout-specific result handling before it can submit member I/O.
 */
void lsv_member_endio(struct bio *clone)
{
	struct lsv_member_io *member_io = clone->bi_private;
	struct lsv_vio *io = member_io->io;
	unsigned long flags;

	if (clone->bi_status != BLK_STS_OK)
	{
		spin_lock_irqsave(&io->lock, flags);
		if (io->status == BLK_STS_OK)
			io->status = clone->bi_status;
		spin_unlock_irqrestore(&io->lock, flags);
	}

	bio_put(clone);
	kfree(member_io);

	if (atomic_dec_and_test(&io->pending))
		lsv_vio_complete(io);
}

void lsv_vio_complete(struct lsv_vio *io)
{
	io->bio->bi_status = io->status;
	bio_endio(io->bio);
	kfree(io);
}
