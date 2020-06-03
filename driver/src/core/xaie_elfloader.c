/******************************************************************************
* Copyright (C) 2019 - 2020 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/


/*****************************************************************************/
/**
* @file xaie_elfloader.c
* @{
*
* The file has implementations of routines for elf loading.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who     Date     Changes
* ----- ------  -------- -----------------------------------------------------
* 1.0   Tejus   09/24/2019  Initial creation
* 1.1   Tejus	01/04/2020  Cleanup error messages
* 1.2   Tejus   03/20/2020  Make internal functions static
* 1.3   Tejus   04/13/2020  Remove range apis and change to single tile apis
* 1.4   Tejus   05/26/2020  Remove elf loader implementation for refactoring
* 1.5   Tejus   05/26/2020  Implement elf loader using program sections.
* 1.6   Tejus   06/03/2020  Fix compilation error for simulation.
* </pre>
*
******************************************************************************/
/***************************** Include Files *********************************/
#include "xaie_elfloader.h"

/************************** Constant Definitions *****************************/
/************************** Function Definitions *****************************/
/*****************************************************************************/
/**
*
* The function prints the content of the elf header.
*
* @param	ElfHdr: Pointer to the elf header.
*
* @return	None.
*
* @note		Internal API only.
*
*******************************************************************************/
static void _XAie_PrintElfHdr(const Elf32_Ehdr *Ehdr)
{
	XAieLib_print("**** ELF HEADER ****\n");
	XAieLib_print("e_type\t\t: 0x%08x\n", Ehdr->e_type);
	XAieLib_print("e_machine\t: 0x%08x\n", Ehdr->e_machine);
	XAieLib_print("e_version\t: 0x%08x\n", Ehdr->e_version);
	XAieLib_print("e_entry\t\t: 0x%08x\n", Ehdr->e_entry);
	XAieLib_print("e_phoff\t\t: 0x%08x\n", Ehdr->e_phoff);
	XAieLib_print("e_shoff\t\t: 0x%08x\n", Ehdr->e_shoff);
	XAieLib_print("e_flags\t\t: 0x%08x\n", Ehdr->e_flags);
	XAieLib_print("e_ehsize\t: 0x%08x\n", Ehdr->e_ehsize);
	XAieLib_print("e_phentsize\t: 0x%08x\n", Ehdr->e_phentsize);
	XAieLib_print("e_phnum\t\t: 0x%08x\n", Ehdr->e_phnum);
	XAieLib_print("e_shentsize\t: 0x%08x\n", Ehdr->e_shentsize);
	XAieLib_print("e_shnum\t\t: 0x%08x\n", Ehdr->e_shnum);
	XAieLib_print("e_shstrndx\t: 0x%08x\n", Ehdr->e_shstrndx);
}

/*****************************************************************************/
/**
*
* The function prints the content of the program header.
*
* @param	Phdr: Pointer to the program section header.
*
* @return	None.
*
* @note		Internal API only.
*
*******************************************************************************/
static void _XAie_PrintProgSectHdr(const Elf32_Phdr *Phdr)
{
	XAieLib_print("**** PROGRAM HEADER ****\n");
	XAieLib_print("p_type\t\t: 0x%08x\n", Phdr->p_type);
	XAieLib_print("p_offset\t: 0x%08x\n", Phdr->p_offset);
	XAieLib_print("p_vaddr\t\t: 0x%08x\n", Phdr->p_vaddr);
	XAieLib_print("p_paddr\t\t: 0x%08x\n", Phdr->p_paddr);
	XAieLib_print("p_filesz\t: 0x%08x\n", Phdr->p_filesz);
	XAieLib_print("p_memsz\t\t: 0x%08x\n", Phdr->p_memsz);
	XAieLib_print("p_flags\t\t: 0x%08x\n", Phdr->p_flags);
	XAieLib_print("p_align\t\t: 0x%08x\n", Phdr->p_align);
}

/*****************************************************************************/
/**
*
* This function is used to get the target tile location from Host's perspective
* based on the physical address of the data memory from the device's
* perspective.
*
* @param	DevInst: Device Instance.
* @param	Loc: Location specified by the user.
* @param	Addr: Physical Address from Device's perspective.
* @param	TgtLoc: Poiner to the target location based on physical address.
*
* @return	XAIE_OK on success and error code for failure.
*
* @note		Internal API only.
*
*******************************************************************************/
static AieRC _XAie_GetTargetTileLoc(XAie_DevInst *DevInst, XAie_LocType Loc,
		u32 Addr, XAie_LocType *TgtLoc)
{
	u8 CardDir;
	u8 RowParity;
	u8 TileType;
	const XAie_CoreMod *CoreMod;

	CoreMod = DevInst->DevProp.DevMod[XAIEGBL_TILE_TYPE_AIETILE].CoreMod;

	/*
	 * Find the cardinal direction and get tile address.
	 * CardDir can have values of 4, 5, 6 or 7 for valid data memory
	 * addresses..
	 */
	CardDir = Addr / CoreMod->DataMemSize;

	RowParity = Loc.Row % 2U;
	/*
	 * Checkerboard architecture is valid for AIE. Force RowParity to 1
	 * otherwise.
	 */
	if(CoreMod->IsCheckerBoard == 0U) {
		RowParity = 1U;
	}

	switch(CardDir) {
	case 4U:
		/* South */
		Loc.Row -= 1U;
		break;
	case 5U:
		/*
		 * West - West I/F could be the same tile or adjacent
		 * tile based on the row number
		 */
		if(RowParity == 1U) {
			/* Adjacent tile */
			Loc.Col -= 1U;
		}
		break;
	case 6U:
		/* North */
		Loc.Row += 1U;
		break;
	case 7U:
		/*
		 * East - East I/F could be the same tile or adjacent
		 * tile based on the row number
		 */
		if(RowParity == 0U) {
			/* Adjacent tile */
			Loc.Col += 1U;
		}
		break;
	default:
		/* Invalid CardDir */
		XAieLib_print("Error: Invalid address - 0x%x\n", Addr);
		return XAIE_ERR;
	}

	/* Return errors if modified rows and cols are invalid */
	if(Loc.Row >= DevInst->NumRows || Loc.Col >= DevInst->NumCols) {
		XAieLib_print("Error: Target row/col out of range\n");
		return XAIE_ERR;
	}

	TileType = _XAie_GetTileTypefromLoc(DevInst, Loc);
	if(TileType != XAIEGBL_TILE_TYPE_AIETILE) {
		XAieLib_print("Error: Invalid tile type for address\n");
		return XAIE_ERR;
	}

	TgtLoc->Row = Loc.Row;
	TgtLoc->Col = Loc.Col;

	return XAIE_OK;
}

/*****************************************************************************/
/**
*
* This routine is used to write to the specified program section by reading the
* corresponding data from the ELF buffer.
*
* @param	DevInst: Device Instance.
* @param	Loc: Starting location of the section.
* @param	ProgSec: Poiner to the program section entry in the ELF buffer.
* @param	ElfPtr: Pointer to the program header.
*
* @return	XAIE_OK on success and error code for failure.
*
* @note		Internal API only.
*
*******************************************************************************/
static AieRC _XAie_WriteProgramSection(XAie_DevInst *DevInst, XAie_LocType Loc,
		const unsigned char *ProgSec, const Elf32_Phdr *Phdr)
{
	AieRC RC;
	u32 OverFlowBytes;
	u32 BytesToWrite;
	u32 SectionAddr;
	u32 SectionSize;
	u32 AddrMask;
	u64 Addr;
	XAie_LocType TgtLoc;
	const XAie_CoreMod *CoreMod;

	CoreMod = DevInst->DevProp.DevMod[XAIEGBL_TILE_TYPE_AIETILE].CoreMod;

	/* Write to Program Memory */
	if(Phdr->p_paddr < CoreMod->ProgMemSize) {
		if((Phdr->p_paddr + Phdr->p_memsz) > CoreMod->ProgMemSize) {
			XAieLib_print("Error: Overflow of program memory\n");
			return XAIE_INVALID_ELF;
		}

		Addr = DevInst->BaseAddr + CoreMod->ProgMemHostOffset +
			Phdr->p_paddr +
			_XAie_GetTileAddr(DevInst, Loc.Row, Loc.Col);

		for(u32 i = 0U; i < Phdr->p_memsz; i += 4U) {
			XAieGbl_Write32(Addr + i, *((u32 *)(ProgSec + i)));
		}

		return XAIE_OK;
	}

	/* Check if section can access out of bound memory location on device */
	if(((Phdr->p_paddr > CoreMod->ProgMemSize) &&
			(Phdr->p_paddr < CoreMod->DataMemAddr)) ||
			((Phdr->p_paddr + Phdr->p_memsz) >
			 (CoreMod->DataMemAddr + CoreMod->DataMemSize * 4U))) {
		XAieLib_print("Error: Invalid section starting at 0x%x\n",
				Phdr->p_paddr);
		return XAIE_INVALID_ELF;
	}

	/* Write initialized section to data memory */
	SectionSize = Phdr->p_filesz;
	SectionAddr = Phdr->p_paddr;
	AddrMask = CoreMod->DataMemSize - 1U;
	while(SectionSize > 0U) {
		RC = _XAie_GetTargetTileLoc(DevInst, Loc, SectionAddr, &TgtLoc);
		if(RC != XAIE_OK) {
			XAieLib_print("Error: Failed to get target location "\
					"for p_paddr 0x%x\n", SectionAddr);
			return RC;
		}

		/*Bytes to write in this section */
		OverFlowBytes = 0U;
		if((SectionAddr & AddrMask) + SectionSize >
				CoreMod->DataMemSize) {
			OverFlowBytes = (SectionAddr & AddrMask) + SectionSize -
				CoreMod->DataMemSize;
		}

		BytesToWrite = SectionSize - OverFlowBytes;
		Addr = DevInst->BaseAddr + (SectionAddr & AddrMask) +
			_XAie_GetTileAddr(DevInst, TgtLoc.Row, TgtLoc.Col);

		for(u32 i = 0; i < BytesToWrite; i += 4U) {
			XAieGbl_Write32(Addr + i, *((u32 *)ProgSec));
			ProgSec += 4U;
		}

		SectionSize -= BytesToWrite;
		SectionAddr += BytesToWrite;
	}

	/* Write un-initialized section to data memory */
	SectionSize = Phdr->p_memsz - Phdr->p_filesz;
	SectionAddr = Phdr->p_paddr + Phdr->p_filesz;
	while(SectionSize > 0U) {

		RC = _XAie_GetTargetTileLoc(DevInst, Loc, SectionAddr, &TgtLoc);
		if(RC != XAIE_OK) {
			XAieLib_print("Error: Failed to get target location "\
					"for p_paddr 0x%x\n", SectionAddr);
			return RC;
		}

		/*Bytes to write in this section */
		OverFlowBytes = 0U;
		if((SectionAddr & AddrMask) + SectionSize >
				CoreMod->DataMemSize) {
			OverFlowBytes = (SectionAddr & AddrMask) + SectionSize -
				CoreMod->DataMemSize;
		}

		BytesToWrite = SectionSize - OverFlowBytes;
		Addr = DevInst->BaseAddr + (SectionAddr & AddrMask) +
			_XAie_GetTileAddr(DevInst, TgtLoc.Row, TgtLoc.Col);

		for(u32 i = 0; i < BytesToWrite; i += 4U) {
			XAieGbl_Write32(Addr + i, 0U);
		}

		SectionSize -= BytesToWrite;
		SectionAddr += BytesToWrite;
	}

	return XAIE_OK;
}

/*****************************************************************************/
/**
*
* This function loads the elf from memory to the AIE Cores. The function writes
* 0 for the unitialized data section.
*
* @param	DevInst: Device Instance.
* @param	Loc: Location of AIE Tile.
* @param	ElfMem: Pointer to the Elf contents in memory.
* @param	ElfSz: Size of the elf pointed by *ElfMem
*
* @return	XAIE_OK on success and error code for failure.
*
* @note		None.
*
*******************************************************************************/
AieRC XAie_LoadElfMem(XAie_DevInst *DevInst, XAie_LocType Loc,
		const unsigned char* ElfMem, const u64 ElfSz)
{
	AieRC RC;
	Elf32_Ehdr *Ehdr;
	Elf32_Phdr *Phdr;
	const unsigned char *SectionPtr;
	u8 TileType;

	if((DevInst == XAIE_NULL) || (ElfMem == XAIE_NULL) ||
		(DevInst->IsReady != XAIE_COMPONENT_IS_READY)) {
		XAieLib_print("Error: Invalid arguments\n");
		return XAIE_INVALID_ARGS;
	}

	TileType = _XAie_GetTileTypefromLoc(DevInst, Loc);
	if(TileType != XAIEGBL_TILE_TYPE_AIETILE) {
		XAieLib_print("Error: Invalid tile type\n");
		return XAIE_INVALID_TILE;
	}

	Ehdr = (Elf32_Ehdr *) ElfMem;
	_XAie_PrintElfHdr(Ehdr);

	for(u8 phnum = 0U; phnum < Ehdr->e_phnum; phnum++) {
		Phdr = (Elf32_Phdr*) (ElfMem + sizeof(*Ehdr) +
			phnum * sizeof(*Phdr));
		_XAie_PrintProgSectHdr(Phdr);
		if(Phdr->p_type == PT_LOAD) {
			SectionPtr = ElfMem + Phdr->p_offset;
			RC = _XAie_WriteProgramSection(DevInst, Loc,
					SectionPtr, Phdr);
			if(RC != XAIE_OK) {
				return RC;
			}
		}
	}

	return XAIE_OK;
}

#ifdef __AIESIM__
/*****************************************************************************/
/**
*
* This routine sends the out of bound command to the sim to load symbols.
*
* @param	Loc: Location of AIE Tile.
* @param	ElfPtr: Path to the ELF file.
*
* @return	None.
*
* @note		None.
*
*******************************************************************************/
static void XAieSim_LoadSymbols(XAie_LocType Loc, const char *ElfPtr)
{
	XAieSim_WriteCmd(XAIESIM_CMDIO_CMD_LOADSYM, Loc.Col, Loc.Row, 0, 0,
			ElfPtr);
}
#endif

#ifdef __AIESIM__
/*****************************************************************************/
/**
*
* This is the routine to derive the stack start and end addresses from the
* specified map file. This function basically looks for the line
* <b><init_address>..<final_address> ( <num> items) : Stack</b> in the
* map file to derive the stack address range.
*
* @param	MapPtr: Path to the Map file.
* @param	StackSzPtr: Pointer to the stack range structure.
*
* @return	XAIESIM_SUCCESS on success, else XAIESIM_FAILURE.
*
* @note		None.
*
*******************************************************************************/
static u32 XAieSim_GetStackRange(const char *MapPtr,
		XAieSim_StackSz *StackSzPtr)
{
	FILE *Fd;
	uint8 buffer[200U];

	/*
	 * Read map file and look for line:
	 * <init_address>..<final_address> ( <num> items) : Stack
	 */
	StackSzPtr->start = 0xFFFFFFFFU;
	StackSzPtr->end = 0U;

	Fd = fopen(MapPtr, "r");
	if(Fd == NULL) {
		XAieLib_print("ERROR: Invalid Map file\n");
		return XAIESIM_FAILURE;
	}

	while(fgets(buffer, 200U, Fd) != NULL) {
		if(strstr(buffer, "items) : Stack") != NULL) {
			sscanf(buffer, "    0x%8x..0x%8x (%*s",
					&StackSzPtr->start, &StackSzPtr->end);
			break;
		}
	}

	fclose(Fd);

	if(StackSzPtr->start == 0xFFFFFFFFU) {
		return XAIESIM_FAILURE;
	} else {
		return XAIESIM_SUCCESS;
	}
}
#endif

/*****************************************************************************/
/**
*
* This function loads the elf from file to the AIE Cores. The function writes
* 0 for the unitialized data section.
*
* @param	DevInst: Device Instance.
* @param	Loc: Location of AIE Tile.
* @param	ElfPtr: Path to the elf file.
* @param	LoadSym: Load symbols from .map file. This argument is valid
*		when __AIESIM__ is defined.
*
* @return	XAIE_OK on success and error code for failure.
*
* @note		None.
*
*******************************************************************************/
AieRC XAie_LoadElf(XAie_DevInst *DevInst, XAie_LocType Loc, const char *ElfPtr,
		u8 LoadSym)
{
	FILE *Fd;
	int Ret;
	unsigned char *ElfMem;
	u8 TileType;
	u64 ElfSz;
	AieRC RC;

	if((DevInst == XAIE_NULL) ||
		(DevInst->IsReady != XAIE_COMPONENT_IS_READY)) {
		XAieLib_print("Error: Invalid device instance\n");
		return XAIE_INVALID_ARGS;
	}

	TileType = _XAie_GetTileTypefromLoc(DevInst, Loc);
	if(TileType != XAIEGBL_TILE_TYPE_AIETILE) {
		XAieLib_print("Error: Invalid tile type\n");
		return XAIE_INVALID_TILE;
	}

#ifdef __AIESIM__
	/*
	 * The code under this macro guard is used in simulation mode only.
	 * According to our understanding from tools team, this is critical for
	 * profiling an simulation. This code is retained as is from v1 except
	 * minor changes to priting error message.
	 */
	u32 Status;
	char MapPath[256U];
	XAieSim_StackSz StackSz;

	/* Get the stack range */
	strcpy(MapPath, ElfPtr);
	strcat(MapPath, ".map");
	Status = XAieSim_GetStackRange(MapPath, &StackSz);
	XAieLib_print("Stack start:%08x, end:%08x\n", StackSz.start,
			StackSz.end);
	if(Status != XAIESIM_SUCCESS) {
		XAieLib_print("Error: Stack range definition failed\n");
		return Status;
	}

	/* Send the stack range set command */
	XAieSim_WriteCmd(XAIESIM_CMDIO_CMD_SETSTACK, Loc.Col, Loc.Row,
			StackSz.start, StackSz.end, XAIE_NULL);

	/* Load symbols if enabled */
	if(LoadSym == XAIE_ENABLE) {
		XAieSim_LoadSymbols(Loc, ElfPtr);
	}
#endif
	Fd = fopen(ElfPtr, "r");
	if(Fd == XAIE_NULL) {
		XAieLib_print("Error: Unable to open elf file\n");
		return XAIE_INVALID_ELF;
	}

	/* Get the file size of the elf */
	Ret = fseek(Fd, 0L, SEEK_END);
	if(Ret != 0U) {
		XAieLib_print("Error: Failed to get end of file\n");
		return XAIE_INVALID_ELF;
	}

	ElfSz = ftell(Fd);
	rewind(Fd);
	XAieLib_print("LOG: Elf size is %ld bytes\n", ElfSz);

	/* Read entire elf file into memory */
	ElfMem = (unsigned char*) malloc(ElfSz);
	if(ElfMem == NULL) {
		XAieLib_print("Error: Memory allocation failed\n");
		return XAIE_ERR;
	}

	Ret = fread((void*)ElfMem, ElfSz, 1U, Fd);
	if(Ret == 0U) {
		XAieLib_print("Error: Failed to read Elf into memory\n");
		return XAIE_ERR;
	}

	fclose(Fd);

	RC = XAie_LoadElfMem(DevInst, Loc, ElfMem, ElfSz);
	if(RC != XAIE_OK) {
		free(ElfMem);
		return RC;
	}

	free(ElfMem);
	return XAIE_OK;
}

/** @} */
