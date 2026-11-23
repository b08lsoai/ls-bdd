// SPDX-License-Identifier: GPL-2.0-only

#ifndef LSV_CORE_VDEV_H
#define LSV_CORE_VDEV_H

#include <linux/atomic.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/* Sectors reserved at the head of the backing device for metadata. */
#define LSV_MEMBER_META_SECTORS 32

struct lsv_back
{
	char *path;
	struct file *bd_file;
	struct block_device *bd;
	sector_t usable_sectors;
};

enum lsv_layout_type
{
	LSV_LAYOUT_LINEAR = 0,
	LSV_LAYOUT_RAID0,
	LSV_LAYOUT_RAID1,
};

struct lsv_vdev;

struct lsv_vdev_target
{
	struct lsv_back *member;
	sector_t member_sector; /* Sector counted from the start of the member. */
};

struct lsv_vio
{
	struct bio *bio;
	struct lsv_vdev *vdev;

	sector_t vsector;

	atomic_t pending; /* Member bios plus one reference while submitting. */
	spinlock_t lock; /* Protects status in submit and endio. */
	blk_status_t status;
};

struct lsv_layout_ops
{
	const char *name;
	enum lsv_layout_type type;

	s32 (*geometry)(struct lsv_vdev *vdev);
	s32 (*submit)(struct lsv_vdev *vdev, struct lsv_vio *io);
	void (*destroy)(struct lsv_vdev *vdev);
};

struct lsv_vdev_params
{
	const struct lsv_layout_ops *ops;
	const char *const *paths;
	u32 nr_members;
	u32 cell_sectors;
	u32 chunk_sectors;
};

struct lsv_vdev
{
	const struct lsv_layout_ops *ops;

	struct lsv_back *members;
	u32 nr_members;

	u32 cell_sectors;
	u32 chunk_sectors;
	sector_t capacity_sectors;

	struct bio_set bio_set;

	void *layout_priv;
};

const struct lsv_layout_ops *lsv_layout_lookup(const char *name);

s32 lsv_vdev_create(const struct lsv_vdev_params *params, struct lsv_vdev **out);
void lsv_vdev_destroy(struct lsv_vdev *vdev);

sector_t lsv_vdev_capacity_sectors(const struct lsv_vdev *vdev);

/* Smallest member capacity after reserving metadata and rounding down to whole cells. */
sector_t lsv_vdev_min_usable_sectors(const struct lsv_vdev *vdev);

/*
 * A zero return means vdev accepted the bio and will complete it exactly once.
 * Completion may happen before this call returns. On a negative return,
 * the caller remains responsible for completing the bio.
 */
s32 lsv_vdev_submit(struct lsv_vdev *vdev, struct bio *piece, sector_t vsector);
s32 lsv_vdev_flush(struct lsv_vdev *vdev, struct bio *piece);

/* buf must be directly mapped and the transfer must fit within one cell. */
s32 lsv_vdev_io_sync(struct lsv_vdev *vdev, enum req_op op, sector_t vsector, void *buf, size_t len);

s32 lsv_vdev_member_submit(struct lsv_vdev *vdev, struct lsv_vio *io, const struct lsv_vdev_target *t);

#endif
