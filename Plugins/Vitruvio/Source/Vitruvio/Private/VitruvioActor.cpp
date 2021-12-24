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

#include "VitruvioActor.h"

#include "VitruvioComponent.h"

AVitruvioActor::AVitruvioActor()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	VitruvioComponent = CreateDefaultSubobject<UVitruvioComponent>(TEXT("VitruvioComponent"));
	PrimaryActorTick.bCanEverTick = true;
}

void AVitruvioActor::Tick(float DeltaSeconds)
{
	// We do the initialization late here because we need all Components loaded and copy/pasted
	// In PostCreated we can not differentiate between an Actor which has been copy pasted
	// (in which case we would not need to load the initial shape) or spawned normally
	Initialize();
}

bool AVitruvioActor::ShouldTickIfViewportsOnly() const
{
	return true;
}

void AVitruvioActor::Initialize()
{
	if (!bInitialized)
	{
		VitruvioComponent->Initialize();
		bInitialized = true;
	}
}
