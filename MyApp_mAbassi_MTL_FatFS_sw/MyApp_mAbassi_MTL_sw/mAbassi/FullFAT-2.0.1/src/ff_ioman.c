/*****************************************************************************
 *     FullFAT - High Performance, Thread-Safe Embedded FAT File-System      *
 *                                                                           *
 *        Copyright(C) 2009  James Walmsley  <james@fullfat-fs.co.uk>        *
 *        Copyright(C) 2011  Hein Tibosch    <hein_tibosch@yahoo.es>         *
 *                                                                           *
 *    See RESTRICTIONS.TXT for extra restrictions on the use of FullFAT.     *
 *                                                                           *
 *    WARNING : COMMERCIAL PROJECTS MUST COMPLY WITH THE GNU GPL LICENSE.    *
 *                                                                           *
 *  Projects that cannot comply with the GNU GPL terms are legally obliged   *
 *    to seek alternative licensing. Contact James Walmsley for details.     *
 *                                                                           *
 *****************************************************************************
 *           See http://www.fullfat-fs.co.uk/ for more information.          *
 *****************************************************************************
 *  This program is free software: you can redistribute it and/or modify     *
 *  it under the terms of the GNU General Public License as published by     *
 *  the Free Software Foundation, either version 3 of the License, or        *
 *  (at your option) any later version.                                      *
 *                                                                           *
 *  This program is distributed in the hope that it will be useful,          *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU General Public License for more details.                             *
 *                                                                           *
 *  You should have received a copy of the GNU General Public License        *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.    *
 *                                                                           *
 *  The Copyright of Hein Tibosch on this project recognises his efforts in  *
 *  contributing to this project. The right to license the project under     *
 *  any other terms (other than the GNU GPL license) remains with the        *
 *  original copyright holder (James Walmsley) only.                         *
 *                                                                           *
 *****************************************************************************
 *  Modification/Extensions/Bugfixes/Improvements to FullFAT must be sent to *
 *  James Walmsley for integration into the main development branch.         *
 *****************************************************************************/

/**
 *	@file		ff_ioman.c
 *	@author		James Walmsley
 *	@ingroup	IOMAN
 *
 *	@defgroup	IOMAN	I/O Manager
 *	@brief		Handles IO buffers for FullFAT safely.
 *
 *	Provides a simple static interface to the rest of FullFAT to manage
 *	buffers. It also defines the public interfaces for Creating and
 *	Destroying a FullFAT IO object.
 **/

#include <time.h>
#include <string.h>

#include "ff_ioman.h"	// Includes ff_types.h, ff_safety.h, <stdio.h>
#include "ff_fatdef.h"
#include "ff_crc.h"
#include "ff_fat.h"

static void FF_IOMAN_InitBufferDescriptors(FF_IOMAN *pIoman);

/**
 *	@public
 *	@brief	Creates an FF_IOMAN object, to initialise FullFAT
 *
 *	@param	pCacheMem		Pointer to a buffer for the cache. (NULL if ok to Malloc).
 *	@param	Size			The size of the provided buffer, or size of the cache to be created. (Must be atleast 2 * BlkSize). Always a multiple of BlkSize.
 *	@param	BlkSize			The block size of devices to be attached. If in doubt use 512.
 *	@param	pError			Pointer to a signed byte for error checking. Can be NULL if not required.
 *	@param	pError			To be checked when a NULL pointer is returned.
 *
 *	@return	Returns a pointer to an FF_IOMAN type object. NULL on Error, check the contents of
 *	@return pError
 **/
FF_IOMAN *FF_CreateIOMAN(FF_T_UINT8 *pCacheMem, FF_T_UINT32 Size, FF_T_UINT16 BlkSize, FF_ERROR *pError) {

	FF_IOMAN	*pIoman = NULL;
	FF_T_UINT32 *pLong	= NULL;
#ifdef FF_HASH_CACHE
	FF_T_UINT i;
#endif

	if(pError) {
		*pError = FF_ERR_NONE;
	}

	if((BlkSize % 512) != 0 || BlkSize == 0) {
		if(pError) {
			*pError = FF_ERR_IOMAN_BAD_BLKSIZE | FF_CREATEIOMAN;
		}
		return NULL;	// BlkSize Size not a multiple of 512 > 0
	}

	if((Size % BlkSize) != 0 || Size == 0 || Size == BlkSize) {  // Size must now be atleast 2 * BlkSize (or a deadlock will occur).
		if(pError) {
			*pError = FF_ERR_IOMAN_BAD_MEMSIZE | FF_CREATEIOMAN;
		}
		return NULL;	// Memory Size not a multiple of BlkSize > 0
	}

	pIoman = (FF_IOMAN *) FF_MALLOC(sizeof(FF_IOMAN));

	if(!pIoman) {		// Ensure malloc() succeeded.
		if(pError) {
			*pError = FF_ERR_NOT_ENOUGH_MEMORY | FF_CREATEIOMAN;
		}
		return NULL;
	}

	memset (pIoman, '\0', sizeof(FF_IOMAN));

	// This is just a bit-mask, to use a byte to keep track of memory.
	// pIoman->MemAllocation = 0x00;	// Unset all allocation identifiers.
	pIoman->pPartition	= (FF_PARTITION  *) FF_MALLOC(sizeof(FF_PARTITION));
	if(!pIoman->pPartition) {
		if(pError) {
			*pError = FF_ERR_NOT_ENOUGH_MEMORY | FF_CREATEIOMAN;
		}
		FF_DestroyIOMAN(pIoman);
		return NULL;
	}
	memset (pIoman->pPartition, '\0', sizeof(FF_PARTITION));

	pIoman->MemAllocation |= FF_IOMAN_ALLOC_PART;	// If succeeded, flag that allocation.

#ifdef FF_HASH_CACHE
	for(i = 0; i < FF_HASH_CACHE_DEPTH; i++) {
		pIoman->HashCache[i].pHashTable 	= FF_CreateHashTable();
		pIoman->HashCache[i].ulDirCluster 	= 0;
		pIoman->HashCache[i].ulMisses		= 100;
	}
#endif

	pIoman->pBlkDevice	= (FF_BLK_DEVICE *) FF_MALLOC(sizeof(FF_BLK_DEVICE));
	if(!pIoman->pBlkDevice) {	// If succeeded, flag that allocation.
		if(pError) {
			*pError = FF_ERR_NOT_ENOUGH_MEMORY | FF_CREATEIOMAN;
		}
		FF_DestroyIOMAN(pIoman);
		return NULL;
	}
	memset (pIoman->pBlkDevice, '\0', sizeof(FF_BLK_DEVICE));
	pIoman->MemAllocation |= FF_IOMAN_ALLOC_BLKDEV;

	// Make sure all pointers are NULL
	// ...have been nulled by previous memset()
//	pIoman->pBlkDevice->fnReadBlocks = NULL;
//	pIoman->pBlkDevice->fnWriteBlocks = NULL;
//	pIoman->pBlkDevice->pParam = NULL;

	// Organise the memory provided, or create our own!
	if(pCacheMem) {
		pIoman->pCacheMem = pCacheMem;
	}else {	// No-Cache buffer provided (malloc)
		pLong = (FF_T_UINT32 *) FF_MALLOC(Size);
		pIoman->pCacheMem = (FF_T_UINT8 *) pLong;
		if(!pIoman->pCacheMem) {
			if(pError) {
				*pError = FF_ERR_NOT_ENOUGH_MEMORY | FF_CREATEIOMAN;
			}
			FF_DestroyIOMAN(pIoman);
			return NULL;
		}
		pIoman->MemAllocation |= FF_IOMAN_ALLOC_BUFFERS;

	}
	memset (pIoman->pCacheMem, '\0', Size);

	pIoman->BlkSize		 = BlkSize;
	pIoman->CacheSize	 = (FF_T_UINT16) (Size / BlkSize);
//	pIoman->FirstFile	 = NULL;
//	pIoman->Locks		 = 0;

	/*	Malloc() memory for buffer objects. (FullFAT never refers to a buffer directly
		but uses buffer objects instead. Allows us to provide thread safety.
	*/
	pIoman->pBuffers = (FF_BUFFER *) FF_MALLOC(sizeof(FF_BUFFER) * pIoman->CacheSize);

	if(!pIoman->pBuffers) {
		if(pError) {
			*pError = FF_ERR_NOT_ENOUGH_MEMORY | FF_CREATEIOMAN;
		}
		FF_DestroyIOMAN(pIoman);
		return NULL;	// HT added
	}
	memset (pIoman->pBuffers, '\0', sizeof(FF_BUFFER) * pIoman->CacheSize);

	pIoman->MemAllocation |= FF_IOMAN_ALLOC_BUFDESCR;
	FF_IOMAN_InitBufferDescriptors(pIoman);

	// Finally create a Semaphore for Buffer Description modifications.
	pIoman->pSemaphore = FF_CreateSemaphore();

#ifdef FF_BLKDEV_USES_SEM
	pIoman->pBlkDevSemaphore = FF_CreateSemaphore();
#endif

	return pIoman;	// Sucess, return the created object.
}

/**
 *	@public
 *	@brief	Destroys an FF_IOMAN object, and frees all assigned memory.
 *
 *	@param	pIoman	Pointer to an FF_IOMAN object, as returned from FF_CreateIOMAN.
 *
 *	@return	FF_ERR_NONE on sucess, or a documented error code on failure. (FF_ERR_NULL_POINTER)
 *
 **/
FF_ERROR FF_DestroyIOMAN(FF_IOMAN *pIoman) {

#ifdef FF_HASH_CACHE
	FF_T_UINT32 i;
#endif

	// Ensure no NULL pointer was provided.
	if(!pIoman) {
		return FF_ERR_NULL_POINTER | FF_DESTROYIOMAN;
	}

	// Ensure pPartition pointer was allocated.
	if((pIoman->MemAllocation & FF_IOMAN_ALLOC_PART)) {
		FF_FREE(pIoman->pPartition);
	}

	// Ensure pBlkDevice pointer was allocated.
	if((pIoman->MemAllocation & FF_IOMAN_ALLOC_BLKDEV)) {
		FF_FREE(pIoman->pBlkDevice);
	}

	// Ensure pBuffers pointer was allocated.
	if((pIoman->MemAllocation & FF_IOMAN_ALLOC_BUFDESCR)) {
		FF_FREE(pIoman->pBuffers);
	}

	// Ensure pCacheMem pointer was allocated.
	if((pIoman->MemAllocation & FF_IOMAN_ALLOC_BUFFERS)) {
		FF_FREE(pIoman->pCacheMem);
	}

	// Destroy any Semaphore that was created.
	if(pIoman->pSemaphore) {
		FF_DestroySemaphore(pIoman->pSemaphore);
	}
#ifdef FF_BLKDEV_USES_SEM
	if(pIoman->pBlkDevSemaphore) {
		FF_DestroySemaphore(pIoman->pBlkDevSemaphore);
	}
#endif

	// Destroy HashCache
#ifdef FF_HASH_CACHE
	for(i = 0; i < FF_HASH_CACHE_DEPTH; i++) {
		FF_DestroyHashTable(pIoman->HashCache[i].pHashTable);
	}
#endif

	// Finally free the FF_IOMAN object.
	FF_FREE(pIoman);

	return FF_ERR_NONE;
}

/**
 *	@private
 *	@brief	Initialises Buffer Descriptions as part of the FF_IOMAN object initialisation.
 *
 *	@param	pIoman		IOMAN Object.
 *
 **/
static void FF_IOMAN_InitBufferDescriptors(FF_IOMAN *pIoman) {
	FF_T_UINT16 i;
	FF_BUFFER *pBuffer = pIoman->pBuffers;
	pIoman->LastReplaced = 0;
	// HT : it is assmued that pBuffer was cleared by memset ()
	for(i = 0; i < pIoman->CacheSize; i++) {
		pBuffer->pBuffer = (FF_T_UINT8 *)((pIoman->pCacheMem) + (pIoman->BlkSize * i));
		pBuffer++;
	}
}


/**
 *	@private
 *	@brief		Flushes all Write cache buffers with no active Handles.
 *
 *	@param		pIoman	IOMAN Object.
 *
 *	@return		FF_ERR_NONE on Success.
 **/
FF_ERROR FF_FlushCache(FF_IOMAN *pIoman) {

	FF_T_UINT16 i,x;

	if(!pIoman) {
		return FF_ERR_NULL_POINTER | FF_FLUSHCACHE;
	}

	FF_PendSemaphore(pIoman->pSemaphore);
	{
		for(i = 0; i < pIoman->CacheSize; i++) {
			if((pIoman->pBuffers + i)->NumHandles == 0 && (pIoman->pBuffers + i)->Modified == FF_TRUE) {

				FF_BlockWrite(pIoman, (pIoman->pBuffers + i)->Sector, 1, (pIoman->pBuffers + i)->pBuffer, FF_TRUE);

				// Buffer has now been flushed, mark it as a read buffer and unmodified.
				(pIoman->pBuffers + i)->Mode = FF_MODE_READ;
				(pIoman->pBuffers + i)->Modified = FF_FALSE;

				// Search for other buffers that used this sector, and mark them as modified
				// So that further requests will result in the new sector being fetched.
				for(x = 0; x < pIoman->CacheSize; x++) {
					if(x != i) {
						if((pIoman->pBuffers + x)->Sector == (pIoman->pBuffers + i)->Sector && (pIoman->pBuffers + x)->Mode == FF_MODE_READ) {
							(pIoman->pBuffers + x)->Modified = FF_TRUE;
						}
					}
				}
			}
		}
	}
	FF_ReleaseSemaphore(pIoman->pSemaphore);

	return FF_ERR_NONE;
}

/*
	A new version of FF_GetBuffer() with a simple mechanism for timeout
*/

#define	FF_GETBUFFER_SLEEP_TIME	10
#define	FF_GETBUFFER_WAIT_TIME	(20000 / FF_GETBUFFER_SLEEP_TIME)

FF_BUFFER *FF_GetBuffer(FF_IOMAN *pIoman, FF_T_UINT32 Sector, FF_T_UINT8 Mode) {
	FF_BUFFER	*pBuffer;
	FF_BUFFER	*pBufLRU;
//	FF_BUFFER	*pBufLHITS = NULL;  // Wasn't use anymore?
	FF_BUFFER	*pBufMatch = NULL;
	FF_T_SINT32	RetVal;
	FF_T_INT    LoopCount = FF_GETBUFFER_WAIT_TIME;

	FF_T_INT cacheSize = pIoman->CacheSize;
	if (cacheSize <= 0) {
		return NULL;
	}

	while(!pBufMatch) {
		if (!--LoopCount) {
			//
			// *pError = FF_ERR_IOMAN_GETBUFFER_TIMEOUT;
			//
			return NULL;
		}
		FF_PendSemaphore(pIoman->pSemaphore);
		{

			for(pBuffer = pIoman->pBuffers; pBuffer < pIoman->pBuffers + cacheSize; pBuffer++) {
				if(pBuffer->Sector == Sector && pBuffer->Valid) {
					pBufMatch = pBuffer;
					break;	// Don't look further if you found a perfect match
				}
			}

			if(pBufMatch) {
				// A Match was found process!
				if(Mode == FF_MODE_READ && pBufMatch->Mode == FF_MODE_READ) {
					pBufMatch->NumHandles += 1;
					pBufMatch->Persistance += 1;
					break;
				}

				if(pBufMatch->NumHandles == 0) {
					pBufMatch->Mode = (Mode & FF_MODE_RD_WR);
					if((Mode & FF_MODE_WRITE) != 0) {	// This buffer has no attached handles.
						pBufMatch->Modified = FF_TRUE;
					}
					pBufMatch->NumHandles = 1;
					pBufMatch->Persistance += 1;
					break;
				}

				pBufMatch = NULL;	// Sector is already in use, keep yielding until its available!

			} else {
				pBufLRU   = NULL;	// So put them to NULL here

				for(pBuffer = pIoman->pBuffers; pBuffer < pIoman->pBuffers + cacheSize; pBuffer++) {
					if(pBuffer->NumHandles)
						continue;  // Occupied
					pBuffer->LRU += 1;

					if(!pBufLRU) {
						pBufLRU = pBuffer;
					}

					if(pBuffer->LRU > pBufLRU->LRU ||
					   (pBuffer->LRU == pBufLRU->LRU && pBuffer->Persistance > pBufLRU->Persistance)) {
						pBufLRU = pBuffer;
					}

				}
				// Choose a suitable buffer!
				if(pBufLRU) {
					// Process the suitable candidate.
					if(pBufLRU->Modified == FF_TRUE) {
						// Along with the FF_TRUE parameter to indicate semapahore has been claimed

						RetVal = FF_BlockWrite(pIoman, pBufLRU->Sector, 1, pBufLRU->pBuffer, FF_TRUE);
						if (RetVal < 0) {
							pBufMatch = NULL;
							break;
						}
					}
					if (Mode == FF_MODE_WR_ONLY) {
						memset (pBufLRU->pBuffer, '\0', pIoman->BlkSize);
					} else {
						RetVal = FF_BlockRead(pIoman, Sector, 1, pBufLRU->pBuffer, FF_TRUE);
						if (RetVal < 0) {
							pBufMatch = NULL;
							break;
						}
					}
					pBufLRU->Mode = (Mode & FF_MODE_RD_WR);
					pBufLRU->Persistance = 1;
					pBufLRU->LRU = 0;
					pBufLRU->NumHandles = 1;
					pBufLRU->Sector = Sector;

					pBufLRU->Modified = (Mode & FF_MODE_WRITE) != 0;

					pBufLRU->Valid = FF_TRUE;
					pBufMatch = pBufLRU;
					break;
				}

			}
		}
		FF_ReleaseSemaphore(pIoman->pSemaphore);
		// Better to go asleep to give low-priority task a chance to release buffer(s)
		FF_Sleep (FF_GETBUFFER_SLEEP_TIME);
	}	// while(!pBufMatch)
	FF_ReleaseSemaphore(pIoman->pSemaphore);

	return pBufMatch;	// Return the Matched Buffer!
}


/**
 *	@private
 *	@brief	Releases a buffer resource.
 *
 *	@param	pIoman	Pointer to an FF_IOMAN object.
 *	@param	pBuffer	Pointer to an FF_BUFFER object.
 *
 **/
FF_ERROR FF_ReleaseBuffer(FF_IOMAN *pIoman, FF_BUFFER *pBuffer) {
	FF_ERROR Error = FF_ERR_NONE;

	// Protect description changes with a semaphore.
	FF_PendSemaphore(pIoman->pSemaphore);
	{
		if (pBuffer->NumHandles) {
			pBuffer->NumHandles--;
		} else {
			//printf ("FF_ReleaseBuffer: buffer not claimed\n");
		}
#ifdef FF_CACHE_WRITE_THROUGH
		if(pBuffer->Modified == FF_TRUE) {
			Error = FF_BlockWrite(pIoman, pBuffer->Sector, 1, pBuffer->pBuffer, FF_TRUE);
			if(!FF_isERR(Error)) {				// Ensure if an error occurs its still possible to write the block again.
				pBuffer->Modified = FF_FALSE;
			}
		}
#endif
	}
	FF_ReleaseSemaphore(pIoman->pSemaphore);

	return Error;
}

/**
 *	@public
 *	@brief	Registers a device driver with FullFAT
 *
 *	The device drivers must adhere to the specification provided by
 *	FF_WRITE_BLOCKS and FF_READ_BLOCKS.
 *
 *	@param	pIoman			FF_IOMAN object.
 *	@param	BlkSize			Block Size that the driver deals in. (Minimum 512, larger values must be a multiple of 512).
 *	@param	fnWriteBlocks	Pointer to the Write Blocks to device function, as described by FF_WRITE_BLOCKS.
 *	@param	fnReadBlocks	Pointer to the Read Blocks from device function, as described by FF_READ_BLOCKS.
 *	@param	pParam			Pointer to a parameter for use in the functions.
 *
 *	@return	0 on success, FF_ERR_IOMAN_DEV_ALREADY_REGD if a device was already hooked, FF_ERR_IOMAN_NULL_POINTER if a pIoman object wasn't provided.
 **/
FF_ERROR FF_RegisterBlkDevice(FF_IOMAN *pIoman, FF_T_UINT16 BlkSize, FF_WRITE_BLOCKS fnWriteBlocks, FF_READ_BLOCKS fnReadBlocks, void *pParam) {
	if(!pIoman) {	// We can't do anything without an IOMAN object.
		return FF_ERR_NULL_POINTER | FF_REGISTERBLKDEVICE;
	}

	if((BlkSize % 512) != 0 || BlkSize == 0) {
		return FF_ERR_IOMAN_DEV_INVALID_BLKSIZE | FF_REGISTERBLKDEVICE;	// BlkSize Size not a multiple of IOMAN's Expected BlockSize > 0
	}

	if((BlkSize % pIoman->BlkSize) != 0 || BlkSize == 0) {
		return FF_ERR_IOMAN_DEV_INVALID_BLKSIZE | FF_REGISTERBLKDEVICE;	// BlkSize Size not a multiple of IOMAN's Expected BlockSize > 0
	}

	// Ensure that a device cannot be re-registered "mid-flight"
	// Doing so would corrupt the context of FullFAT
	if(pIoman->pBlkDevice->fnpReadBlocks) {
		return FF_ERR_IOMAN_DEV_ALREADY_REGD | FF_REGISTERBLKDEVICE;
	}
	if(pIoman->pBlkDevice->fnpWriteBlocks) {
		return FF_ERR_IOMAN_DEV_ALREADY_REGD | FF_REGISTERBLKDEVICE;
	}
	if(pIoman->pBlkDevice->pParam) {
		return FF_ERR_IOMAN_DEV_ALREADY_REGD | FF_REGISTERBLKDEVICE;
	}

	// Here we shall just set the values.
	// FullFAT checks before using any of these values.
	pIoman->pBlkDevice->devBlkSize		= BlkSize;
	pIoman->pBlkDevice->fnpReadBlocks	= fnReadBlocks;
	pIoman->pBlkDevice->fnpWriteBlocks	= fnWriteBlocks;
	pIoman->pBlkDevice->pParam			= pParam;

	return FF_ERR_NONE;	// Success
}

/*
	New Interface for FullFAT to read blocks.
*/

FF_T_SINT32 FF_BlockRead(FF_IOMAN *pIoman, FF_T_UINT32 ulSectorLBA, FF_T_UINT32 ulNumSectors, void *pBuffer, FF_T_BOOL aSemLocked) {
	FF_T_SINT32 slRetVal = 0;

	if(pIoman->pPartition->TotalSectors) {
		if((ulSectorLBA + ulNumSectors) > (pIoman->pPartition->TotalSectors + pIoman->pPartition->BeginLBA)) {
			return (FF_ERR_IOMAN_OUT_OF_BOUNDS_READ | FF_BLOCKREAD);
		}
	}

	if(pIoman->pBlkDevice->fnpReadBlocks) do {	// Make sure we don't execute a NULL.
#ifdef	FF_BLKDEV_USES_SEM
		if (!aSemLocked || pIoman->pSemaphore != pIoman->pBlkDevSemaphore)
			FF_PendSemaphore(pIoman->pBlkDevSemaphore);
#endif
		slRetVal = pIoman->pBlkDevice->fnpReadBlocks(pBuffer, ulSectorLBA, ulNumSectors, pIoman->pBlkDevice->pParam);
#ifdef	FF_BLKDEV_USES_SEM
		if (!aSemLocked || pIoman->pSemaphore != pIoman->pBlkDevSemaphore)
			FF_ReleaseSemaphore(pIoman->pBlkDevSemaphore);
#endif
		if(!FF_isERR(slRetVal) && FF_GETERROR(slRetVal) != FF_ERR_DRIVER_BUSY) {
			break;
		}
		FF_Sleep(FF_DRIVER_BUSY_SLEEP);
	} while (FF_TRUE);

	return slRetVal;
}

FF_T_SINT32 FF_BlockWrite(FF_IOMAN *pIoman, FF_T_UINT32 ulSectorLBA, FF_T_UINT32 ulNumSectors, void *pBuffer, FF_T_BOOL aSemLocked) {
	FF_T_SINT32 slRetVal = 0;

	if(pIoman->pPartition->TotalSectors) {
		if((ulSectorLBA + ulNumSectors) > (pIoman->pPartition->TotalSectors + pIoman->pPartition->BeginLBA)) {
			return (FF_ERR_IOMAN_OUT_OF_BOUNDS_WRITE | FF_BLOCKWRITE);
		}
	}

	if(pIoman->pBlkDevice->fnpWriteBlocks) do {	// Make sure we don't execute a NULL.
#ifdef	FF_BLKDEV_USES_SEM
		if (!aSemLocked || pIoman->pSemaphore != pIoman->pBlkDevSemaphore)
			FF_PendSemaphore(pIoman->pBlkDevSemaphore);
#endif
		slRetVal = pIoman->pBlkDevice->fnpWriteBlocks(pBuffer, ulSectorLBA, ulNumSectors, pIoman->pBlkDevice->pParam);
#ifdef	FF_BLKDEV_USES_SEM
		if (!aSemLocked || pIoman->pSemaphore != pIoman->pBlkDevSemaphore)
			FF_ReleaseSemaphore(pIoman->pBlkDevSemaphore);
#endif
		if(!FF_isERR(slRetVal) && FF_GETERROR(slRetVal) != FF_ERR_DRIVER_BUSY) {
			break;
		}
		FF_Sleep(FF_DRIVER_BUSY_SLEEP);
	} while (FF_TRUE);

	return slRetVal;
}


/**
 *	@private
 **/
static FF_ERROR FF_DetermineFatType(FF_IOMAN *pIoman) {

	FF_PARTITION	*pPart;
	FF_BUFFER		*pBuffer;
	FF_T_UINT32		testLong;
	FF_ERROR Error;
	if(pIoman) {
		pPart = pIoman->pPartition;

		if(pPart->NumClusters < 4085) {
			// FAT12
			pPart->Type = FF_T_FAT12;
#ifdef FF_FAT_CHECK
#ifdef FF_FAT12_SUPPORT
			pBuffer = FF_GetBuffer(pIoman, pIoman->pPartition->FatBeginLBA, FF_MODE_READ);
			{
				if(!pBuffer) {
					return FF_ERR_DEVICE_DRIVER_FAILED | FF_DETERMINEFATTYPE;
				}
				testLong = (FF_T_UINT32) FF_getShort(pBuffer->pBuffer, 0x0000);
			}
			Error = FF_ReleaseBuffer(pIoman, pBuffer);
			if(FF_isERR(Error)) {
				return Error;
			}
			if((testLong & 0x3FF) != 0x3F8) {
				return FF_ERR_IOMAN_NOT_FAT_FORMATTED | FF_DETERMINEFATTYPE;
			}
#else
			return FF_ERR_IOMAN_NOT_FAT_FORMATTED | FF_DETERMINEFATTYPE;
#endif
#endif
#ifdef FF_FAT12_SUPPORT
			return FF_ERR_NONE;
#endif
		} else if(pPart->NumClusters < 65525) {
			// FAT 16
			pPart->Type = FF_T_FAT16;
#ifdef FF_FAT_CHECK
			pBuffer = FF_GetBuffer(pIoman, pIoman->pPartition->FatBeginLBA, FF_MODE_READ);
			{
				if(!pBuffer) {
					return FF_ERR_DEVICE_DRIVER_FAILED | FF_DETERMINEFATTYPE;
				}
				testLong = (FF_T_UINT32) FF_getShort(pBuffer->pBuffer, 0x0000);
			}
			Error = FF_ReleaseBuffer(pIoman, pBuffer);
			if(FF_isERR(Error)) {
				return Error;
			}

			if(testLong == 0xFFF8)
#endif
				return FF_ERR_NONE;

		}
		else {
			// FAT 32!
			pPart->Type = FF_T_FAT32;
#ifdef FF_FAT_CHECK
			pBuffer = FF_GetBuffer(pIoman, pIoman->pPartition->FatBeginLBA, FF_MODE_READ);
			{
				if(!pBuffer) {
					return FF_ERR_DEVICE_DRIVER_FAILED | FF_DETERMINEFATTYPE;
				}
				testLong = FF_getLong(pBuffer->pBuffer, 0x0000);
			}
			Error = FF_ReleaseBuffer(pIoman, pBuffer);
			if(FF_isERR(Error)) {
				return Error;
			}

			if((testLong & 0x0FFFFFF8) != 0x0FFFFFF8 && (testLong & 0x0FFFFFF8) != 0x0FFFFFF0) {
				// HT:
				// I had an SD-card which worked well in Linux/W32
				// but FullFAT returned me this error
				// So for me I left out this check (just issue a warning for now)
				return FF_ERR_NONE; // FF_ERR_IOMAN_NOT_FAT_FORMATTED;
			}
#endif
			return FF_ERR_NONE;
		}
	}

	return FF_ERR_IOMAN_NOT_FAT_FORMATTED | FF_DETERMINEFATTYPE;
}

static FF_T_SINT8 FF_PartitionCount (FF_T_UINT8 *pBuffer)
{
	FF_T_SINT8 count = 0;
	FF_T_SINT8 part;
	// Check PBR or MBR signature
	if (FF_getChar(pBuffer, FF_FAT_MBR_SIGNATURE) != 0x55 &&
		FF_getChar(pBuffer, FF_FAT_MBR_SIGNATURE) != 0xAA ) {
		// No MBR, but is it a PBR ?
		if (FF_getChar(pBuffer, 0) == 0xEB &&          // PBR Byte 0
		    FF_getChar(pBuffer, 2) == 0x90 &&          // PBR Byte 2
		    (FF_getChar(pBuffer, 21) & 0xF0) == 0xF0) {// PBR Byte 21 : Media byte
			return 1;	// No MBR but PBR exist then only one partition
		}
		return 0;   // No MBR and no PBR then no partition found
	}
	for (part = 0; part < 4; part++)  {
		FF_T_UINT8 active = FF_getChar(pBuffer, FF_FAT_PTBL + FF_FAT_PTBL_ACTIVE + (16 * part));
		FF_T_UINT8 part_id = FF_getChar(pBuffer, FF_FAT_PTBL + FF_FAT_PTBL_ID + (16 * part));
		// The first sector must be a MBR, then check the partition entry in the MBR
		if (active != 0x80 && (active != 0 || part_id == 0)) {
			break;
		}
		count++;
	}
	return count;
}

/*
	Mount GPT Partition Tables
*/

#define FF_GPT_HEAD_ENTRY_SIZE			0x54
#define	FF_GPT_HEAD_TOTAL_ENTRIES		0x50
#define FF_GPT_HEAD_PART_ENTRY_LBA		0x48
#define FF_GPT_ENTRY_FIRST_SECTOR_LBA	0x20
#define FF_GPT_HEAD_CRC					0x10
#define FF_GPT_HEAD_LENGTH				0x0C

static FF_ERROR FF_GetEfiPartitionEntry(FF_IOMAN *pIoman, FF_T_UINT32 ulPartitionNumber) {
	// Continuing on from FF_MountPartition() pPartition->BeginLBA should be the sector of the GPT Header
	FF_BUFFER		*pBuffer;
	FF_PARTITION	*pPart = pIoman->pPartition;

	FF_T_UINT32		ulBeginGPT;
	FF_T_UINT32		ulEntrySector;
	FF_T_UINT32		ulSectorOffset;
	FF_T_UINT32		ulPartitionEntrySize;
	FF_T_UINT32		ulGPTHeadCRC, ulGPTCrcCheck, ulGPTHeadLength;

	FF_ERROR Error;

	if(ulPartitionNumber >= 128) {
		return FF_ERR_IOMAN_INVALID_PARTITION_NUM | FF_GETEFIPARTITIONENTRY;
	}

	pBuffer = FF_GetBuffer(pIoman, pPart->BeginLBA, FF_MODE_READ);
	{
		if(!pBuffer) {
			return FF_ERR_DEVICE_DRIVER_FAILED | FF_GETEFIPARTITIONENTRY;
		}

		// Verify this is an EFI header
		if(memcmp(pBuffer->pBuffer, "EFI PART", 8) != 0) {
			Error = FF_ReleaseBuffer(pIoman, pBuffer);	// Already returning an error, but this error would override the current one.
			if(FF_isERR(Error)) {
				return Error;
			}
			return FF_ERR_IOMAN_INVALID_FORMAT | FF_GETEFIPARTITIONENTRY;
		}

		ulBeginGPT					= FF_getLong(pBuffer->pBuffer, FF_GPT_HEAD_PART_ENTRY_LBA);

		ulPartitionEntrySize		= FF_getLong(pBuffer->pBuffer, FF_GPT_HEAD_ENTRY_SIZE);
		ulGPTHeadCRC				= FF_getLong(pBuffer->pBuffer, FF_GPT_HEAD_CRC);
		ulGPTHeadLength				= FF_getLong(pBuffer->pBuffer, FF_GPT_HEAD_LENGTH);

		// Calculate Head CRC

		// Blank CRC field
		FF_putLong(pBuffer->pBuffer, FF_GPT_HEAD_CRC, 0x00000000);

		// Calculate CRC
		ulGPTCrcCheck = FF_GetCRC32(pBuffer->pBuffer, ulGPTHeadLength);

		// Restore The CRC field
		FF_putLong(pBuffer->pBuffer, FF_GPT_HEAD_CRC, ulGPTHeadCRC);
	}
	Error = FF_ReleaseBuffer(pIoman, pBuffer);
	if(FF_isERR(Error)) {
		return Error;
	}

	// Check CRC
	if(ulGPTHeadCRC != ulGPTCrcCheck) {
		return FF_ERR_IOMAN_GPT_HEADER_CORRUPT | FF_GETEFIPARTITIONENTRY;
	}

	// Calculate Sector Containing the Partition Entry we want to use.

	ulEntrySector	= ((ulPartitionNumber * ulPartitionEntrySize) / pIoman->BlkSize) + ulBeginGPT;
	ulSectorOffset	= (ulPartitionNumber % (pIoman->BlkSize / ulPartitionEntrySize)) * ulPartitionEntrySize;

	pBuffer = FF_GetBuffer(pIoman, ulEntrySector, FF_MODE_READ);
	{
		if(!pBuffer) {
			return FF_ERR_DEVICE_DRIVER_FAILED | FF_GETEFIPARTITIONENTRY;
		}

		pPart->BeginLBA = FF_getLong(pBuffer->pBuffer, ulSectorOffset + FF_GPT_ENTRY_FIRST_SECTOR_LBA);
	}
	Error = FF_ReleaseBuffer(pIoman, pBuffer);
	if(FF_isERR(Error)) {
		return Error;
	}

	if(!pPart->BeginLBA) {
		return FF_ERR_IOMAN_INVALID_PARTITION_NUM | FF_GETEFIPARTITIONENTRY;
	}

	return FF_ERR_NONE;
}

/**
 *	@public
 *	@brief	Mounts the Specified partition, the volume specified by the FF_IOMAN object provided.
 *
 *	The device drivers must adhere to the specification provided by
 *	FF_WRITE_BLOCKS and FF_READ_BLOCKS.
 *
 *	@param	pIoman			FF_IOMAN object.
 *	@param	PartitionNumber	The primary partition number to be mounted. (0 - 3).
 *
 *	@return	0 on success.
 *	@return FF_ERR_NULL_POINTER if a pIoman object wasn't provided.
 *	@return FF_ERR_IOMAN_INVALID_PARTITION_NUM if the partition number is out of range.
 *	@return FF_ERR_IOMAN_NO_MOUNTABLE_PARTITION if no partition was found.
 *	@return FF_ERR_IOMAN_INVALID_FORMAT if the master boot record or partition boot block didn't provide sensible data.
 *	@return FF_ERR_IOMAN_NOT_FAT_FORMATTED if the volume or partition couldn't be determined to be FAT. (@see ff_config.h)
 *
 **/
FF_ERROR FF_MountPartition(FF_IOMAN *pIoman, FF_T_UINT8 PartitionNumber) {
	FF_PARTITION	*pPart;
	FF_BUFFER		*pBuffer = 0;
	FF_ERROR		Error;

	FF_T_UINT8		ucPartitionType;
#ifdef FF_HASH_CACHE
	FF_T_INT        i;
#endif
	int partCount;

	if(!pIoman) {
		return FF_ERR_NULL_POINTER | FF_MOUNTPARTITION;
	}

	/*if(PartitionNumber > 3) {
		return FF_ERR_IOMAN_INVALID_PARTITION_NUM;
	}*/

	pPart = pIoman->pPartition;

	memset (pIoman->pBuffers, '\0', sizeof(FF_BUFFER) * pIoman->CacheSize);
	memset (pIoman->pCacheMem, '\0', pIoman->BlkSize * pIoman->CacheSize);

#ifdef FF_HASH_CACHE
	for(i = 0; i < FF_HASH_CACHE_DEPTH; i++) {
		FF_ClearHashTable(pIoman->HashCache[i].pHashTable);
	}
#endif

	FF_IOMAN_InitBufferDescriptors(pIoman);
	pIoman->FirstFile = 0;

	pBuffer = FF_GetBuffer(pIoman, 0, FF_MODE_READ);
	if(!pBuffer) {
		return FF_ERR_DEVICE_DRIVER_FAILED | FF_MOUNTPARTITION;
	}

	partCount = FF_PartitionCount (pBuffer->pBuffer);

	pPart->BlkSize = FF_getShort(pBuffer->pBuffer, FF_FAT_BYTES_PER_SECTOR);

	if (partCount == 0) { //(pPart->BlkSize % 512) == 0 && pPart->BlkSize > 0) {
		// Volume is not partitioned (MBR Found)
		pPart->BeginLBA = 0;
	} else {

		ucPartitionType = FF_getChar(pBuffer->pBuffer, FF_FAT_PTBL + FF_FAT_PTBL_ID);	// Ensure its not an EFI partition!

		if(ucPartitionType != 0xEE) {

			if(PartitionNumber > 3) {
				Error = FF_ReleaseBuffer(pIoman, pBuffer);	// Already returning an error, but an error from ReleaseBuf would override it.
				if(FF_isERR(Error)) {
					return Error;
				}
				return FF_ERR_IOMAN_INVALID_PARTITION_NUM | FF_MOUNTPARTITION;
			}

			// Primary Partitions to deal with!
			pPart->BeginLBA = FF_getLong(pBuffer->pBuffer, FF_FAT_PTBL + FF_FAT_PTBL_LBA + (16 * PartitionNumber));
		}
		Error = FF_ReleaseBuffer(pIoman, pBuffer);
		if(FF_isERR(Error)) {
			return Error;
		}

		if(ucPartitionType == 0xEE) {
			pPart->BeginLBA = FF_getLong(pBuffer->pBuffer, FF_FAT_PTBL + FF_FAT_PTBL_LBA);
			Error = FF_GetEfiPartitionEntry(pIoman, PartitionNumber);

			if(FF_isERR(Error)) {
				return Error;
			}
		}

		if(!pPart->BeginLBA) {
			return FF_ERR_IOMAN_NO_MOUNTABLE_PARTITION | FF_MOUNTPARTITION;
		}
		// Now we get the Partition sector.
		pBuffer = FF_GetBuffer(pIoman, pPart->BeginLBA, FF_MODE_READ);
		if(!pBuffer) {
			return FF_ERR_DEVICE_DRIVER_FAILED | FF_MOUNTPARTITION;
		}
		pPart->BlkSize = FF_getShort(pBuffer->pBuffer, FF_FAT_BYTES_PER_SECTOR);
		if((pPart->BlkSize % 512) != 0 || pPart->BlkSize == 0) {
			Error = FF_ReleaseBuffer(pIoman, pBuffer);	// An error here should override the current error, as its likely fatal.
			if(FF_isERR(Error)) {
				return Error;
			}
			return FF_ERR_IOMAN_INVALID_FORMAT | FF_MOUNTPARTITION;
		}
	}

	// Assume FAT16, then we'll adjust if its FAT32
	pPart->ReservedSectors = FF_getShort(pBuffer->pBuffer, FF_FAT_RESERVED_SECTORS);
	pPart->FatBeginLBA = pPart->BeginLBA + pPart->ReservedSectors;

	pPart->NumFATS = (FF_T_UINT8) FF_getShort(pBuffer->pBuffer, FF_FAT_NUMBER_OF_FATS);
	pPart->SectorsPerFAT = (FF_T_UINT32) FF_getShort(pBuffer->pBuffer, FF_FAT_16_SECTORS_PER_FAT);

	pPart->SectorsPerCluster = FF_getChar(pBuffer->pBuffer, FF_FAT_SECTORS_PER_CLUS);

	pPart->BlkFactor = (FF_T_UINT8) (pPart->BlkSize / pIoman->BlkSize);    // Set the BlockFactor (How many real-blocks in a fake block!).

	if(pPart->SectorsPerFAT == 0) {	// FAT32
		pPart->SectorsPerFAT	= FF_getLong(pBuffer->pBuffer, FF_FAT_32_SECTORS_PER_FAT);
		pPart->RootDirCluster	= FF_getLong(pBuffer->pBuffer, FF_FAT_ROOT_DIR_CLUSTER);
		pPart->ClusterBeginLBA	= pPart->BeginLBA + pPart->ReservedSectors + (pPart->NumFATS * pPart->SectorsPerFAT);
		pPart->TotalSectors		= (FF_T_UINT32) FF_getShort(pBuffer->pBuffer, FF_FAT_16_TOTAL_SECTORS);
		if(pPart->TotalSectors == 0) {
			pPart->TotalSectors = FF_getLong(pBuffer->pBuffer, FF_FAT_32_TOTAL_SECTORS);
		}
		memcpy (pPart->VolLabel, pBuffer->pBuffer + FF_FAT_32_VOL_LABEL, sizeof pPart->VolLabel);
	} else {	// FAT16
		pPart->ClusterBeginLBA	= pPart->BeginLBA + pPart->ReservedSectors + (pPart->NumFATS * pPart->SectorsPerFAT);
		pPart->TotalSectors		= (FF_T_UINT32) FF_getShort(pBuffer->pBuffer, FF_FAT_16_TOTAL_SECTORS);
		pPart->RootDirCluster	= 1; // 1st Cluster is RootDir!
		if(pPart->TotalSectors == 0) {
			pPart->TotalSectors = FF_getLong(pBuffer->pBuffer, FF_FAT_32_TOTAL_SECTORS);
		}
		memcpy (pPart->VolLabel, pBuffer->pBuffer + FF_FAT_16_VOL_LABEL, sizeof pPart->VolLabel);
	}
#ifdef FF_WRITE_FREE_COUNT
	pPart->FSInfoLBA = pPart->BeginLBA + FF_getShort(pBuffer->pBuffer, 48);
#endif

	Error = FF_ReleaseBuffer(pIoman, pBuffer);	// Release the buffer finally!
	if(FF_isERR(Error)) {
		return Error;
	}

	if(!pPart->BlkSize) {
		return FF_ERR_IOMAN_INVALID_FORMAT | FF_MOUNTPARTITION;
	}

	pPart->RootDirSectors	= ((FF_getShort(pBuffer->pBuffer, FF_FAT_ROOT_ENTRY_COUNT) * 32) + pPart->BlkSize - 1) / pPart->BlkSize;
	pPart->FirstDataSector	= pPart->ClusterBeginLBA + pPart->RootDirSectors;
	pPart->DataSectors		= pPart->TotalSectors - (pPart->ReservedSectors + (pPart->NumFATS * pPart->SectorsPerFAT) + pPart->RootDirSectors);

	if(!pPart->SectorsPerCluster) {
		return FF_ERR_IOMAN_INVALID_FORMAT | FF_MOUNTPARTITION;
	}

	pPart->NumClusters = pPart->DataSectors / pPart->SectorsPerCluster;

	Error = FF_DetermineFatType(pIoman);

	if(FF_isERR(Error)) {
		return Error;
	}
	pPart->PartitionMounted = FF_TRUE;
	pPart->LastFreeCluster	= 0;
#ifdef FF_MOUNT_FIND_FREE
	pPart->LastFreeCluster	= FF_FindFreeCluster(pIoman, &Error);
	if(FF_isERR(Error)) {
		 return Error;
	}
	pPart->FreeClusterCount = FF_CountFreeClusters(pIoman, &Error);
	if(FF_isERR(Error)) {
		 return Error;
	}
#else
	pPart->FreeClusterCount = 0;
#endif

	return FF_ERR_NONE;
}

/**
 *	@public
 *	@brief	Unregister a Blockdevice, so that the IOMAN can be re-used for another device.
 *
 *	Any active partitions must be Unmounted first.
 *
 *	@param	pIoman	FF_IOMAN object.
 *
 *	@return	FF_ERR_NONE on success.
 **/
FF_ERROR FF_UnregisterBlkDevice(FF_IOMAN *pIoman) {

	FF_ERROR RetVal = FF_ERR_NONE;

	if(!pIoman) {
		return FF_ERR_NULL_POINTER | FF_UNREGISTERBLKDEVICE;
	}

	FF_PendSemaphore(pIoman->pSemaphore);
	{
		if(pIoman->pPartition->PartitionMounted == FF_FALSE) {
			pIoman->pBlkDevice->devBlkSize		= 0;
			pIoman->pBlkDevice->fnpReadBlocks	= NULL;
			pIoman->pBlkDevice->fnpWriteBlocks	= NULL;
			pIoman->pBlkDevice->pParam			= NULL;
		} else {
			RetVal = FF_ERR_IOMAN_PARTITION_MOUNTED | FF_UNREGISTERBLKDEVICE;
		}
	}
	FF_ReleaseSemaphore(pIoman->pSemaphore);

	return RetVal;
}

/**
 *	@private
 *	@brief		Checks the cache for Active Handles
 *
 *	@param		pIoman FF_IOMAN Object.
 *
 *	@return		FF_TRUE if an active handle is found, else FF_FALSE.
 *
 *	@pre		This function must be wrapped with the cache handling semaphore.
 **/
static FF_T_BOOL FF_ActiveHandles(FF_IOMAN *pIoman) {
	FF_T_UINT32	i;
	FF_BUFFER	*pBuffer;

	for(i = 0; i < pIoman->CacheSize; i++) {
		pBuffer = (pIoman->pBuffers + i);
		if(pBuffer->NumHandles) {
			return FF_TRUE;
		}
	}

	return FF_FALSE;
}


/**
 *	@public
 *	@brief	Unmounts the active partition.
 *
 *	@param	pIoman	FF_IOMAN Object.
 *
 *	@return FF_ERR_NONE on success.
 **/
FF_ERROR FF_UnmountPartition(FF_IOMAN *pIoman) {
	FF_ERROR RetVal = FF_ERR_NONE;
#ifdef FF_MIRROR_FATS_UMOUNT
	FF_T_UINT i, y;
	FF_BUFFER *pBuffer;
#endif 

	if(!pIoman || !pIoman->pPartition) {
		return FF_ERR_NULL_POINTER | FF_UNMOUNTPARTITION;
	}
	if (!pIoman->pPartition->PartitionMounted)
		return FF_ERR_NONE;

	FF_PendSemaphore(pIoman->pSemaphore);	// Ensure that there are no File Handles
	{
		if(!FF_ActiveHandles(pIoman)) {
			if(pIoman->FirstFile == NULL) {
				// Release Semaphore to call this function!
				FF_ReleaseSemaphore(pIoman->pSemaphore);
				RetVal = FF_FlushCache(pIoman);			// Flush any unwritten sectors to disk.
				if(FF_isERR(RetVal)) {
					return RetVal;
				}
				// Reclaim Semaphore
				FF_PendSemaphore(pIoman->pSemaphore);
				pIoman->pPartition->PartitionMounted = FF_FALSE;

#ifdef FF_MIRROR_FATS_UMOUNT
				FF_ReleaseSemaphore(pIoman->pSemaphore);
				for(i = 0; i < pIoman->pPartition->SectorsPerFAT; i++) {
					 pBuffer = FF_GetBuffer(pIoman, pIoman->pPartition->FatBeginLBA + i, FF_MODE_READ);
					 if(!pBuffer) {
						  RetVal = FF_ERR_DEVICE_DRIVER_FAILED | FF_UNMOUNTPARTITION;
						  break;
					 }
					 for(y = 0; y < pIoman->pPartition->NumFATS; y++) {
						  FF_BlockWrite(pIoman, pIoman->pPartition->FatBeginLBA + (y*pIoman->pPartition->SectorsPerFAT) + i, 1, pBuffer->pBuffer, FF_FALSE);
					 }					 
				}
				FF_PendSemaphore(pIoman->pSemaphore);
#endif
			} else {
				RetVal = FF_ERR_IOMAN_ACTIVE_HANDLES | FF_UNMOUNTPARTITION;
			}
		} else {
			RetVal = FF_ERR_IOMAN_ACTIVE_HANDLES | FF_UNMOUNTPARTITION;	// Active handles found on the cache.
		}
	}
	FF_ReleaseSemaphore(pIoman->pSemaphore);

	return RetVal;
}


FF_ERROR FF_IncreaseFreeClusters(FF_IOMAN *pIoman, FF_T_UINT32 Count) {

	FF_ERROR Error;

#ifdef FF_WRITE_FREE_COUNT
	FF_BUFFER *pBuffer;
#endif

	if(!pIoman->pPartition->FreeClusterCount) {
		 pIoman->pPartition->FreeClusterCount = FF_CountFreeClusters(pIoman, &Error);
		 if(FF_isERR(Error)) {
			  return Error;
		 }
	} else {
		 pIoman->pPartition->FreeClusterCount += Count;
	}

	if(!pIoman->pPartition->LastFreeCluster) {
		 pIoman->pPartition->LastFreeCluster = FF_FindFreeCluster(pIoman, &Error);
		 if(FF_isERR(Error)) {
			  return Error;
		 }
	}

#ifdef FF_WRITE_FREE_COUNT
	// FAT32 update the FSINFO sector.
	if(pIoman->pPartition->Type == FF_T_FAT32) {
		 // Find the FSINFO sector.
		 pBuffer = FF_GetBuffer(pIoman, pIoman->pPartition->FSInfoLBA, FF_MODE_WRITE);
		 {
			  if(!pBuffer) {
					return FF_ERR_DEVICE_DRIVER_FAILED | FF_INCREASEFREECLUSTERS;
			  }

			  if(FF_getLong(pBuffer->pBuffer, 0) == 0x41615252 && FF_getLong(pBuffer->pBuffer, 484) == 0x61417272) {
					// FSINFO sector magic nums we're verified. Safe to write.
					FF_putLong(pBuffer->pBuffer, 488, pIoman->pPartition->FreeClusterCount);
					FF_putLong(pBuffer->pBuffer, 492, pIoman->pPartition->LastFreeCluster);
			  }
		 }
		 Error = FF_ReleaseBuffer(pIoman, pBuffer);
		 if(FF_isERR(Error)) {
			 return Error;
		 }
	}
#endif

	return FF_ERR_NONE;
}

FF_ERROR FF_DecreaseFreeClusters(FF_IOMAN *pIoman, FF_T_UINT32 Count) {

	FF_ERROR Error;
#ifdef FF_WRITE_FREE_COUNT
	FF_BUFFER *pBuffer;
#endif

	if(!pIoman->pPartition->FreeClusterCount) {
		 pIoman->pPartition->FreeClusterCount = FF_CountFreeClusters(pIoman, &Error);
		 if(FF_isERR(Error)) {
			  return Error;
		 }
	} else {
		 pIoman->pPartition->FreeClusterCount -= Count;
	}

	if(!pIoman->pPartition->LastFreeCluster) {
		 pIoman->pPartition->LastFreeCluster = FF_FindFreeCluster(pIoman, &Error);
		 if(FF_isERR(Error)) {
			  return Error;
		 }
	}

#ifdef FF_WRITE_FREE_COUNT
	// FAT32 update the FSINFO sector.
	if(pIoman->pPartition->Type == FF_T_FAT32) {
		 // Find the FSINFO sector.
		 pBuffer = FF_GetBuffer(pIoman, pIoman->pPartition->FSInfoLBA, FF_MODE_WRITE);
		 {
			  if(!pBuffer) {
					return FF_ERR_DEVICE_DRIVER_FAILED | FF_DECREASEFREECLUSTERS;
			  }

			  if(FF_getLong(pBuffer->pBuffer, 0) == 0x41615252 && FF_getLong(pBuffer->pBuffer, 484) == 0x61417272) {
					// FSINFO sector magic nums we're verified. Safe to write.
					FF_putLong(pBuffer->pBuffer, 488, pIoman->pPartition->FreeClusterCount);
					FF_putLong(pBuffer->pBuffer, 492, pIoman->pPartition->LastFreeCluster);
			  }
		 }
		 Error = FF_ReleaseBuffer(pIoman, pBuffer);
		 if(FF_isERR(Error)) {
			 return Error;
		 }
	}
#endif
	return FF_ERR_NONE;
}


/**
 *	@brief	Returns the Block-size of a mounted Partition
 *
 *	The purpose of this function is to provide API access to information
 *	that might be useful in special cases. Like USB sticks that require a sector
 *	knocking sequence for security. After the sector knock, some secure USB
 *	sticks then present a different BlockSize.
 *
 *	@param	pIoman		FF_IOMAN Object returned from FF_CreateIOMAN()
 *
 *	@return	The blocksize of the partition. A value less than 0 when an error occurs.
 *	@return	Any negative value can be cast to the FF_ERROR type.
 **/
FF_T_SINT32 FF_GetPartitionBlockSize(FF_IOMAN *pIoman) {

	if(pIoman) {
		return (FF_T_SINT32) pIoman->pPartition->BlkSize;
	}

	return FF_ERR_NULL_POINTER | FF_GETPARTITIONBLOCKSIZE;
}

#ifdef FF_64_NUM_SUPPORT
/**
 *	@brief	Returns the number of bytes contained within the mounted partition or volume.
 *
 *	@param	pIoman		FF_IOMAN Object returned from FF_CreateIOMAN()
 *
 *	@return The total number of bytes that the mounted partition or volume contains.
 *
 **/
FF_T_UINT64 FF_GetVolumeSize(FF_IOMAN *pIoman) {
	if(pIoman) {
		FF_T_UINT32 TotalClusters = (pIoman->pPartition->DataSectors / pIoman->pPartition->SectorsPerCluster);
		return (FF_T_UINT64) ((FF_T_UINT64)TotalClusters * (FF_T_UINT64)((FF_T_UINT64)pIoman->pPartition->SectorsPerCluster * (FF_T_UINT64)pIoman->pPartition->BlkSize));
	}
	return 0;
}
#else
FF_T_UINT32 FF_GetVolumeSize(FF_IOMAN *pIoman) {
	if(pIoman) {
		FF_T_UINT32 TotalClusters = pIoman->pPartition->DataSectors / pIoman->pPartition->SectorsPerCluster;
		return (FF_T_UINT32) (TotalClusters * (pIoman->pPartition->SectorsPerCluster * pIoman->pPartition->BlkSize));
	}
	return 0;
}
#endif

