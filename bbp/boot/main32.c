/*

Protected Mode initialization
=============================

This file contains initialization code protected mode (preparation to switch to 
long mode).

It does:
	* page setup and initialization

License (BSD-3)
===============

Copyright (c) 2013, Gusts 'gusC' Kaksis <gusts.kaksis@gmail.com>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "main32.h"
#include "../config.h"

/**
* Page table structures
*/
static pm_t *pml4; // a.k.a. PML4T
static pm_t *pml3; // a.k.a. PTD (page directory table)
static pm_t *pml2; // a.k.a. PD (page directory)
static pm_t *pml1; // a.k.a. PT (page table)
/**
* PML4 pointer (to be passed over to CR3)
* @see boot16.asm
*/
extern uint32 pml4_ptr32;
/**
* Set value in memory
* @param val - the value to set
* @param dest - destination address (pointer to destination buffer)
* @param len - length to fill
*/
static void mem_set(uint8 val, uint8 *dest, uint16 len){
	while(len--){
		*dest++ = val;
	}
}
/**
* Filter out memory map of unusable regions
* and sort them in ascending order
*/
static void sort_e820(e820map_t *mem_map){
	uint16 i = 0;
	// Do the bubble sort to make them in ascending order
	e820entry_t e;
	uint16 swapped  = 1;
	uint16 count = mem_map->size;
	while (count > 0 && swapped ){
		i = 0;
		swapped  = 0;
		while (i < count - 1){
			if (mem_map->entries[i].base > mem_map->entries[i + 1].base){
				e = mem_map->entries[i];
				mem_map->entries[i] = mem_map->entries[i + 1];
				mem_map->entries[i + 1] = e;
				swapped  = 1;
			}
			i ++;
		}
		count --;
	}
}
/**
* Get total available RAM
* @param mem_map - memory map structure
* @return total available memory in bytes
*/
static uint64 get_ram(e820map_t *mem_map){
	uint16 i;
	uint64 mem_size = 0;
	for (i = 0; i < mem_map->size; i ++){
		if (mem_map->entries[i].base + mem_map->entries[i].length > mem_size){
			mem_size = mem_map->entries[i].base + mem_map->entries[i].length;
		}
	}
	return mem_size;
}
/**
* Setup PML4 pages to enter Long Mode
* @param ammount - ammount of memory to map
*/
static void setup_pages(uint64 ammount){
	uint64 d;
	uint64 t;
	uint64 p;
	uint64 ptr;
	
	// Single page holds 4KB of RAM
	uint64 page_count = ammount / 4096;
	if (page_count % 4096 > 0){
		page_count ++;
	}
	// Single table entry holds 2MB of RAM
	uint64 table_count = page_count / 512;
	if (page_count % 512 > 0){
		table_count ++;
	}
	// Single directory entry holds 1GB of RAM
	uint64 dir_count = table_count / 512;
	if (table_count % 512 > 0){
		dir_count ++;
	}

	// To map 2GB of RAM you'll need
	// 524288 - page entries
	// 1024 - table entries
	// 2 - directory entries
	// Single PML4 entry is enough to map 512GB of RAM

	// To map 256MB of RAM
	// 65536 - page entries
	// 128 - table entries
	// 1 - directory entry

	// Position the page structures in memory
	pml4 = (pm_t*)PT_LOC; // @0x00100000
	pml3 = (pm_t*)(((uint32)pml4) + (sizeof(pm_t) * 512)); // @0x00101000
	pml2 = (pm_t*)(((uint32)pml3) + (sizeof(pm_t) * 512)); // @0x00102000
	pml1 = (pm_t*)(((uint32)pml2) + (sizeof(pm_t) * 512 * (uint32)dir_count)); // @0x00103000
	
	// Clear memory region where the page tables will reside
	mem_set(0, (uint8 *)pml4, sizeof(pm_t) * 512);
	mem_set(0, (uint8 *)pml3, sizeof(pm_t) * 512);
	mem_set(0, (uint8 *)pml2, sizeof(pm_t) * 512 * dir_count);
	mem_set(0, (uint8 *)pml1, sizeof(pm_t) * 512 * table_count);

	// Set up pages, tables and directories
	for (p = 0; p < page_count; p ++){
		pml1[p].raw = (p * 4096) & PAGE_MASK;
		pml1[p].s.present = 1;
		pml1[p].s.writable = 1;
	}
	for (t = 0; t < table_count; t ++){
		ptr = (uint64)(((uint32)pml1) + (sizeof(pm_t) * 512 * t));
		pml2[t].raw = ptr & PAGE_MASK;
		pml2[t].s.present = 1;
		pml2[t].s.writable = 1;
	}
	for (d = 0; d < dir_count; d ++){
		ptr = (uint64)(((uint32)pml2) + (sizeof(pm_t) * 512 * d));
		pml3[d].raw = ptr & PAGE_MASK;
		pml3[d].s.present = 1;
		pml3[d].s.writable = 1;
	}
	// Point first PML4 entry to PML3 table
	ptr = (uint64)((uint32)pml3);
	pml4[0].raw = ptr & PAGE_MASK;
	pml4[0].s.present = 1;
	pml4[0].s.writable = 1;
	
	// Set PML4 pointer address
	pml4_ptr32 = (uint32)pml4;
}

/**
* Initialize Protected Mode
*/
void main32(){
	
#if DEBUG == 1 && VIDEOMODE == 1
	// Print out the first visible message
	char *vm = (char *)VIDEOMEM_LOC;
	vm[0] = 'P';
	vm[1] = 0x05;
	vm[2] = 'M';
	vm[3] = 0x05;
#endif

	e820map_t *mem_map = (e820map_t *)E820_LOC;
	// Sort memory map
	sort_e820(mem_map);
	// Get total available ram
	uint64 ram_total = get_ram(mem_map);
	// Setup Page tables
	setup_pages(ram_total);
}
