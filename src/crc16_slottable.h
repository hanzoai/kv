#ifndef _CRC16_TABLE_H__
#define _CRC16_TABLE_H__

/* A table of the shortest possible alphanumeric string that is mapped by crc16
 * to any given cluster slot.
 *
 * The array indexes are slot numbers, so that given a desired slot, this string
 * is guaranteed to make the cluster route a request to the shard holding it.
 *
 * The table itself lives in crc16_slottable.c — this header only declares it.
 * It used to carry a second full copy of the definition, which every including
 * translation unit then re-defined.
 */
typedef char crc16_alphastring[4];

extern const crc16_alphastring crc16_slot_table[];

#endif
