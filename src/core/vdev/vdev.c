// SPDX-License-Identifier: GPL-2.0-only

#include <linux/err.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "core/vdev/vdev.h"

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
	if (IS_ERR(bd_file)) {
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

s32 lsv_vdev_create(const struct lsv_vdev_params *params, struct lsv_vdev **out)
{
	struct lsv_vdev *vdev;
	s32 rc;
	u32 i;

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev)
		return -ENOMEM;

	vdev->members = kcalloc(params->nr_members, sizeof(*vdev->members), GFP_KERNEL);
	if (!vdev->members) {
		rc = -ENOMEM;
		goto free_vdev;
	}

	vdev->ops = params->ops;
	vdev->nr_members = params->nr_members;
	vdev->cell_sectors = params->cell_sectors;
	vdev->chunk_sectors = params->chunk_sectors;

	for (i = 0; i < vdev->nr_members; i++) {
		rc = lsv_member_open(&vdev->members[i], params->paths[i]);
		if (rc)
			goto close_members;
	}

	rc = bioset_init(&vdev->bio_set, BIO_POOL_SIZE, 0, BIOSET_NEED_BVECS);
	if (rc)
		goto close_members;

	rc = vdev->ops->geometry(vdev);
	if (rc)
		goto destroy_layout;

	if (!vdev->capacity_sectors) {
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

/*
 * Free vdev. Replaces lsv_dev_close_back().
 */
void lsv_vdev_destroy(struct lsv_vdev *vdev)
{
}

u64 lsv_vdev_capacity_sectors(const struct lsv_vdev *vdev)
{
	return vdev->capacity_sectors;
}

/*
 *   1. Allocate lsv_vio and save piece and vsector.
 *   2. Set pending = 1 (the submitter reference).
 *   3. Call vdev->ops->submit(vdev, io).
 *   4. After submission, drop the submitter reference; if pending
 *      reaches zero, complete io.
 */
s32 lsv_vdev_submit(struct lsv_vdev *vdev, struct bio *piece, sector_t vsector)
{
	return -ENOSYS;
}

s32 lsv_vdev_flush(struct lsv_vdev *vdev, struct bio *piece)
{
	return -ENOSYS;
}

/*
 * Usable member capacity in sectors: subtract the metadata reserve
 * and round down to whole cells.
 * Use bdev_nr_sectors(member->bd) for the underlying capacity.
 */
u64 lsv_member_usable_sectors(const struct lsv_vdev *vdev, const struct lsv_back *member)
{
	return 0;
}

/*
 * Synchronous read or write at vsector, waiting for completion.
 */
s32 lsv_vdev_io_sync(struct lsv_vdev *vdev, enum req_op op, sector_t vsector, void *buf, size_t len)
{
	return -ENOSYS;
}

/*
 * Submit one member bio.
 *   1. clone = bio_alloc_clone(...);
 *   2. clone->bi_iter.bi_sector = t->member_sector;
 *   3. Save io and t->member in the clone->bi_private context.
 *   4. clone->bi_end_io = lsv_member_endio;
 *   5. atomic_inc(&io->pending);
 *   6. submit_bio_noacct(clone).
 */
s32 lsv_vdev_member_submit(struct lsv_vdev *vdev, struct lsv_vio *io, const struct lsv_vdev_target *t)
{
	return -ENOSYS;
}

/*
 * Complete one member bio (assigned to clone->bi_end_io).
 * Account for its result according to the layout policy.
 * If pending reaches zero, complete io with the final io->status.
 */
void lsv_member_endio(struct bio *clone)
{
}

/*
 * Complete the logical I/O request once all required member bios finish.
 *
 * Set io->bio->bi_status = io->status and call bio_endio(io->bio),
 * which leads to lsv_bio_end_io. Then free io.
 */
void lsv_vio_complete(struct lsv_vio *io)
{
}
