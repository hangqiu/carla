// Copyright (c) 2017 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "Carla.h"
#include "Carla/Sensor/SceneCaptureCamera.h"
#include "Carla/Game/CarlaEngine.h"
#include <chrono>
#include "TimestampLogger.h"
#include "Actor/ActorBlueprintFunctionLibrary.h"

#include "Runtime/RenderCore/Public/RenderingThread.h"

FActorDefinition ASceneCaptureCamera::GetSensorDefinition()
{
    constexpr bool bEnableModifyingPostProcessEffects = true;
    return UActorBlueprintFunctionLibrary::MakeCameraDefinition(
        TEXT("rgb"),
        bEnableModifyingPostProcessEffects);
}

ASceneCaptureCamera::ASceneCaptureCamera(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    AddPostProcessingMaterial(
        TEXT("Material'/Carla/PostProcessingMaterials/PhysicLensDistortion.PhysicLensDistortion'"));
}

void ASceneCaptureCamera::BeginPlay()
{
  Super::BeginPlay();
}

void ASceneCaptureCamera::OnFirstClientConnected()
{
}

void ASceneCaptureCamera::OnLastClientDisconnected()
{
}

void ASceneCaptureCamera::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  Super::EndPlay(EndPlayReason);
}

void ASceneCaptureCamera::PostPhysTick(UWorld *World, ELevelTick TickType, float DeltaSeconds)
{
  TRACE_CPUPROFILER_EVENT_SCOPE(ASceneCaptureCamera::PostPhysTick);
  ENQUEUE_RENDER_COMMAND(MeasureTime)
  (
    [](auto &InRHICmdList)
    {
      std::chrono::time_point<std::chrono::high_resolution_clock> Time = 
          std::chrono::high_resolution_clock::now();
      auto Duration = std::chrono::duration_cast< std::chrono::milliseconds >(Time.time_since_epoch());
      uint64_t Milliseconds = Duration.count();
      FString ProfilerText = FString("(Render)Frame: ") + FString::FromInt(FCarlaEngine::GetFrameCounter()) + 
          FString(" Time: ") + FString::FromInt(Milliseconds);
      TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*ProfilerText);
    }
  );

  double now = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

  auto frame = FCarlaEngine::GetFrameCounter();
  
  FString Role = RoleName;
  
  
  std::string result =
    "Sensor_" + std::to_string(now) +
    "_frame=" + std::to_string(frame) +
    "_rolename=" +  TCHAR_TO_UTF8(*RoleName);

  TimestampLogger::GetInstance().Log(
    std::string(TCHAR_TO_UTF8(*RoleName)),
    now,
    frame
    );
    
  carla::log_error(result);

  std::cout << result << std::endl;
  std::cout.flush();
  std::cerr << result << std::endl;

  FPixelReader::SendPixelsInRenderThread<ASceneCaptureCamera, FColor>(*this);
}

void ASceneCaptureCamera::SendGBufferTextures(FGBufferRequest& GBuffer)
{
    SendGBufferTexturesInternal(*this, GBuffer);
}
