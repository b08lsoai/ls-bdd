# Vdev and Layouts

The `vdev` layer combines backing devices into a common address space.
Layouts: linear, RAID0 and RAID1.

## Main Structures

| Structure | Purpose |
| --- | --- |
| `lsv_vdev_params` | Creation parameters: layout, member paths, cell and chunk sizes. |
| `lsv_vdev` | Members in `members[]`, geometry, clone pool and layout state. |
| `lsv_back` | One member: its path and open block device. |
| `lsv_vio` | One request: `bio`, `vdev`, address, `pending` and final status. |
| `lsv_vdev_target` | One destination: a member and its sector. |

## Addressing

`lmap` maps LBA1 to LBA2; the layout maps LBA2 to locations on members:

- `lba`: logical cell of the upper device (LBA1).
- `pba`: cell storing a data version in the `vdev` address space (LBA2).
- `vsector`: the same LBA2 expressed in 512-byte sectors.
- `member_sector`: a member sector, including its data-area offset.

## Request Processing

1. `lsv_vdev_submit()` creates `vio`, sets `pending = 1` and calls `ops->submit()`.
2. The selected layout's `ops->submit()` chooses the targets and calls
   `lsv_vdev_member_submit()` for each one. This function prepares a clone and its
   context, then increments `pending` before calling `submit_bio_noacct()`.
3. After the layout returns, `lsv_vdev_submit()` accounts for submission errors
   and drops the initial reference.
4. `lsv_member_endio()` accounts for the result according to the layout policy,
   frees the clone and its context, then decrements `pending`.
5. When `pending` reaches zero, `lsv_vio_complete()` copies the final status
   to the original `bio`, calls `bio_endio()` and frees `vio`.

`pending` tracks outstanding work; the layout determines success.
The initial reference prevents `vio` from being freed while clones are submitted.

## Layouts

| Layout | Data placement |
| --- | --- |
| [linear](layout_linear.c) | Consecutive address ranges on members. |
| [RAID0](layout_raid0.c) | Chunks striped across members. |
| [RAID1](layout_raid1.c) | Writes to participating mirrors; reads from an up-to-date copy. |

The RAID0 chunk size and each member's usable capacity are multiples of the cell size.
