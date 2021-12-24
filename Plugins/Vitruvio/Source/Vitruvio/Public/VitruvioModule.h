/* Copyright 2021 Esri
 *
 * Licensed under the Apache License Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "AttributeMap.h"
#include "InitialShape.h"
#include "MeshCache.h"
#include "MeshDescription.h"
#include "PRTTypes.h"
#include "RuleAttributes.h"
#include "RulePackage.h"

#include "prt/Object.h"

#include "HAL/ThreadSafeCounter.h"
#include "Modules/ModuleManager.h"
#include "Engine/StaticMesh.h"

#include "UnrealLogHandler.h"
#include "VitruvioTypes.h"

#include <map>
#include <memory>
#include <string>

DECLARE_LOG_CATEGORY_EXTERN(LogUnrealPrt, Log, All);

struct FGenerateResultDescription
{
	Vitruvio::FInstanceMap Instances;
	TMap<int32, TSharedPtr<FVitruvioMesh>> Meshes;
	TMap<int32, FString> Names;
};

class FInvalidationToken
{
public:
	mutable FCriticalSection Lock;

	void Invalidate()
	{
		FScopeLock InvalidationLock(&Lock);
		bIsInvalid = true;
	}

	bool IsInvalid() const
	{
		return bIsInvalid;
	}

private:
	FThreadSafeBool bIsInvalid = false;
};

class FEvalAttributesToken : public FInvalidationToken
{
public:
	void RequestReEvaluateAttributes()
	{
		bRequestReEvaluateAttributes = true;
	}

	bool IsReEvaluateRequested() const
	{
		return bRequestReEvaluateAttributes;
	}

private:
	FThreadSafeBool bRequestReEvaluateAttributes = false;
};

class FGenerateToken : public FInvalidationToken
{
public:
	void RequestRegenerate()
	{
		bRequestRegenerate = true;
	}

	bool IsRegenerateRequested() const
	{
		return bRequestRegenerate;
	}

private:
	FThreadSafeBool bRequestRegenerate = false;
};

template <typename R, typename T>
class TResult
{
public:
	using FTokenConstPtr = TSharedPtr<const T>;
	using FTokenPtr = TSharedPtr<T>;

	struct ResultType
	{
		FTokenConstPtr Token;
		R Value;
	};
	using FFutureType = TFuture<ResultType>;

	FFutureType Result;
	FTokenPtr Token;
};

using FGenerateResult = TResult<FGenerateResultDescription, FGenerateToken>;
using FAttributeMapResult = TResult<FAttributeMapPtr, FEvalAttributesToken>;

class VitruvioModule final : public IModuleInterface, public FGCObject
{
	friend class VitruvioEditorModule;

public:
	void StartupModule() override;
	void ShutdownModule() override;

	/**
	 * \brief Decodes the given texture.
	 */
	VITRUVIO_API Vitruvio::FTextureData DecodeTexture(UObject* Outer, const FString& Path, const FString& Key) const;

	/**
	 * \brief Asynchronously generate the models with the given InitialShape, RulePackage and Attributes.
	 *
	 * \param InitialShape
	 * \param RulePackage
	 * \param Attributes
	 * \param RandomSeed
	 * \return the generated UStaticMesh.
	 */
	VITRUVIO_API FGenerateResult GenerateAsync(const TArray<FInitialShapeFace>& InitialShape, URulePackage* RulePackage, AttributeMapUPtr Attributes,
											   const int32 RandomSeed) const;

	/**
	 * \brief Generate the models with the given InitialShape, RulePackage and Attributes.
	 *
	 * \param InitialShape
	 * \param RulePackage
	 * \param Attributes
	 * \param RandomSeed
	 * \return the generated UStaticMesh.
	 */
	VITRUVIO_API FGenerateResultDescription Generate(const TArray<FInitialShapeFace>& InitialShape, URulePackage* RulePackage,
													 AttributeMapUPtr Attributes, const int32 RandomSeed) const;

	/**
	 * \brief Asynchronously evaluates attributes for the given initial shape and rule package.
	 *
	 * \param InitialShape
	 * \param RulePackage
	 * \param Attributes
	 * \param RandomSeed
	 * \return
	 */
	VITRUVIO_API FAttributeMapResult EvaluateRuleAttributesAsync(const TArray<FInitialShapeFace>& InitialShape, URulePackage* RulePackage,
															 AttributeMapUPtr Attributes, const int32 RandomSeed) const;

	/**
	 * \return whether PRT is initialized meaning installed and ready to use. Before initialization generation is not possible and will
	 * immediately return without results.
	 */
	VITRUVIO_API bool IsInitialized() const
	{
		return Initialized;
	}

	/**
	 * \return true if currently at least one generate call ongoing.
	 */
	VITRUVIO_API bool IsGenerating() const
	{
		return GenerateCallsCounter.GetValue() > 0;
	}

	/**
	 * \return the number of active generate calls.
	 */
	VITRUVIO_API int32 GetNumGenerateCalls() const
	{
		return GenerateCallsCounter.GetValue();
	}

	/**
	 * \return true if currently at least one RPK is being loaded.
	 */
	VITRUVIO_API bool IsLoadingRpks() const
	{
		return RpkLoadingTasksCounter.GetValue() > 0;
	}

	/**
	 * \returns the cache used for materials generated by PRT.
	 */
	VITRUVIO_API TMap<Vitruvio::FMaterialAttributeContainer, UMaterialInstanceDynamic*>& GetMaterialCache()
	{
		return MaterialCache;
	}

	/**
	 * \returns the cache used for instanced meshes by PRT.
	 */
	VITRUVIO_API FMeshCache& GetMeshCache()
	{
		return MeshCache;
	}

	/**
	 * \returns the cache used for materials generated by PRT.
	 */
	VITRUVIO_API TMap<FString, Vitruvio::FTextureData>& GetTextureCache()
	{
		return TextureCache;
	}

	/**
	 * Registers a generated mesh to keep it from being garbage collected.
	 */
	VITRUVIO_API void RegisterMesh(UStaticMesh* StaticMesh);

	/**
	 * Unregisters a generated mesh and therefore allows the garbage collector to delete it if it not referenced anywhere else.
	 */
	VITRUVIO_API void UnregisterMesh(UStaticMesh* StaticMesh);

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnGenerateCompleted, int);

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAllGenerateCompleted, int, int);

	/**
	 * Delegate which is called after a generate call has completed.
	 */
	FOnGenerateCompleted OnGenerateCompleted;

	/**
	* Delegate which is called after all generate calls have completed.
	*/
	FOnAllGenerateCompleted OnAllGenerateCompleted;

	void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObjects(MaterialCache);
		Collector.AddReferencedObjects(RegisteredMeshes);
	};

	static VitruvioModule& Get()
	{
		return FModuleManager::LoadModuleChecked<VitruvioModule>("Vitruvio");
	}

	static VitruvioModule* GetUnchecked()
	{
		return FModuleManager::LoadModulePtr<VitruvioModule>("Vitruvio");
	}

private:
	void* PrtDllHandle = nullptr;
	prt::Object const* PrtLibrary = nullptr;
	CacheObjectUPtr PrtCache;

	TUniquePtr<UnrealLogHandler> LogHandler;

	TAtomic<bool> Initialized = false;

	mutable TMap<TLazyObjectPtr<URulePackage>, ResolveMapSPtr> ResolveMapCache;
	mutable TMap<TLazyObjectPtr<URulePackage>, FGraphEventRef> ResolveMapEventGraphRefCache;

	mutable FCriticalSection LoadResolveMapLock;

	mutable FThreadSafeCounter GenerateCallsCounter;
	mutable FThreadSafeCounter RpkLoadingTasksCounter;
	mutable FThreadSafeCounter LoadAttributesCounter;

	FString RpkFolder;

	TMap<Vitruvio::FMaterialAttributeContainer, UMaterialInstanceDynamic*> MaterialCache;
	TMap<FString, Vitruvio::FTextureData> TextureCache;
	FMeshCache MeshCache;

	FCriticalSection RegisterMeshLock;
	TSet<UStaticMesh*> RegisteredMeshes;

	TFuture<ResolveMapSPtr> LoadResolveMapAsync(URulePackage* RulePackage) const;
	void InitializePrt();

	VITRUVIO_API void EvictFromResolveMapCache(URulePackage* RulePackage);
};
