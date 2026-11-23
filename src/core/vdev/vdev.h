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

/*
 * One physical target: a member and its sector.
 * member_sector includes the data-area offset on that member.
 */
struct lsv_vdev_target
{
	struct lsv_back *member;
	sector_t member_sector;
};

/*
 * A logical request to vdev, addressed in sectors.
 * Operation, flags and length come from bio: bio_op(), bi_opf, bio_sectors().
 */
struct lsv_vio
{
	struct bio *bio;
	struct lsv_vdev *vdev;

	sector_t vsector;

	atomic_t pending; // Outstanding member bios plus one reference held during submission.
	spinlock_t lock; // Protects status updates from submit and endio.
	blk_status_t status; // Logical request result according to the layout policy.
};

/*
 * The layout defines geometry and executes logical requests.
 * submit may send several member bios for one io.
 */
struct lsv_layout_ops
{
	const char *name;
	enum lsv_layout_type type;

	s32 (*geometry)(struct lsv_vdev *vdev);				   // Calculate capacity_sectors from the members.
	s32 (*submit)(struct lsv_vdev *vdev, struct lsv_vio *io); // Execute one logical I/O request.
	void (*destroy)(struct lsv_vdev *vdev);				   // Free layout-private data (vdev->layout_priv).
};

/*
 * Creation parameters.
 */
struct lsv_vdev_params
{
	const struct lsv_layout_ops *ops;
	const char *const *paths;
	u32 nr_members;
	u32 cell_sectors;  /* Nonzero cell size, a power of two. */
	u32 chunk_sectors; /* RAID0: > 0 and a multiple of cell_sectors; otherwise 0. */
};

struct lsv_vdev
{
	const struct lsv_layout_ops *ops;

	struct lsv_back *members; /* Array of nr_members entries. */
	u32 nr_members; /* Number of member slots. */

	u32 cell_sectors;
	u32 chunk_sectors; /* Chunk size in sectors. */
	sector_t capacity_sectors; /* Usable device capacity in sectors. */

	struct bio_set bio_set; /* BIO pool for member clones. */

	void *layout_priv; // Layout-private data.
};

const struct lsv_layout_ops *lsv_layout_lookup(const char *name);

/*
 * Create vdev.
 * Open all members, calculate geometry and initialize bio_set.
 * Replaces lsv_dev_open_back() in dev.c.
 */
s32 lsv_vdev_create(const struct lsv_vdev_params *params, struct lsv_vdev **out);
void lsv_vdev_destroy(struct lsv_vdev *vdev);

/*
 * Usable device capacity in sectors.
 */
sector_t lsv_vdev_capacity_sectors(const struct lsv_vdev *vdev);

/* Minimum usable capacity across a nonempty member array. */
sector_t lsv_vdev_min_usable_sectors(const struct lsv_vdev *vdev);

/* ---- Logical I/O submission ---- */

/*
 * Entry point for bio.c. Replaces:
 *   bio->bi_iter.bi_sector = lsv_lmap_data_sector(...); submit_bio_noacct(bio);
 * Creates lsv_vio and delegates to the layout (ops->submit).
 */
s32 lsv_vdev_submit(struct lsv_vdev *vdev, struct bio *piece, sector_t vsector);

s32 lsv_vdev_flush(struct lsv_vdev *vdev, struct bio *piece);

/*
 * Synchronous read or write at vsector, waiting for completion.
 */
s32 lsv_vdev_io_sync(struct lsv_vdev *vdev, enum req_op op, sector_t vsector, void *buf, size_t len);

s32 lsv_vdev_member_submit(struct lsv_vdev *vdev, struct lsv_vio *io, const struct lsv_vdev_target *t);

/*
 * Complete one member bio (assigned to clone->bi_end_io).
 * Account for its result according to the layout policy, then drop its reference.
 * When pending reaches zero, complete io with the status determined by the layout.
 */
void lsv_member_endio(struct bio *clone);

/*
 * Complete the logical I/O request using the accumulated io->status.
 */
void lsv_vio_complete(struct lsv_vio *io);

#endif /* LSV_CORE_VDEV_H */
