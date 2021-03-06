#include "exefs.h"
#include "backwardlz77.h"
#include <openssl/sha.h>

const int CExeFs::s_nBlockSize = 0x200;

CExeFs::CExeFs()
	: m_pFileName(nullptr)
	, m_bVerbose(false)
	, m_pHeaderFileName(nullptr)
	, m_bUncompress(false)
	, m_bCompress(false)
	, m_fpExeFs(nullptr)
{
	memset(&m_ExeFsSuperBlock, 0, sizeof(m_ExeFsSuperBlock));
	m_mPath["banner"] = STR("banner.bnr");
	m_mPath["icon"] = STR("icon.icn");
	m_mPath["logo"] = STR("logo.bcma.lz");
}

CExeFs::~CExeFs()
{
}

void CExeFs::SetFileName(const char* a_pFileName)
{
	m_pFileName = a_pFileName;
}

void CExeFs::SetVerbose(bool a_bVerbose)
{
	m_bVerbose = a_bVerbose;
}

void CExeFs::SetHeaderFileName(const char* a_pHeaderFileName)
{
	m_pHeaderFileName = a_pHeaderFileName;
}

void CExeFs::SetExeFsDirName(const char* a_pExeFsDirName)
{
	m_sExeFsDirName = FSAToUnicode(a_pExeFsDirName);
}

void CExeFs::SetUncompress(bool a_bUncompress)
{
	m_bUncompress = a_bUncompress;
}

void CExeFs::SetCompress(bool a_bCompress)
{
	m_bCompress = a_bCompress;
}

bool CExeFs::ExtractFile()
{
	bool bResult = true;
	m_fpExeFs = FFopen(m_pFileName, "rb");
	if (m_fpExeFs == nullptr)
	{
		return false;
	}
	fread(&m_ExeFsSuperBlock, sizeof(m_ExeFsSuperBlock), 1, m_fpExeFs);
	if (!FMakeDir(m_sExeFsDirName.c_str()))
	{
		fclose(m_fpExeFs);
		return false;
	}
	if (!extractHeader())
	{
		bResult = false;
	}
	for (int i = 0; i < 8; i++)
	{
		if (!extractSection(i))
		{
			bResult = false;
		}
	}
	fclose(m_fpExeFs);
	return bResult;
}

bool CExeFs::CreateFile()
{
	bool bResult = true;
	m_fpExeFs = FFopen(m_pFileName, "wb");
	if (m_fpExeFs == nullptr)
	{
		return false;
	}
	if (!createHeader())
	{
		fclose(m_fpExeFs);
		return false;
	}
	for (int i = 0; i < 8; i++)
	{
		if (!createSection(i))
		{
			bResult = false;
			i--;
		}
	}
	FFseek(m_fpExeFs, 0, SEEK_SET);
	fwrite(&m_ExeFsSuperBlock, sizeof(m_ExeFsSuperBlock), 1, m_fpExeFs);
	fclose(m_fpExeFs);
	return bResult;
}

bool CExeFs::IsExeFsFile(const char* a_pFileName, n64 a_nOffset)
{
	FILE* fp = FFopen(a_pFileName, "rb");
	if (fp == nullptr)
	{
		return false;
	}
	ExeFsSuperBlock exeFsSuperBlock;
	fread(&exeFsSuperBlock, sizeof(exeFsSuperBlock), 1, fp);
	fclose(fp);
	return IsExeFsSuperBlock(exeFsSuperBlock);
}

bool CExeFs::IsExeFsSuperBlock(const ExeFsSuperBlock& a_ExeFsSuperBlock)
{
	static const u8 uReserved[sizeof(a_ExeFsSuperBlock.m_Reserved)] = {};
	return a_ExeFsSuperBlock.m_Header[0].offset == 0 && memcmp(a_ExeFsSuperBlock.m_Reserved, uReserved, sizeof(a_ExeFsSuperBlock.m_Reserved)) == 0;
}

bool CExeFs::extractHeader()
{
	bool bResult = true;
	if (m_pHeaderFileName != nullptr)
	{
		FILE* fp = FFopen(m_pHeaderFileName, "wb");
		if (fp == nullptr)
		{
			bResult = false;
		}
		else
		{
			if (m_bVerbose)
			{
				printf("save: %s\n", m_pHeaderFileName);
			}
			fwrite(&m_ExeFsSuperBlock, sizeof(m_ExeFsSuperBlock), 1, fp);
			fclose(fp);
		}
	}
	else if (m_bVerbose)
	{
		printf("INFO: exefs header is not extract\n");
	}
	return bResult;
}

bool CExeFs::extractSection(int a_nIndex)
{
	bool bResult = true;
	String sPath;
	string sName = reinterpret_cast<const char*>(m_ExeFsSuperBlock.m_Header[a_nIndex].name);
	if (!sName.empty())
	{
		auto it = m_mPath.find(sName);
		if (it == m_mPath.end())
		{
			if (a_nIndex == 0)
			{
				sPath = m_sExeFsDirName + STR("/code.bin");
			}
			else
			{
				if (m_bVerbose)
				{
					printf("INFO: unknown entry name %s\n", sName.c_str());
				}
				sPath = m_sExeFsDirName + STR("/") + FSAToUnicode(sName) + STR(".bin");
			}
		}
		else
		{
			sPath = m_sExeFsDirName + STR("/") + it->second;
		}
		FILE* fp = FFopenUnicode(sPath.c_str(), STR("wb"));
		if (fp == nullptr)
		{
			bResult = false;
		}
		else
		{
			if (m_bVerbose)
			{
				FPrintf(STR("save: %s\n"), sPath.c_str());
			}
			if (a_nIndex == 0 && m_bUncompress)
			{
				u32 uCompressedSize = m_ExeFsSuperBlock.m_Header[a_nIndex].size;
				FFseek(m_fpExeFs, sizeof(m_ExeFsSuperBlock) + m_ExeFsSuperBlock.m_Header[a_nIndex].offset, SEEK_SET);
				u8* pCompressed = new u8[uCompressedSize];
				fread(pCompressed, 1, uCompressedSize, m_fpExeFs);
				u32 uUncompressedSize = 0;
				bResult = CBackwardLZ77::GetUncompressedSize(pCompressed, uCompressedSize, uUncompressedSize);
				if (bResult)
				{
					u8* pUncompressed = new u8[uUncompressedSize];
					bResult = CBackwardLZ77::Uncompress(pCompressed, uCompressedSize, pUncompressed, uUncompressedSize);
					if (bResult)
					{
						fwrite(pUncompressed, 1, uUncompressedSize, fp);
					}
					else
					{
						printf("ERROR: uncompress error\n\n");
					}
					delete[] pUncompressed;
				}
				else
				{
					printf("ERROR: get uncompressed size error\n\n");
				}
				delete[] pCompressed;
			}
			if (a_nIndex != 0 || !m_bUncompress || !bResult)
			{
				FCopyFile(fp, m_fpExeFs, sizeof(m_ExeFsSuperBlock) + m_ExeFsSuperBlock.m_Header[a_nIndex].offset, m_ExeFsSuperBlock.m_Header[a_nIndex].size);
			}
			fclose(fp);
		}
	}
	return bResult;
}

bool CExeFs::createHeader()
{
	FILE* fp = FFopen(m_pHeaderFileName, "rb");
	if (fp == nullptr)
	{
		return false;
	}
	FFseek(fp, 0, SEEK_END);
	n64 nFileSize = FFtell(fp);
	if (nFileSize < sizeof(m_ExeFsSuperBlock))
	{
		fclose(fp);
		printf("ERROR: exefs header is too short\n\n");
		return false;
	}
	if (m_bVerbose)
	{
		printf("load: %s\n", m_pHeaderFileName);
	}
	FFseek(fp, 0, SEEK_SET);
	fread(&m_ExeFsSuperBlock, sizeof(m_ExeFsSuperBlock), 1, fp);
	fclose(fp);
	fwrite(&m_ExeFsSuperBlock, sizeof(m_ExeFsSuperBlock), 1, m_fpExeFs);
	return true;
}

bool CExeFs::createSection(int a_nIndex)
{
	bool bResult = true;
	String sPath;
	string sName = reinterpret_cast<const char*>(m_ExeFsSuperBlock.m_Header[a_nIndex].name);
	if (!sName.empty())
	{
		auto it = m_mPath.find(sName);
		if (it == m_mPath.end())
		{
			if (a_nIndex == 0)
			{
				sPath = m_sExeFsDirName + STR("/code.bin");
			}
			else
			{
				if (m_bVerbose)
				{
					printf("INFO: unknown entry name %s\n", sName.c_str());
				}
				sPath = m_sExeFsDirName + STR("/") + FSAToUnicode(sName) + STR(".bin");
			}
		}
		else
		{
			sPath = m_sExeFsDirName + STR("/") + it->second;
		}
		FILE* fp = FFopenUnicode(sPath.c_str(), STR("rb"));
		if (fp == nullptr)
		{
			clearSection(a_nIndex);
			bResult = false;
		}
		else
		{
			if (m_bVerbose)
			{
				FPrintf(STR("load: %s\n"), sPath.c_str());
			}
			FFseek(fp, 0, SEEK_END);
			u32 uFileSize = static_cast<u32>(FFtell(fp));
			FFseek(fp, 0, SEEK_SET);
			u8* pData = new u8[uFileSize];
			fread(pData, 1, uFileSize, fp);
			fclose(fp);
			bool bCompressResult = false;
			if (a_nIndex == 0 && m_bCompress)
			{
				u32 uCompressedSize = uFileSize;
				u8* pCompressed = new u8[uCompressedSize];
				bCompressResult = CBackwardLZ77::Compress(pData, uFileSize, pCompressed, uCompressedSize);
				if (bCompressResult)
				{
					SHA256(pCompressed, uCompressedSize, m_ExeFsSuperBlock.m_Hash[7 - a_nIndex]);
					fwrite(pCompressed, 1, uCompressedSize, m_fpExeFs);
				}
				delete[] pCompressed;
			}
			if (a_nIndex != 0 || !m_bCompress || !bCompressResult)
			{
				SHA256(pData, uFileSize, m_ExeFsSuperBlock.m_Hash[7 - a_nIndex]);
				fwrite(pData, 1, uFileSize, m_fpExeFs);
			}
			delete[] pData;
			FPadFile(m_fpExeFs, FAlign(FFtell(m_fpExeFs), s_nBlockSize) - FFtell(m_fpExeFs), 0);
		}
	}
	return bResult;
}

void CExeFs::clearSection(int a_nIndex)
{
	if (a_nIndex != 7)
	{
		memmove(&m_ExeFsSuperBlock.m_Header[a_nIndex], &m_ExeFsSuperBlock.m_Header[a_nIndex + 1], sizeof(m_ExeFsSuperBlock.m_Header[0]) * (7 - a_nIndex));
	}
	memset(&m_ExeFsSuperBlock.m_Header[7], 0, sizeof(m_ExeFsSuperBlock.m_Header[7]));
	memmove(m_ExeFsSuperBlock.m_Hash[1], m_ExeFsSuperBlock.m_Hash[0], sizeof(m_ExeFsSuperBlock.m_Hash[0]) * (7 - a_nIndex));
	memset(m_ExeFsSuperBlock.m_Hash[0], 0, sizeof(m_ExeFsSuperBlock.m_Hash[0]));
}
