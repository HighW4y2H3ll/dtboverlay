// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 */

#include <linux/kernel.h>
#include "fdt.h"
//#include "libfdt.h"
//#include "libfdt_internal.h"

#define uintptr_t u64
#define fdt32_t u32
#define fdt64_t u64

#ifndef FDT_ASSUME_MASK
#define FDT_ASSUME_MASK 0
#endif

/*
 * Defines assumptions which can be enabled. Each of these can be enabled
 * individually. For maximum safety, don't enable any assumptions!
 *
 * For minimal code size and no safety, use ASSUME_PERFECT at your own risk.
 * You should have another method of validating the device tree, such as a
 * signature or hash check before using libfdt.
 *
 * For situations where security is not a concern it may be safe to enable
 * ASSUME_SANE.
 */
enum {
	/*
	 * This does essentially no checks. Only the latest device-tree
	 * version is correctly handled. Inconsistencies or errors in the device
	 * tree may cause undefined behaviour or crashes. Invalid parameters
	 * passed to libfdt may do the same.
	 *
	 * If an error occurs when modifying the tree it may leave the tree in
	 * an intermediate (but valid) state. As an example, adding a property
	 * where there is insufficient space may result in the property name
	 * being added to the string table even though the property itself is
	 * not added to the struct section.
	 *
	 * Only use this if you have a fully validated device tree with
	 * the latest supported version and wish to minimise code size.
	 */
	ASSUME_PERFECT		= 0xff,

	/*
	 * This assumes that the device tree is sane. i.e. header metadata
	 * and basic hierarchy are correct.
	 *
	 * With this assumption enabled, normal device trees produced by libfdt
	 * and the compiler should be handled safely. Malicious device trees and
	 * complete garbage may cause libfdt to behave badly or crash. Truncated
	 * device trees (e.g. those only partially loaded) can also cause
	 * problems.
	 *
	 * Note: Only checks that relate exclusively to the device tree itself
	 * (not the parameters passed to libfdt) are disabled by this
	 * assumption. This includes checking headers, tags and the like.
	 */
	ASSUME_VALID_DTB	= 1 << 0,

	/*
	 * This builds on ASSUME_VALID_DTB and further assumes that libfdt
	 * functions are called with valid parameters, i.e. not trigger
	 * FDT_ERR_BADOFFSET or offsets that are out of bounds. It disables any
	 * extensive checking of parameters and the device tree, making various
	 * assumptions about correctness.
	 *
	 * It doesn't make sense to enable this assumption unless
	 * ASSUME_VALID_DTB is also enabled.
	 */
	ASSUME_VALID_INPUT	= 1 << 1,

	/*
	 * This disables checks for device-tree version and removes all code
	 * which handles older versions.
	 *
	 * Only enable this if you know you have a device tree with the latest
	 * version.
	 */
	ASSUME_LATEST		= 1 << 2,

	/*
	 * This assumes that it is OK for a failed addition to the device tree,
	 * due to lack of space or some other problem, to skip any rollback
	 * steps (such as dropping the property name from the string table).
	 * This is safe to enable in most circumstances, even though it may
	 * leave the tree in a sub-optimal state.
	 */
	ASSUME_NO_ROLLBACK	= 1 << 3,

	/*
	 * This assumes that the device tree components appear in a 'convenient'
	 * order, i.e. the memory reservation block first, then the structure
	 * block and finally the string block.
	 *
	 * This order is not specified by the device-tree specification,
	 * but is expected by libfdt. The device-tree compiler always created
	 * device trees with this order.
	 *
	 * This assumption disables a check in fdt_open_into() and removes the
	 * ability to fix the problem there. This is safe if you know that the
	 * device tree is correctly ordered. See fdt_blocks_misordered_().
	 */
	ASSUME_LIBFDT_ORDER	= 1 << 4,

	/*
	 * This assumes that libfdt itself does not have any internal bugs. It
	 * drops certain checks that should never be needed unless libfdt has an
	 * undiscovered bug.
	 *
	 * This can generally be considered safe to enable.
	 */
	ASSUME_LIBFDT_FLAWLESS	= 1 << 5,
};

/**
 * can_assume_() - check if a particular assumption is enabled
 *
 * @mask: Mask to check (ASSUME_...)
 * @return true if that assumption is enabled, else false
 */
static inline bool can_assume_(int mask)
{
	return FDT_ASSUME_MASK & mask;
}

/** helper macros for checking assumptions */
#define can_assume(_assume)	can_assume_(ASSUME_ ## _assume)

#define FDT_FIRST_SUPPORTED_VERSION	0x02
#define FDT_LAST_COMPATIBLE_VERSION 0x10
#define FDT_LAST_SUPPORTED_VERSION	0x11

/* Error codes: informative error codes */
#define FDT_ERR_NOTFOUND	1
	/* FDT_ERR_NOTFOUND: The requested node or property does not exist */
#define FDT_ERR_EXISTS		2
	/* FDT_ERR_EXISTS: Attempted to create a node or property which
	 * already exists */
#define FDT_ERR_NOSPACE		3
	/* FDT_ERR_NOSPACE: Operation needed to expand the device
	 * tree, but its buffer did not have sufficient space to
	 * contain the expanded tree. Use fdt_open_into() to move the
	 * device tree to a buffer with more space. */

/* Error codes: codes for bad parameters */
#define FDT_ERR_BADOFFSET	4
	/* FDT_ERR_BADOFFSET: Function was passed a structure block
	 * offset which is out-of-bounds, or which points to an
	 * unsuitable part of the structure for the operation. */
#define FDT_ERR_BADPATH		5
	/* FDT_ERR_BADPATH: Function was passed a badly formatted path
	 * (e.g. missing a leading / for a function which requires an
	 * absolute path) */
#define FDT_ERR_BADPHANDLE	6
	/* FDT_ERR_BADPHANDLE: Function was passed an invalid phandle.
	 * This can be caused either by an invalid phandle property
	 * length, or the phandle value was either 0 or -1, which are
	 * not permitted. */
#define FDT_ERR_BADSTATE	7
	/* FDT_ERR_BADSTATE: Function was passed an incomplete device
	 * tree created by the sequential-write functions, which is
	 * not sufficiently complete for the requested operation. */

/* Error codes: codes for bad device tree blobs */
#define FDT_ERR_TRUNCATED	8
	/* FDT_ERR_TRUNCATED: FDT or a sub-block is improperly
	 * terminated (overflows, goes outside allowed bounds, or
	 * isn't properly terminated).  */
#define FDT_ERR_BADMAGIC	9
	/* FDT_ERR_BADMAGIC: Given "device tree" appears not to be a
	 * device tree at all - it is missing the flattened device
	 * tree magic number. */
#define FDT_ERR_BADVERSION	10
	/* FDT_ERR_BADVERSION: Given device tree has a version which
	 * can't be handled by the requested operation.  For
	 * read-write functions, this may mean that fdt_open_into() is
	 * required to convert the tree to the expected version. */
#define FDT_ERR_BADSTRUCTURE	11
	/* FDT_ERR_BADSTRUCTURE: Given device tree has a corrupt
	 * structure block or other serious error (e.g. misnested
	 * nodes, or subnodes preceding properties). */
#define FDT_ERR_BADLAYOUT	12
	/* FDT_ERR_BADLAYOUT: For read-write functions, the given
	 * device tree has it's sub-blocks in an order that the
	 * function can't handle (memory reserve map, then structure,
	 * then strings).  Use fdt_open_into() to reorganize the tree
	 * into a form suitable for the read-write operations. */

/* "Can't happen" error indicating a bug in libfdt */
#define FDT_ERR_INTERNAL	13
	/* FDT_ERR_INTERNAL: libfdt has failed an internal assertion.
	 * Should never be returned, if it is, it indicates a bug in
	 * libfdt itself. */

/* Errors in device tree content */
#define FDT_ERR_BADNCELLS	14
	/* FDT_ERR_BADNCELLS: Device tree has a #address-cells, #size-cells
	 * or similar property with a bad format or value */

#define FDT_ERR_BADVALUE	15
	/* FDT_ERR_BADVALUE: Device tree has a property with an unexpected
	 * value. For example: a property expected to contain a string list
	 * is not NUL-terminated within the length of its value. */

#define FDT_ERR_BADOVERLAY	16
	/* FDT_ERR_BADOVERLAY: The device tree overlay, while
	 * correctly structured, cannot be applied due to some
	 * unexpected or missing value, property or node. */

#define FDT_ERR_NOPHANDLES	17
	/* FDT_ERR_NOPHANDLES: The device tree doesn't have any
	 * phandle available anymore without causing an overflow */

#define FDT_ERR_BADFLAGS	18
	/* FDT_ERR_BADFLAGS: The function was passed a flags field that
	 * contains invalid flags or an invalid combination of flags. */

#define FDT_ERR_ALIGNMENT	19
	/* FDT_ERR_ALIGNMENT: The device tree base address is not 8-byte
	 * aligned. */

#define FDT_ERR_MAX		19

/* constants */
#define FDT_MAX_PHANDLE 0xfffffffe

static inline uint32_t fdt32_ld(const fdt32_t *p)
{
	const u8 *bp = (const u8 *)p;

	return ((u32)bp[0] << 24)
		| ((u32)bp[1] << 16)
		| ((u32)bp[2] << 8)
		| bp[3];
}

#define fdt_get_header(fdt, field) \
	(fdt32_ld(&((const struct fdt_header *)(fdt))->field))
#define fdt_magic(fdt)			(fdt_get_header(fdt, magic))
#define fdt_totalsize(fdt)		(fdt_get_header(fdt, totalsize))
#define fdt_off_dt_struct(fdt)		(fdt_get_header(fdt, off_dt_struct))
#define fdt_off_dt_strings(fdt)		(fdt_get_header(fdt, off_dt_strings))
#define fdt_off_mem_rsvmap(fdt)		(fdt_get_header(fdt, off_mem_rsvmap))
#define fdt_version(fdt)		(fdt_get_header(fdt, version))
#define fdt_last_comp_version(fdt)	(fdt_get_header(fdt, last_comp_version))
#define fdt_boot_cpuid_phys(fdt)	(fdt_get_header(fdt, boot_cpuid_phys))
#define fdt_size_dt_strings(fdt)	(fdt_get_header(fdt, size_dt_strings))
#define fdt_size_dt_struct(fdt)		(fdt_get_header(fdt, size_dt_struct))

static int check_off_(u32 hdrsize, u32 totalsize, u32 off)
{
	return (off >= hdrsize) && (off <= totalsize);
}

static int check_block_(u32 hdrsize, u32 totalsize,
			u32 base, u32 size)
{
	if (!check_off_(hdrsize, totalsize, base))
		return 0; /* block start out of bounds */
	if ((base + size) < base)
		return 0; /* overflow */
	if (!check_off_(hdrsize, totalsize, base + size))
		return 0; /* block end out of bounds */
	return 1;
}

size_t fdt_header_size_(u32 version)
{
	if (version <= 1)
		return FDT_V1_SIZE;
	else if (version <= 2)
		return FDT_V2_SIZE;
	else if (version <= 3)
		return FDT_V3_SIZE;
	else if (version <= 16)
		return FDT_V16_SIZE;
	else
		return FDT_V17_SIZE;
}

size_t fdt_header_size(const void *fdt)
{
	return can_assume(LATEST) ? FDT_V17_SIZE :
		fdt_header_size_(fdt_version(fdt));
}

int fdt_check_header(const void *fdt)
{
	size_t hdrsize;

	/* The device tree must be at an 8-byte aligned address */
	if ((uintptr_t)fdt & 7)
		return -FDT_ERR_ALIGNMENT;

    printk("DEBUG 0");
	if (fdt_magic(fdt) != FDT_MAGIC)
		return -FDT_ERR_BADMAGIC;
    printk("DEBUG 1");
	if (!can_assume(LATEST)) {
		if ((fdt_version(fdt) < FDT_FIRST_SUPPORTED_VERSION)
		    || (fdt_last_comp_version(fdt) >
			FDT_LAST_SUPPORTED_VERSION))
			return -FDT_ERR_BADVERSION;
		if (fdt_version(fdt) < fdt_last_comp_version(fdt))
			return -FDT_ERR_BADVERSION;
	}
    printk("DEBUG 2");
	hdrsize = fdt_header_size(fdt);
    printk("DEBUG 3");
	if (!can_assume(VALID_DTB)) {

		if ((fdt_totalsize(fdt) < hdrsize)
		    || (fdt_totalsize(fdt) > INT_MAX))
			return -FDT_ERR_TRUNCATED;

		/* Bounds check memrsv block */
		if (!check_off_(hdrsize, fdt_totalsize(fdt),
				fdt_off_mem_rsvmap(fdt)))
			return -FDT_ERR_TRUNCATED;
	}
    printk("DEBUG 4");

	if (!can_assume(VALID_DTB)) {
		/* Bounds check structure block */
		if (!can_assume(LATEST) && fdt_version(fdt) < 17) {
			if (!check_off_(hdrsize, fdt_totalsize(fdt),
					fdt_off_dt_struct(fdt)))
				return -FDT_ERR_TRUNCATED;
		} else {
			if (!check_block_(hdrsize, fdt_totalsize(fdt),
					  fdt_off_dt_struct(fdt),
					  fdt_size_dt_struct(fdt)))
				return -FDT_ERR_TRUNCATED;
		}
    printk("DEBUG 5");

		/* Bounds check strings block */
		if (!check_block_(hdrsize, fdt_totalsize(fdt),
				  fdt_off_dt_strings(fdt),
				  fdt_size_dt_strings(fdt)))
			return -FDT_ERR_TRUNCATED;
	}

	return 0;
}

//const void *fdt_offset_ptr(const void *fdt, int offset, unsigned int len)
//{
//	unsigned int uoffset = offset;
//	unsigned int absoffset = offset + fdt_off_dt_struct(fdt);
//
//	if (offset < 0)
//		return NULL;
//
//	if (!can_assume(VALID_INPUT))
//		if ((absoffset < uoffset)
//		    || ((absoffset + len) < absoffset)
//		    || (absoffset + len) > fdt_totalsize(fdt))
//			return NULL;
//
//	if (can_assume(LATEST) || fdt_version(fdt) >= 0x11)
//		if (((uoffset + len) < uoffset)
//		    || ((offset + len) > fdt_size_dt_struct(fdt)))
//			return NULL;
//
//	return fdt_offset_ptr_(fdt, offset);
//}
//
//uint32_t fdt_next_tag(const void *fdt, int startoffset, int *nextoffset)
//{
//	const fdt32_t *tagp, *lenp;
//	uint32_t tag;
//	int offset = startoffset;
//	const char *p;
//
//	*nextoffset = -FDT_ERR_TRUNCATED;
//	tagp = fdt_offset_ptr(fdt, offset, FDT_TAGSIZE);
//	if (!can_assume(VALID_DTB) && !tagp)
//		return FDT_END; /* premature end */
//	tag = fdt32_to_cpu(*tagp);
//	offset += FDT_TAGSIZE;
//
//	*nextoffset = -FDT_ERR_BADSTRUCTURE;
//	switch (tag) {
//	case FDT_BEGIN_NODE:
//		/* skip name */
//		do {
//			p = fdt_offset_ptr(fdt, offset++, 1);
//		} while (p && (*p != '\0'));
//		if (!can_assume(VALID_DTB) && !p)
//			return FDT_END; /* premature end */
//		break;
//
//	case FDT_PROP:
//		lenp = fdt_offset_ptr(fdt, offset, sizeof(*lenp));
//		if (!can_assume(VALID_DTB) && !lenp)
//			return FDT_END; /* premature end */
//		/* skip-name offset, length and value */
//		offset += sizeof(struct fdt_property) - FDT_TAGSIZE
//			+ fdt32_to_cpu(*lenp);
//		if (!can_assume(LATEST) &&
//		    fdt_version(fdt) < 0x10 && fdt32_to_cpu(*lenp) >= 8 &&
//		    ((offset - fdt32_to_cpu(*lenp)) % 8) != 0)
//			offset += 4;
//		break;
//
//	case FDT_END:
//	case FDT_END_NODE:
//	case FDT_NOP:
//		break;
//
//	default:
//		return FDT_END;
//	}
//
//	if (!fdt_offset_ptr(fdt, startoffset, offset - startoffset))
//		return FDT_END; /* premature end */
//
//	*nextoffset = FDT_TAGALIGN(offset);
//	return tag;
//}
//
//int fdt_check_node_offset_(const void *fdt, int offset)
//{
//	if (!can_assume(VALID_INPUT)
//	    && ((offset < 0) || (offset % FDT_TAGSIZE)))
//		return -FDT_ERR_BADOFFSET;
//
//	if (fdt_next_tag(fdt, offset, &offset) != FDT_BEGIN_NODE)
//		return -FDT_ERR_BADOFFSET;
//
//	return offset;
//}
//
//int fdt_check_prop_offset_(const void *fdt, int offset)
//{
//	if (!can_assume(VALID_INPUT)
//	    && ((offset < 0) || (offset % FDT_TAGSIZE)))
//		return -FDT_ERR_BADOFFSET;
//
//	if (fdt_next_tag(fdt, offset, &offset) != FDT_PROP)
//		return -FDT_ERR_BADOFFSET;
//
//	return offset;
//}
//
//int fdt_next_node(const void *fdt, int offset, int *depth)
//{
//	int nextoffset = 0;
//	uint32_t tag;
//
//	if (offset >= 0)
//		if ((nextoffset = fdt_check_node_offset_(fdt, offset)) < 0)
//			return nextoffset;
//
//	do {
//		offset = nextoffset;
//		tag = fdt_next_tag(fdt, offset, &nextoffset);
//
//		switch (tag) {
//		case FDT_PROP:
//		case FDT_NOP:
//			break;
//
//		case FDT_BEGIN_NODE:
//			if (depth)
//				(*depth)++;
//			break;
//
//		case FDT_END_NODE:
//			if (depth && ((--(*depth)) < 0))
//				return nextoffset;
//			break;
//
//		case FDT_END:
//			if ((nextoffset >= 0)
//			    || ((nextoffset == -FDT_ERR_TRUNCATED) && !depth))
//				return -FDT_ERR_NOTFOUND;
//			else
//				return nextoffset;
//		}
//	} while (tag != FDT_BEGIN_NODE);
//
//	return offset;
//}
//
//int fdt_first_subnode(const void *fdt, int offset)
//{
//	int depth = 0;
//
//	offset = fdt_next_node(fdt, offset, &depth);
//	if (offset < 0 || depth != 1)
//		return -FDT_ERR_NOTFOUND;
//
//	return offset;
//}
//
//int fdt_next_subnode(const void *fdt, int offset)
//{
//	int depth = 1;
//
//	/*
//	 * With respect to the parent, the depth of the next subnode will be
//	 * the same as the last.
//	 */
//	do {
//		offset = fdt_next_node(fdt, offset, &depth);
//		if (offset < 0 || depth < 1)
//			return -FDT_ERR_NOTFOUND;
//	} while (depth > 1);
//
//	return offset;
//}
//
//const char *fdt_find_string_(const char *strtab, int tabsize, const char *s)
//{
//	int len = strlen(s) + 1;
//	const char *last = strtab + tabsize - len;
//	const char *p;
//
//	for (p = strtab; p <= last; p++)
//		if (memcmp(p, s, len) == 0)
//			return p;
//	return NULL;
//}
//
//int fdt_move(const void *fdt, void *buf, int bufsize)
//{
//	if (!can_assume(VALID_INPUT) && bufsize < 0)
//		return -FDT_ERR_NOSPACE;
//
//	FDT_RO_PROBE(fdt);
//
//	if (fdt_totalsize(fdt) > (unsigned int)bufsize)
//		return -FDT_ERR_NOSPACE;
//
//	memmove(buf, fdt, fdt_totalsize(fdt));
//	return 0;
//}
