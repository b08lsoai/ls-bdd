# Vdev and layouts

`lmap` maps an upper-device cell (`lba`, LBA1) to a cell in the array
(`pba`, LBA2). It converts that cell number to a 512-byte sector
(`vsector`). The layout selects a member and adds its 32-sector metadata
offset to obtain `member_sector`.

| Layout | Placement |
| --- | --- |
| [Linear](layout_linear.c) | Fills each member's usable address range in order. |
| [RAID0](layout_raid0.c) | Stripes whole chunks across members, up to the capacity of the smallest member rounded down to a whole chunk. |
| [RAID1](layout_raid1.c) | Reads from member 0. Writes go to all mirrors and succeed only if every copy succeeds. |

RAID0 and RAID1 require at least two members. The RAID0 chunk size must be
a multiple of the cell size. RAID1 currently assumes healthy mirrors and has
no read retry or resynchronization.

## I/O

For each cell-bounded piece, `lsv_vdev_submit()` calls the layout's
`submit` method, which calls `lsv_vdev_member_submit()` for each target.
A standalone flush goes to every member. The initial `pending` reference
keeps the request alive while clones are submitted. The last reference
completes the original bio with the first error, if any.
