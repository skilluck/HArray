/*
# Copyright(C) 2010-2016 Vyacheslav Makoveychuk (email: slv709@gmail.com, skype: vyacheslavm81)
# This file is part of VyMa\Trie.
#
# VyMa\Trie is free software : you can redistribute it and / or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Vyma\Trie is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "stdafx.h"
#include "HArrayVarRAM.h"

void HArrayVarRAM::scanKeysAndValuesFromBlock(uint32* key,
											  uint32 contentOffset,
											  uint32 keyOffset,
											  uint32 blockOffset,
											  HARRAY_ITEM_VISIT_FUNC visitor,
											  void* pData)
{
	//printf("getValuesByRangeFromBlock count=%d size=%d contentOffset=%d keyOffset=%d blockOffset=%d\n", count, size, contentOffset, keyOffset, blockOffset);

	uint32 maxOffset = blockOffset + BLOCK_ENGINE_SIZE;

	for(uint32 offset = blockOffset; offset < maxOffset; offset++)
	{
		BlockPage* pBlockPage = pBlockPages[offset >> 16];
		BlockCell& blockCell = pBlockPage->pBlock[offset & 0xFFFF];

		uchar8& blockCellType = blockCell.Type;

		if(blockCellType == EMPTY_TYPE)
		{
			continue;
		}
		else if(blockCellType == CURRENT_VALUE_TYPE) //current value
		{
			uint32& keyValue = blockCell.ValueOrOffset;

			key[keyOffset] = keyValue;

			scanKeysAndValues(key, keyOffset + 1,  blockCell.Offset, visitor, pData);
		}
		else if(blockCellType <= MAX_BRANCH_TYPE1) //branch cell
		{
			BranchPage* pBranchPage = pBranchPages[blockCell.Offset >> 16];
			BranchCell& branchCell1 = pBranchPage->pBranch[blockCell.Offset & 0xFFFF];

			//try find value in the list
			for(uint32 i=0; i<blockCellType; i++)
			{
				uint32& keyValue = branchCell1.Values[i];

				key[keyOffset] = keyValue;

				scanKeysAndValues(key, keyOffset + 1, branchCell1.Offsets[i], visitor, pData);
			}
		}
		else if(blockCellType <= MAX_BRANCH_TYPE2) //branch cell
		{
			BranchPage* pBranchPage1 = pBranchPages[blockCell.Offset >> 16];
			BranchCell branchCell1 = pBranchPage1->pBranch[blockCell.Offset & 0xFFFF];

			//try find value in the list
			for(uint32 i=0; i < BRANCH_ENGINE_SIZE; i++)
			{
				uint32& keyValue = branchCell1.Values[i];

				key[keyOffset] = keyValue;

				scanKeysAndValues(key, keyOffset + 1, branchCell1.Offsets[i], visitor, pData);
			}

			BranchPage* pBranchPage2 = pBranchPages[blockCell.ValueOrOffset >> 16];
			BranchCell branchCell2 = pBranchPage2->pBranch[blockCell.ValueOrOffset & 0xFFFF];

			//try find value in the list
			uint32 countValues = blockCellType - MAX_BRANCH_TYPE1;

			for(uint32 i=0; i<countValues; i++)
			{
				uint32& keyValue = branchCell2.Values[i];

				key[keyOffset] = keyValue;

				scanKeysAndValues(key, keyOffset + 1, branchCell2.Offsets[i], visitor, pData);
			}
		}
		else if(blockCell.Type <= MAX_BLOCK_TYPE)
		{
			//go to block
			scanKeysAndValuesFromBlock(key,
									   contentOffset,
									   keyOffset,
									   blockCell.Offset,
									   visitor,
									   pData);
		}
	}
}

void HArrayVarRAM::scanKeysAndValues(uint32* key,
									 uint32 keyOffset,
									 uint32 contentOffset,
									 HARRAY_ITEM_VISIT_FUNC visitor,
									 void* pData)
{
	//printf("getValuesByRange count=%d size=%d contentOffset=%d keyOffset=%d\n", count, size, contentOffset, keyOffset);

	for(;; keyOffset++, contentOffset++)
	{

NEXT_KEY_PART:

		ContentPage* pContentPage = pContentPages[contentOffset>>16];
		ushort16 contentIndex = contentOffset&0xFFFF;

		uint32 contentCellValueOrOffset = pContentPage->pContent[contentIndex].Value;
		uchar8 contentCellType = pContentPage->pContent[contentIndex].Type; //move to type part

		if(contentCellType >= ONLY_CONTENT_TYPE) //ONLY CONTENT =========================================================================================
		{
			uint32 keyLen =  contentCellType - ONLY_CONTENT_TYPE;

			for(uint32 i = 0; i < keyLen; i++, keyOffset++, contentOffset++)
			{
				uint32& keyValue = pContentPages[contentOffset>>16]->pContent[contentOffset&0xFFFF].Value;

				key[keyOffset] = keyValue;
			}

			//contentOffset += (keyLen - keyOffset);

			(*visitor)(key,
					   keyOffset,
					   pContentPages[contentOffset>>16]->pContent[contentOffset&0xFFFF].Value,
					   pContentPages[contentOffset>>16]->pContent[contentOffset&0xFFFF].Type,
					   pData);

			return;
		}

		if(contentCellType == VAR_TYPE) //VAR =====================================================================
		{
			VarPage* pVarPage = pVarPages[contentCellValueOrOffset >> 16];
			VarCell& varCell = pVarPage->pVar[contentCellValueOrOffset & 0xFFFF];

			//save value
			contentCellType = varCell.ContCell.Type; //read from var cell
			contentCellValueOrOffset = varCell.ContCell.Value;

			(*visitor)(key,
					   keyOffset,
					   varCell.ValueContentCell.Value,
					   varCell.ValueContentCell.Type,
					   pData);

			if(contentCellType == CONTINUE_VAR_TYPE) //CONTINUE VAR =====================================================================
			{
				contentOffset = contentCellValueOrOffset;

				//goto
				scanKeysAndValues(key, keyOffset, contentOffset, visitor, pData);

				return;
			}
		}

		if(contentCellType <= MAX_BRANCH_TYPE1) //BRANCH =====================================================================
		{
			BranchPage* pBranchPage = pBranchPages[contentCellValueOrOffset >> 16];
			BranchCell& branchCell = pBranchPage->pBranch[contentCellValueOrOffset & 0xFFFF];

			//check other
			for(uint32 i = 0; i<contentCellType; i++) //from 1
			{
				uint32& keyValue = branchCell.Values[i];

				key[keyOffset] = keyValue;

				scanKeysAndValues(key, keyOffset + 1, branchCell.Offsets[i], visitor, pData);
			}

			return;
		}
		else if(contentCellType == VALUE_TYPE)
		{
			(*visitor)(key,
					   keyOffset,
					   contentCellValueOrOffset,
					   contentCellType,
					   pData);

			return;
		}
		else if(contentCellType <= MAX_BLOCK_TYPE) //VALUE IN BLOCK ===================================================================
		{
			scanKeysAndValuesFromBlock(key,
										contentOffset,
										keyOffset,
										contentCellValueOrOffset,
										visitor,
										pData);

			return;
		}
		else if(contentCellType == CURRENT_VALUE_TYPE)
		{
			uint32& keyValue = contentCellValueOrOffset;

			key[keyOffset] = keyValue;
		}
	}
}

uint32 HArrayVarRAM::scanKeysAndValues(uint32* key,
									 uint32 keyLen,
									 HARRAY_ITEM_VISIT_FUNC visitor,
									 void* pData)
{
	keyLen >>= 2; //in 4 bytes
	uint32 maxSafeShort = MAX_SAFE_SHORT - keyLen;

	uint32 contentOffset = pHeader[key[0]>>HeaderBits].Offset;

	if(contentOffset)
	{
		uint32 keyOffset = 0;

NEXT_KEY_PART:
		ContentPage* pContentPage = pContentPages[contentOffset>>16];
		ushort16 contentIndex = contentOffset&0xFFFF;

		uchar8 contentCellType = pContentPage->pContent[contentIndex].Type; //move to type part

		if(contentCellType >= ONLY_CONTENT_TYPE) //ONLY CONTENT =========================================================================================
		{
			uint32 fullKeyLen = keyOffset + contentCellType - ONLY_CONTENT_TYPE;

			if(contentIndex < maxSafeShort) //content in one page
			{
				for(; keyOffset < keyLen; contentIndex++, keyOffset++)
				{
					if(pContentPage->pContent[contentIndex].Value != key[keyOffset])
						return 0;
				}

				for(; keyOffset < fullKeyLen; contentIndex++, keyOffset++)
				{
					key[keyOffset] = pContentPage->pContent[contentIndex].Value;
				}

				(*visitor)(key,
						   keyOffset,
						   pContentPage->pContent[contentIndex].Value,
						   pContentPage->pContent[contentIndex].Type, pData); //return value

				return 0;
			}
			else //content in two pages
			{
				for(; keyOffset < keyLen; contentOffset++, keyOffset++)
				{
					if(pContentPages[contentOffset>>16]->pContent[contentOffset&0xFFFF].Value != key[keyOffset])
						return 0;
				}

				for(; keyOffset < fullKeyLen; contentIndex++, keyOffset++)
				{
					key[keyOffset] = pContentPage->pContent[contentIndex].Value;
				}

				(*visitor)(key,
						   keyOffset,
						   pContentPages[contentOffset>>16]->pContent[contentOffset&0xFFFF].Value,
						   pContentPages[contentOffset>>16]->pContent[contentOffset&0xFFFF].Type,
						   pData);

				return 0;
			}
		}

		uint32& keyValue = key[keyOffset];
		uint32 contentCellValueOrOffset = pContentPage->pContent[contentIndex].Value;

		if(contentCellType == VAR_TYPE) //VAR =====================================================================
		{
			VarPage* pVarPage = pVarPages[contentCellValueOrOffset >> 16];
			VarCell& varCell = pVarPage->pVar[contentCellValueOrOffset & 0xFFFF];

			if(keyOffset < keyLen)
			{
				contentCellType = varCell.ContCell.Type; //read from var cell
				contentCellValueOrOffset = varCell.ContCell.Value;

				if(contentCellType == CONTINUE_VAR_TYPE) //CONTINUE VAR =====================================================================
				{
					contentOffset = contentCellValueOrOffset;

					goto NEXT_KEY_PART;
				}
			}
			else
			{
				scanKeysAndValues(key, keyOffset, contentOffset, visitor, pData);

				//return varCell.Value;

				return 0;
			}
		}
		else if(keyOffset == keyLen)
		{
			if(contentCellType == VALUE_TYPE)
			{
				(*visitor)(key,
						   keyOffset,
					       contentCellValueOrOffset,
						   contentCellType,
						   pData);

				return 0;

			}
			else
			{
				scanKeysAndValues(key, keyOffset, contentOffset, visitor, pData);

				return 0;
			}
		}

		if(contentCellType <= MAX_BRANCH_TYPE1) //BRANCH =====================================================================
		{
			BranchPage* pBranchPage = pBranchPages[contentCellValueOrOffset >> 16];
			BranchCell& branchCell = pBranchPage->pBranch[contentCellValueOrOffset & 0xFFFF];

			//try find value in the list
			uint32* values = branchCell.Values;

			for(uint32 i=0; i<contentCellType; i++)
			{
				if(values[i] == keyValue)
				{
					contentOffset = branchCell.Offsets[i];
					keyOffset++;

					goto NEXT_KEY_PART;
				}
			}

			return 0;
		}
		else if(contentCellType == VALUE_TYPE)
		{
			(*visitor)(key,
					   keyOffset,
					   contentCellValueOrOffset,
					   contentCellType,
					   pData);

			return 0;
		}
		else if(contentCellType <= MAX_BLOCK_TYPE) //VALUE IN BLOCK ===================================================================
		{
			uchar8 idxKeyValue = (contentCellType - MIN_BLOCK_TYPE) * BLOCK_ENGINE_STEP;

			uint32 startOffset = contentCellValueOrOffset;

	NEXT_BLOCK:
			uint32 subOffset = ((keyValue << idxKeyValue) >> BLOCK_ENGINE_SHIFT);
			uint32 blockOffset = startOffset + subOffset;

			BlockPage* pBlockPage = pBlockPages[blockOffset >> 16];
			BlockCell& blockCell = pBlockPage->pBlock[blockOffset & 0xFFFF];

			uchar8& blockCellType = blockCell.Type;

			if(blockCellType == EMPTY_TYPE)
			{
				return 0;
			}
			else if(blockCellType == CURRENT_VALUE_TYPE) //current value
			{
				if(blockCell.ValueOrOffset == keyValue) //value is exists
				{
					contentOffset = blockCell.Offset;
					keyOffset++;

					goto NEXT_KEY_PART;
				}
				else
				{
					return 0;
				}
			}
			else if(blockCellType <= MAX_BRANCH_TYPE1) //branch cell
			{
				BranchPage* pBranchPage = pBranchPages[blockCell.Offset >> 16];
				BranchCell& branchCell1 = pBranchPage->pBranch[blockCell.Offset & 0xFFFF];

				//try find value in the list
				for(uint32 i=0; i<blockCellType; i++)
				{
					if(branchCell1.Values[i] == keyValue)
					{
						contentOffset = branchCell1.Offsets[i];
						keyOffset++;

						goto NEXT_KEY_PART;
					}
				}

				return 0;
			}
			else if(blockCellType <= MAX_BRANCH_TYPE2) //branch cell
			{
				BranchPage* pBranchPage1 = pBranchPages[blockCell.Offset >> 16];
				BranchCell branchCell1 = pBranchPage1->pBranch[blockCell.Offset & 0xFFFF];

				//try find value in the list
				for(uint32 i=0; i < BRANCH_ENGINE_SIZE; i++)
				{
					if(branchCell1.Values[i] == keyValue)
					{
						contentOffset = branchCell1.Offsets[i];
						keyOffset++;

						goto NEXT_KEY_PART;
					}
				}

				BranchPage* pBranchPage2 = pBranchPages[blockCell.ValueOrOffset >> 16];
				BranchCell branchCell2 = pBranchPage2->pBranch[blockCell.ValueOrOffset & 0xFFFF];

				//try find value in the list
				uint32 countValues = blockCellType - MAX_BRANCH_TYPE1;

				for(uint32 i=0; i<countValues; i++)
				{
					if(branchCell2.Values[i] == keyValue)
					{
						contentOffset = branchCell2.Offsets[i];
						keyOffset++;

						goto NEXT_KEY_PART;
					}
				}

				return 0;
			}
			else if(blockCell.Type <= MAX_BLOCK_TYPE)
			{
				//go to block
				idxKeyValue = (blockCell.Type - MIN_BLOCK_TYPE) * BLOCK_ENGINE_STEP;
				startOffset = blockCell.Offset;

				goto NEXT_BLOCK;
			}
			else
			{
				return 0;
			}
		}
		else if(contentCellType == CURRENT_VALUE_TYPE) //PART OF KEY =========================================================================
		{
			if(contentCellValueOrOffset == keyValue)
			{
				contentOffset++;
				keyOffset++;

				goto NEXT_KEY_PART;
			}
			else
			{
				return 0;
			}
		}
	}

	return 0;
}
