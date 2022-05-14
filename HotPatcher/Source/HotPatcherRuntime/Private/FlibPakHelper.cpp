// Fill out your copyright notice in the Description page of Project Settings.


#include "FlibPakHelper.h"
#include "IPlatformFilePak.h"
#include "HAL/PlatformFilemanager.h"
#include "AssetManager/FFileArrayDirectoryVisitor.hpp"
#include "HotPatcherLog.h"
#include "FlibAssetManageHelper.h"
#include "HotPatcherTemplateHelper.hpp"
// Engine Header
#include "Resources/Version.h"
#include "Serialization/ArrayReader.h"
#include "AssetRegistryModule.h"
#include "IAssetRegistry.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Misc/FileHelper.h"
#include "IPlatformFilePak.h"
#include "ShaderPipelineCache.h"
#include "RHI.h"
#include "AssetRegistryState.h"
#include "Misc/Base64.h"
#include "Misc/CoreDelegates.h"
#include "Serialization/LargeMemoryReader.h"
#include "ShaderCodeLibrary.h"

void UFlibPakHelper::ExecMountPak(FString InPakPath, int32 InPakOrder, FString InMountPoint)
{
	UFlibPakHelper::MountPak(InPakPath, InPakOrder, InMountPoint);
}

bool UFlibPakHelper::MountPak(const FString& PakPath, int32 PakOrder, const FString& InMountPoint)
{
	bool bMounted = false;

	FPakPlatformFile* PakFileMgr = (FPakPlatformFile*)FPlatformFileManager::Get().GetPlatformFile(FPakPlatformFile::GetTypeName());
	if (!PakFileMgr)
	{
		UE_LOG(LogHotPatcher, Log, TEXT("GetPlatformFile(TEXT(\"PakFile\") is NULL"));
		return false;
	}

	PakOrder = FMath::Max(0, PakOrder);

	if (FPaths::FileExists(PakPath) && FPaths::GetExtension(PakPath) == TEXT("pak"))
	{
		bool bIsEmptyMountPoint = InMountPoint.IsEmpty();
		const TCHAR* MountPoint = bIsEmptyMountPoint ? NULL : InMountPoint.GetCharArray().GetData();

#if !WITH_EDITOR
		
		if (PakFileMgr->Mount(*PakPath, PakOrder, MountPoint))
		{
			UE_LOG(LogHotPatcher, Log, TEXT("Mounted = %s, Order = %d, MountPoint = %s"), *PakPath, PakOrder, !MountPoint ? TEXT("(NULL)") : MountPoint);
			bMounted = true;
		}
		else {
			UE_LOG(LogHotPatcher, Error, TEXT("Faild to mount pak = %s"), *PakPath);
			bMounted = false;
		}

#endif
	}

	return bMounted;
}

bool UFlibPakHelper::UnMountPak(const FString& PakPath)
{
	bool bMounted = false;

	FPakPlatformFile* PakFileMgr = (FPakPlatformFile*)FPlatformFileManager::Get().GetPlatformFile(FPakPlatformFile::GetTypeName());
	if (!PakFileMgr)
	{
		UE_LOG(LogHotPatcher, Log, TEXT("GetPlatformFile(TEXT(\"PakFile\") is NULL"));
		return false;
	}

	if (!FPaths::FileExists(PakPath))
		return false;
	return PakFileMgr->Unmount(*PakPath);
}

bool UFlibPakHelper::CreateFileByBytes(const FString& InFile, const TArray<uint8>& InBytes, int32 InWriteFlag /*= 0*/)
{
	if (InFile.IsEmpty()/* && FPaths::FileExists(InFile)*/)
	{
		UE_LOG(LogHotPatcher, Log, TEXT("CreateFileByBytes InFile is Empty!"));
		// UE_LOG(LogHotPatcher, Log, TEXT("CreateFileByBytes file %s existing!"), *InFile);
		return false;
	}
	FArchive* SaveToFile = NULL;
	SaveToFile = IFileManager::Get().CreateFileWriter(*InFile, InWriteFlag);
	check(SaveToFile != nullptr);
	SaveToFile->Serialize((void*)InBytes.GetData(), InBytes.Num());
	delete SaveToFile;
	return true;
}


bool UFlibPakHelper::ScanPlatformDirectory(const FString& InRelativePath, bool bIncludeFile, bool bIncludeDir, bool bRecursively, TArray<FString>& OutResault)
{
	bool bRunStatus = false;
	OutResault.Reset();

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if(PlatformFile.DirectoryExists(*InRelativePath))
	{
		TArray<FString> Files;
		TArray<FString> Dirs;

		FFileArrayDirectoryVisitor FallArrayDirVisitor;
		if (bRecursively)
		{
			PlatformFile.IterateDirectoryRecursively(*InRelativePath, FallArrayDirVisitor);
		}
		else 
		{
			PlatformFile.IterateDirectory(*InRelativePath, FallArrayDirVisitor);
		}
		Files = FallArrayDirVisitor.Files;
		Dirs = FallArrayDirVisitor.Directories;

		if (bIncludeFile)
		{
			OutResault = Files;
		}
		if (bIncludeDir)
		{
			OutResault.Append(Dirs);
		}

		bRunStatus = true;
	}
	else 
	{
		UE_LOG(LogHotPatcher, Warning, TEXT("Directory %s is not found."), *InRelativePath);
	}

	return bRunStatus;
}

bool UFlibPakHelper::ScanExtenFilesInDirectory(const FString& InRelativePath, const FString& InExtenPostfix, bool InRecursively, TArray<FString>& OutFiles)
{
	OutFiles.Reset();
	TArray<FString> ReceiveScanResult;
	if (!UFlibPakHelper::ScanPlatformDirectory(InRelativePath, true, false, InRecursively, ReceiveScanResult))
	{
		UE_LOG(LogHotPatcher, Warning, TEXT("UFlibPakHelper::ScanPlatformDirectory return false."));
		return false;
	}
	TArray<FString> FinalResult;
	for (const auto& File : ReceiveScanResult)
	{
		if (File.EndsWith(*InExtenPostfix,ESearchCase::IgnoreCase))
		{
			FinalResult.AddUnique(File);
		}
	}
	OutFiles = FinalResult;
	return true;
}

TArray<FString> UFlibPakHelper::ScanAllVersionDescribleFiles()
{
	TArray<FString> FinalResult;

	FString VersionDescribleDir = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Extension/Versions"));
	UFlibPakHelper::ScanExtenFilesInDirectory(VersionDescribleDir, TEXT(".json"), true, FinalResult);

	return FinalResult;
}

TArray<FString> UFlibPakHelper::ScanExtenPakFiles()
{
	TArray<FString> FinalResult;

	FString ExtenPakDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ExtenPak"));
	UFlibPakHelper::ScanExtenFilesInDirectory(ExtenPakDir, TEXT(".pak"), true, FinalResult);

	return FinalResult;
}

int32 UFlibPakHelper::GetPakOrderByPakPath(const FString& PakFile)
{
	int32 PakOrder = 0;
    if (!PakFile.IsEmpty())
    {
    	FString PakFilename = PakFile;
    	if (PakFilename.EndsWith(TEXT("_P.pak")))
    	{
			// Prioritize based on the chunk version number
    		// Default to version 1 for single patch system
    		uint32 ChunkVersionNumber = 1;
    		FString StrippedPakFilename = PakFilename.LeftChop(6);
    		int32 VersionEndIndex = PakFilename.Find("_", ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    		if (VersionEndIndex != INDEX_NONE && VersionEndIndex > 0)
    		{
    			int32 VersionStartIndex = PakFilename.Find("_", ESearchCase::CaseSensitive, ESearchDir::FromEnd, VersionEndIndex - 1);
    			if (VersionStartIndex != INDEX_NONE)
    			{
    				VersionStartIndex++;
    				FString VersionString = PakFilename.Mid(VersionStartIndex, VersionEndIndex - VersionStartIndex);
    				if (VersionString.IsNumeric())
    				{
    					int32 ChunkVersionSigned = FCString::Atoi(*VersionString);
    					if (ChunkVersionSigned >= 1)
    					{
    						// Increment by one so that the first patch file still gets more priority than the base pak file
    						ChunkVersionNumber = (uint32)ChunkVersionSigned + 1;
    					}
    				}
    			}
    		}
    		PakOrder += 100 * ChunkVersionNumber;
		}
    }
	return PakOrder;
}

void UFlibPakHelper::ReloadShaderbytecode()
{
	FShaderCodeLibrary::OpenLibrary("Global", FPaths::ProjectContentDir());
	FShaderCodeLibrary::OpenLibrary(FApp::GetProjectName(), FPaths::ProjectContentDir());
}


bool UFlibPakHelper::LoadShaderbytecode(const FString& LibraryName, const FString& LibraryDir)
{
	bool result = true;
	FString FinalLibraryDir = LibraryDir;
#if PLATFORM_IOS
	FinalLibraryDir = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*LibraryDir);;
#endif
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 23
	result = FShaderCodeLibrary::OpenLibrary(LibraryName, LibraryDir);
#else
	FShaderCodeLibrary::OpenLibrary(LibraryName, LibraryDir);
#endif
	UE_LOG(LogHotPatcher,Log,TEXT("Load Shader bytecode %s,Dir: %s, status: %s"),*LibraryName,*FinalLibraryDir,result?TEXT("True"):TEXT("False"));
	return result;
}

bool UFlibPakHelper::LoadShaderbytecodeInDefaultDir(const FString& LibraryName)
{
	return LoadShaderbytecode(LibraryName,FPaths::Combine(FPaths::ProjectDir(),TEXT("ShaderLibs")));
}

void UFlibPakHelper::CloseShaderbytecode(const FString& LibraryName)
{
	FShaderCodeLibrary::CloseLibrary(LibraryName);
}

#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
bool UFlibPakHelper::LoadAssetRegistryToState(const TCHAR* Path,FAssetRegistryState& Out)
{
	check(Path);

	TUniquePtr<FArchive> FileReader(IFileManager::Get().CreateFileReader(Path));
	if (FileReader)
	{
		// It's faster to load the whole file into memory on a Gen5 console
		TArray64<uint8> Data;
		Data.SetNumUninitialized(FileReader->TotalSize());
		FileReader->Serialize(Data.GetData(), Data.Num());
		check(!FileReader->IsError());
		
		FLargeMemoryReader MemoryReader(Data.GetData(), Data.Num());
		return Out.Load(MemoryReader, FAssetRegistryLoadOptions{});
	}

	return false;
}
#else
bool UFlibPakHelper::LoadAssetRegistryToState(const TCHAR* Path, FAssetRegistryState& Out)
{
	bool bStatus = false;
	FString AssetRegistryPath = Path;
	FArrayReader SerializedAssetData;
	// FString AssetRegistryBinPath = InAssetRegistryBin;
	// UE_LOG(LogHotPatcher,Log,TEXT("Load AssetRegistry %s"),*AssetRegistryBinPath);
	if (IFileManager::Get().FileExists(*AssetRegistryPath) && FFileHelper::LoadFileToArray(SerializedAssetData, *AssetRegistryPath))
	{
		FAssetRegistryState State;
		FAssetRegistrySerializationOptions SerializationOptions;
		SerializationOptions.bSerializeAssetRegistry = true;
		bStatus = State.Serialize(SerializedAssetData, SerializationOptions);
	}
	return bStatus;
}
#endif


bool UFlibPakHelper::LoadAssetRegistry(const FString& LibraryName, const FString& LibraryDir)
{
	bool bStatus = false;
	FString AssetRegistryPath = FPaths::Combine(LibraryDir,FString::Printf(TEXT("%s.bin"),*LibraryName));
	UE_LOG(LogHotPatcher,Log,TEXT("Load Asset Registry %s"),*AssetRegistryPath);
	if(FPaths::FileExists(AssetRegistryPath))
	{
		FAssetRegistryState State;
		if(LoadAssetRegistryToState(*AssetRegistryPath,State))
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION > 21)
			AssetRegistryModule.Get().AppendState(State);
#else
			// todo support append state in 4.21
#endif
			bStatus = true;
		}
	}
	return bStatus;
}



bool UFlibPakHelper::OpenPSO(const FString& Name)
{
	return FShaderPipelineCache::OpenPipelineFileCache(Name,GMaxRHIShaderPlatform);
}

FAES::FAESKey CachedAESKey;
#define AES_KEY_SIZE 32
bool ValidateEncryptionKey(TArray<uint8>& IndexData, const FSHAHash& InExpectedHash, const FAES::FAESKey& InAESKey)
{
	FAES::DecryptData(IndexData.GetData(), IndexData.Num(), InAESKey);

	// Check SHA1 value.
	FSHAHash ActualHash;
	FSHA1::HashBuffer(IndexData.GetData(), IndexData.Num(), ActualHash.Hash);
	return InExpectedHash == ActualHash;
}

bool PreLoadPak(const FString& InPakPath,const FString& AesKey)
{
	UE_LOG(LogHotPatcher, Log, TEXT("Pre load pak file: %s and check file hash."), *InPakPath);

	FArchive* Reader = IFileManager::Get().CreateFileReader(*InPakPath);
	if (!Reader)
	{
		return false;
	}

	FPakInfo Info;
	const int64 CachedTotalSize = Reader->TotalSize();
	bool bShouldLoad = false;
	int32 CompatibleVersion = FPakInfo::PakFile_Version_Latest;

	// Serialize trailer and check if everything is as expected.
	// start up one to offset the -- below
	CompatibleVersion++;
	int64 FileInfoPos = -1;
	do
	{
		// try the next version down
		CompatibleVersion--;

		FileInfoPos = CachedTotalSize - Info.GetSerializedSize(CompatibleVersion);
		if (FileInfoPos >= 0)
		{
			Reader->Seek(FileInfoPos);

			// Serialize trailer and check if everything is as expected.
			Info.Serialize(*Reader, CompatibleVersion);
			if (Info.Magic == FPakInfo::PakFile_Magic)
			{
				bShouldLoad = true;
			}
		}
	} while (!bShouldLoad && CompatibleVersion >= FPakInfo::PakFile_Version_Initial);

	if (!bShouldLoad)
	{
		Reader->Close();
		delete Reader;
		return false;
	}

	if (Info.EncryptionKeyGuid.IsValid() || Info.bEncryptedIndex)
	{
		const FString KeyString = AesKey;
		
		FAES::FAESKey AESKey;

		TArray<uint8> DecodedBuffer;
		if (!FBase64::Decode(KeyString, DecodedBuffer))
		{
			UE_LOG(LogHotPatcher, Error, TEXT("AES encryption key base64[%s] is not base64 format!"), *KeyString);
			bShouldLoad = false;
		}

		// Error checking
		if (bShouldLoad && DecodedBuffer.Num() != AES_KEY_SIZE)
		{
			UE_LOG(LogHotPatcher, Error, TEXT("AES encryption key base64[%s] can not decode to %d bytes long!"), *KeyString, AES_KEY_SIZE);
			bShouldLoad = false;
		}

		if (bShouldLoad)
		{
			FMemory::Memcpy(AESKey.Key, DecodedBuffer.GetData(), AES_KEY_SIZE);

			TArray<uint8> PrimaryIndexData;
			Reader->Seek(Info.IndexOffset);
			PrimaryIndexData.SetNum(Info.IndexSize);
			Reader->Serialize(PrimaryIndexData.GetData(), Info.IndexSize);

			FSHAHash Hash;
			FMemory::Memcpy(Hash.Hash,
#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 23)
				Info.IndexHash.Hash,
#else
				Info.IndexHash,
#endif
				20);

			if (!ValidateEncryptionKey(PrimaryIndexData, Hash, AESKey))
			{
				UE_LOG(LogHotPatcher, Error, TEXT("AES encryption key base64[%s] is not correct!"), *KeyString);
				bShouldLoad = false;
			}
			else
			{
 				CachedAESKey = AESKey;

				UE_LOG(LogHotPatcher, Log, TEXT("Use AES encryption key base64[%s] for %s."), *KeyString, *InPakPath);
				FCoreDelegates::GetPakEncryptionKeyDelegate().BindLambda(
					[AESKey](uint8 OutKey[32])
					{
						FMemory::Memcpy(OutKey, AESKey.Key, 32);
					});

				if (Info.EncryptionKeyGuid.IsValid())
				{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 26
					FCoreDelegates::GetRegisterEncryptionKeyMulticastDelegate().Broadcast(Info.EncryptionKeyGuid, AESKey);
#else
					FCoreDelegates::GetRegisterEncryptionKeyDelegate().ExecuteIfBound(Info.EncryptionKeyGuid, AESKey);
#endif
				}
			}
		}
	}

	Reader->Close();
	delete Reader;
	return bShouldLoad;
}
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 25
#define FFileIterator FFilenameIterator
#endif

TArray<FString> UFlibPakHelper::GetPakFileList(const FString& InPak, const FString& AESKey)
{
	TArray<FString> Records;
	
	auto PakFilePtr = UFlibPakHelper::GetPakFileIns(InPak,AESKey);;
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
	TRefCountPtr<FPakFile> PakFile = PakFilePtr;
#else
	TSharedPtr<FPakFile> PakFile = MakeShareable(PakFilePtr);
#endif
	UFlibPakHelper::GetPakEntrys(PakFilePtr,AESKey).GetKeys(Records);
	return Records;
}

TMap<FString,FPakEntry> UFlibPakHelper::GetPakEntrys(FPakFile* InPakFile, const FString& AESKey)
{
	TMap<FString,FPakEntry> Records;
	
	if(InPakFile)
	{
		FString MountPoint = InPakFile->GetMountPoint();
		for (FPakFile::FFileIterator It(*InPakFile, true); It; ++It)
		{
			const FString& Filename = It.Filename();
			FPakEntry PakEntry;
			PakEntry = It.Info();
			InPakFile->ReadHashFromPayload(PakEntry,PakEntry.Hash);
			Records.Emplace(MountPoint + Filename,PakEntry);
		}
		
	}
	return Records;
}

void UFlibPakHelper::DumpPakEntrys(const FString& InPak, const FString& AESKey, const FString& SaveTo)
{
	auto PakFilePtr = UFlibPakHelper::GetPakFileIns(InPak,AESKey);;
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
	TRefCountPtr<FPakFile> PakFile = PakFilePtr;
#else
	TSharedPtr<FPakFile> PakFile = MakeShareable(PakFilePtr);
#endif
	
	TMap<FString,FPakEntry> Records = UFlibPakHelper::GetPakEntrys(PakFilePtr,AESKey);
	FString FileName = FPaths::GetBaseFilename(InPak,true);
	FPakDumper Results;
	Results.PakName = FileName;
	Results.MountPoint = PakFilePtr->GetMountPoint();
	
	for(const auto& Pair:Records)
	{
		FDumpPakEntry Entry;
		FString PakFilename = Pair.Key;
		FString PackageName;
		FString RecordName;
		bool bIsAsset = false;
		if(FPackageName::TryConvertFilenameToLongPackageName(PakFilename,PackageName))
		{
			bIsAsset = true;
			RecordName = PackageName;
		}
		else
		{
			RecordName = PakFilename;
		}
		
		PakFilename = FPaths::GetBaseFilename(PakFilename,true) + FPaths::GetExtension(PakFilename,true);
		Entry.ContentSize = Pair.Value.Size;
		Entry.Offset = Pair.Value.Offset;
		FDumpPakAsset& RecordItem = Results.PakEntrys.FindOrAdd(RecordName);
		Entry.PakEntrySize = Pair.Value.GetSerializedSize(FPakInfo::PakFile_Version_Latest);
		if(bIsAsset)
		{
			UFlibAssetManageHelper::GetAssetPackageGUID(PackageName,RecordItem.GUID);
		}
		else
		{
			FSHAHash Hash;
			FMemory::Memcpy(Hash.Hash,Pair.Value.Hash,20);
			RecordItem.GUID = *Hash.ToString();
		}
		RecordItem.PackageName = PackageName;
		
		RecordItem.AssetEntrys.Add(PakFilename,Entry);
	}
	FString OutString;
	THotPatcherTemplateHelper::TSerializeStructAsJsonString(Results,OutString);
	FFileHelper::SaveStringToFile(OutString,*SaveTo);
}

FPakFile* UFlibPakHelper::GetPakFileIns(const FString& InPak, const FString& AESKey)
{
	IPlatformFile* PlatformIns = &FPlatformFileManager::Get().GetPlatformFile();
	FPakFile* rPakFile = nullptr;
	FString StandardFileName = InPak;
	FPaths::MakeStandardFilename(StandardFileName);
	TArray<FString> Records;
	if(PreLoadPak(StandardFileName,AESKey))
	{
// #if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
// 		TRefCountPtr<FPakFile> PakFile = new FPakFile(&PlatformIns->GetPlatformPhysical(), *StandardFileName, false);
// 		rPakFile = PakFile.GetReference();
// #else
		rPakFile = new FPakFile(&PlatformIns->GetPlatformPhysical(), *StandardFileName, false,true);
//#endif
	}
	return rPakFile;
}

FString UFlibPakHelper::GetPakFileMountPoint(const FString& InPak, const FString& AESKey)
{
	FString result;

	auto PakFilePtr = UFlibPakHelper::GetPakFileIns(InPak,AESKey);;
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
	TRefCountPtr<FPakFile> PakFile = PakFilePtr;
#else
	TSharedPtr<FPakFile> PakFile = MakeShareable(PakFilePtr);
#endif

	if(PakFilePtr)
	{
		result = PakFilePtr->GetMountPoint();
	}
	return result;
}

TArray<FString> UFlibPakHelper::GetAllMountedPaks()
{
	FPakPlatformFile* PakPlatformFile = (FPakPlatformFile*)(FPlatformFileManager::Get().FindPlatformFile(FPakPlatformFile::GetTypeName()));
	
	TArray<FString> Resault;
	if(PakPlatformFile)
		PakPlatformFile->GetMountedPakFilenames(Resault);
	else
		UE_LOG(LogHotPatcher,Warning,TEXT("PakPlatformFile is invalid!"));
	return Resault;
}

