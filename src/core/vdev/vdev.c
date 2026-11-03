// SPDX-License-Identifier: GPL-2.0-only

#include <linux/err.h>
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

/*
 * Create the virtual device. Replaces lsv_dev_open_back().
 */
s32 lsv_vdev_create(const struct lsv_vdev_params *params, struct lsv_vdev **out)
{
	return -ENOSYS;
}

/*
 * Free vdev. Replaces lsv_dev_close_back().
 */
void lsv_vdev_destroy(struct lsv_vdev *v)
{
}

u64 lsv_vdev_capacity_sectors(const struct lsv_vdev *v)
{
	return v->capacity_sectors;
}

/*
 *   1. Allocate lsv_vio and save piece and vsector.
 *   2. Set pending = 1 (the submitter reference).
 *   3. Call v->ops->submit(v, io).
 *   4. After submission, drop the submitter reference; if pending
 *      reaches zero, complete io.
 */
s32 lsv_vdev_submit(struct lsv_vdev *v, struct bio *piece, sector_t vsector)
{
	return -ENOSYS;
}

s32 lsv_vdev_flush(struct lsv_vdev *v, struct bio *piece)
{
	return -ENOSYS;
}

/*
 * Usable member capacity in sectors: subtract the metadata reserve
 * and round down to whole cells.
 * Use bdev_nr_sectors(member->bd) for the underlying capacity.
 */
u64 lsv_member_usable_sectors(const struct lsv_vdev *v, const struct lsv_back *member)
{
	return 0;
}

/*
 * Synchronous read or write at vsector, waiting for completion.
 */
s32 lsv_vdev_io_sync(struct lsv_vdev *v, enum req_op op, sector_t vsector, void *buf, size_t len)
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
s32 lsv_vdev_member_submit(struct lsv_vdev *v, struct lsv_vio *io, const struct lsv_vdev_target *t)
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
