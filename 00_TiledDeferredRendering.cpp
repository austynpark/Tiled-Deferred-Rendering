/*
 * Copyright (c) 2017-2022 The Forge Interactive Inc.
 *
 * This file is part of The-Forge
 * (see https://github.com/ConfettiFX/The-Forge).
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
*/

// Unit Test for a Tiled Deferred Rendering
#include <random>

//Interfaces
#include "../../../../Common_3/Application/Interfaces/ICameraController.h"
#include "../../../../Common_3/Application/Interfaces/IApp.h"
#include "../../../../Common_3/Utilities/Interfaces/ILog.h"
#include "../../../../Common_3/Application/Interfaces/IInput.h"
#include "../../../../Common_3/Utilities/Interfaces/IFileSystem.h"
#include "../../../../Common_3/Utilities/Interfaces/ITime.h"
#include "../../../../Common_3/Application/Interfaces/IProfiler.h"
#include "../../../../Common_3/Application/Interfaces/IScreenshot.h"
#include "../../../../Common_3/Game/Interfaces/IScripting.h"
#include "../../../../Common_3/Application/Interfaces/IFont.h"
#include "../../../../Common_3/Application/Interfaces/IUI.h"
#include "../../../../Common_3/Utilities/RingBuffer.h"

//Renderer
#include "../../../../Common_3/Graphics/Interfaces/IGraphics.h"
#include "../../../../Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"

//Math
#include "../../../../Common_3/Utilities/Math/MathTypes.h"
#include "../../../../Common_3/Utilities/Interfaces/IMemory.h"

// Model / Texture file name
#include "ResourceName.inl"

#include "Shaders/Shared.h"

#define DEFERRED_RT_COUNT 2

const uint32_t gDataBufferCount = 2;
Renderer* pRenderer = NULL;

Queue*   pGraphicsQueue = NULL;
GpuCmdRing gGraphicsCmdRing = {};

SwapChain*    pSwapChain = NULL;
RenderTarget* pDepthBuffer = NULL;
Semaphore*    pImageAcquiredSemaphore = NULL;

Sampler* pSamplerBilinear = NULL;

uint32_t gFrameIndex = 0;
ProfileToken gGpuProfileToken = PROFILE_INVALID_TOKEN;

ICameraController* pCameraController = NULL;
UIComponent* pGuiWindow = NULL;
uint32_t gFontID = 0;

// holds index of pMaterialTextures
struct Material {
	uint albedoIndex = 1;
	uint normalIndex = 1;
	uint metallicIndex = 1;
	uint roughnessIndex = 1;
};

struct ObjectInfo
{
	float3 mPosition;
	float3 mRotation;
	float mScale;
	Material mMaterial;
};

struct Light
{
	vec4 mPosition; // float4(position.xyz, radius)
	vec4 mColor; // float4(color.rgb, intensity)
};

// Have a buffer for object data
struct LightData
{
	Light lights[MAX_LIGHTS] = {};
};

// Have a push constant for object data
struct ConstantObjData 
{
	mat4 mWorldMat;
	uint mMaterialId;
};

// Have a uniform for camera data
struct UniformCamData 
{
	mat4 mProjectView;
	mat4 mProjectViewInv;
	vec3 mCamPos;
};

// Have a uniform for extended camera data
struct UniformExtCamData
{
	mat4 mView;
	mat4 mProjectView;
	mat4 mProjectInv;
	mat4 mProjectViewInvViewport;
	vec3 mCamPos;
};

// Have a uniform for tile cull data;
struct UniformTileCullData
{
	uint mNumTilesX;
	uint mNumTilesY;
	uint mNumOfLights; // Active lights
	uint mDebugDraw;
	uint2 mResolution;
};

// Gbuffer
Shader* pGbufferShader = NULL;
Pipeline* pGbufferPipeline = NULL;
RootSignature* pGbufferRootSignature = NULL;
RenderTarget* pGbufferRenderTargets[DEFERRED_RT_COUNT]; //

// Descriptor for Gbuffer fill
DescriptorSet* pDescriptorSetGbuffers[2]; // 0 = texture (none), 1 = camera, object (per frame) 
uint32_t gModelIdRootConstantIndex = 0;

// render screenQuad
Shader* pRenderQuadShader = NULL;
Pipeline* pRenderQuadPipeline = NULL;
RootSignature* pRenderQuadRootSignature = NULL;
RenderTarget* pRenderQuadRenderTargets = NULL;
DescriptorSet* pDescritporSetRenderQuad = NULL; // 0 = scene textures (none)

Shader* pDeferredShader = NULL;
Pipeline* pDeferredPipeline = NULL;
RootSignature* pDeferredRootSignature = NULL;
DescriptorSet* pDescriptorSetDeferredLightPass[2] = { NULL };
uint32_t gLightCountRootConstantIndex = 0;

// Tiled Culling Base
RenderTarget* pSceneBuffer = NULL;
Shader* pTiledCullShader = NULL;
Pipeline* pTiledCullPipeline = NULL;
// Tiled Culling HalfZ
Shader* pTiledCullHalfZShader = NULL;
Pipeline* pTiledCullHalfZPipeline = NULL;

RootSignature* pTiledCullRootSignature = NULL;
DescriptorSet* pDescriptorSetCullPass[2] = { NULL }; // 0 = material_rts, depth, scene / 1 = ext_camera, light buffer

// Tiled Culling ModifiedZ
Shader* pTiledCullModifiedZShader = NULL;
Pipeline* pTiledCullModifiedZPipeline = NULL;

Buffer* pTileCullDataBuffer[gDataBufferCount] = { NULL };
UniformTileCullData gUniformTileCullData = {};

// Object Data
Geometry* gModels[MODEL_COUNT] = { NULL };
ObjectInfo gObjectInfo[MODEL_COUNT] = {};

VertexLayout gVertexLayoutModel = {};

// Quad
Buffer* pScreenQuadVertexBuffer = NULL;

// Camera Data
Buffer* pCameraBuffer[gDataBufferCount] = { NULL };
UniformCamData gUniformCamData = {};

// Extended Camera Data
Buffer* pExtCameraBuffer[gDataBufferCount] = { NULL };
UniformExtCamData gUniformExtCamData = {};

// Light Data (Cache-friendly)
Buffer* pLightPosAndRadiusBuffer[gDataBufferCount] = { NULL };
Buffer* pLightColorAndIntensityBuffer[gDataBufferCount] = { NULL };
float4 gLightPos;
vec4 gLightPositionAndRadius[MAX_LIGHTS];
vec4 gLightColorAndIntensity[MAX_LIGHTS];

// Initial Light position before rotation
float3 gInitLightPos[MAX_LIGHTS] = {};

// Texture for Materials
Texture* pMaterialTextures[TOTAL_IMGS];
int gSponzaTextureIndexForMaterial[26][5] = {};

// Material ID only for Sponza
uint32_t    gMaterialIds[] = {
	0,  3,  1,  4,  5,  6,  7,  8,  6,  9,  7,  6, 10, 5, 7,  5, 6, 7,  6, 7,  6,  7,  6,  7,  6,  7,  6,  7,  6,  7,  6,  7,  6,  7,  6,
	5,  6,  5,  11, 5,  11, 5,  11, 5,  10, 5,  9, 8,  6, 12, 2, 5, 13, 0, 14, 15, 16, 14, 15, 14, 16, 15, 13, 17, 18, 19, 18, 19, 18, 17,
	19, 18, 17, 20, 21, 20, 21, 20, 21, 20, 21, 3, 1,  3, 1,  3, 1, 3,  1, 3,  1,  3,  1,  3,  1,  22, 23, 4,  23, 4,  5,  24, 5,
};

FontDrawDesc gFrameTimeDraw; 

//Generate sky box vertex buffer
const float gSkyBoxPoints[] = {
	10.0f,  -10.0f, -10.0f, 6.0f,    // -z
	-10.0f, -10.0f, -10.0f, 6.0f,   -10.0f, 10.0f,  -10.0f, 6.0f,   -10.0f, 10.0f,
	-10.0f, 6.0f,   10.0f,  10.0f,  -10.0f, 6.0f,   10.0f,  -10.0f, -10.0f, 6.0f,

	-10.0f, -10.0f, 10.0f,  2.0f,    //-x
	-10.0f, -10.0f, -10.0f, 2.0f,   -10.0f, 10.0f,  -10.0f, 2.0f,   -10.0f, 10.0f,
	-10.0f, 2.0f,   -10.0f, 10.0f,  10.0f,  2.0f,   -10.0f, -10.0f, 10.0f,  2.0f,

	10.0f,  -10.0f, -10.0f, 1.0f,    //+x
	10.0f,  -10.0f, 10.0f,  1.0f,   10.0f,  10.0f,  10.0f,  1.0f,   10.0f,  10.0f,
	10.0f,  1.0f,   10.0f,  10.0f,  -10.0f, 1.0f,   10.0f,  -10.0f, -10.0f, 1.0f,

	-10.0f, -10.0f, 10.0f,  5.0f,    // +z
	-10.0f, 10.0f,  10.0f,  5.0f,   10.0f,  10.0f,  10.0f,  5.0f,   10.0f,  10.0f,
	10.0f,  5.0f,   10.0f,  -10.0f, 10.0f,  5.0f,   -10.0f, -10.0f, 10.0f,  5.0f,

	-10.0f, 10.0f,  -10.0f, 3.0f,    //+y
	10.0f,  10.0f,  -10.0f, 3.0f,   10.0f,  10.0f,  10.0f,  3.0f,   10.0f,  10.0f,
	10.0f,  3.0f,   -10.0f, 10.0f,  10.0f,  3.0f,   -10.0f, 10.0f,  -10.0f, 3.0f,

	10.0f,  -10.0f, 10.0f,  4.0f,    //-y
	10.0f,  -10.0f, -10.0f, 4.0f,   -10.0f, -10.0f, -10.0f, 4.0f,   -10.0f, -10.0f,
	-10.0f, 4.0f,   -10.0f, -10.0f, 10.0f,  4.0f,   10.0f,  -10.0f, 10.0f,  4.0f,
};

bool gTakeScreenshot = false;
void takeScreenshot(void* pUserData) 
{
	if (!gTakeScreenshot)
		gTakeScreenshot = true;
}

enum // run script (tile base or tile half -> set tex2d descriptor set / )
{
	NON_TILE = 0,
	TILE_BASE = 1,
	TILE_HALFZ = 2,
	TILE_MODIFIED_Z
};

static uint32_t gTileCullMode = TILE_BASE;

static bool bDebugDraw = false;
static bool bDynamicLight = false;
static bool bRandomizePosition = false;
// increased per frame and update light buffer only for 3 times after the actual data update in cpu
static uint32_t gLightFrameCount = 0;
static uint32_t gCurrentLightCount = 0;
static float gLightSpawnBoxScale = 5.0f;
static uint32_t gSelectedModel = LION_MODEL;

// for static light scene to compare improvement on depth discontinuity
void scenarioLightPosition(void* pUserData)
{
	// set to light frame count = 0, and update the light buffer per frame
	// turn on switch (update light buffer)
	gLightFrameCount = 0;
	gCurrentLightCount = 1600;
	static const vec3 camPos(0.8f, 7.8f, -26.7f);
	static const vec4 colorAndIntensity = vec4(1.0f, 0.3f, 0.3f, 1.0f);
	pCameraController->moveTo(camPos);
	
	// volume of cube (x:y:z = 8:10:15)  
	const float width = 8; // -4, 4
	const float height = 10; // 5 ~ 15
	const float depth = 20; // 0 ~ 20
	const float area = width * height * depth;
	float unitDistance = exp((1.0f/3.0f)* log(gCurrentLightCount / area));

	int xAxisCount = int(width / unitDistance);
	int yAxisCount = int(height / unitDistance);
	int zAxisCount = int(depth / unitDistance);
	gCurrentLightCount = xAxisCount * yAxisCount * zAxisCount;
	gUniformTileCullData.mNumOfLights = gCurrentLightCount;

	const vec3 offset(-4.0f, 5.0f, 0.0f); // offset

	for (int x = 0; x < xAxisCount; ++x)
	{
		for (int y = 0; y < yAxisCount; ++y)
		{
			for (int z = 0; z < zAxisCount; ++z)
			{
				gLightPositionAndRadius[zAxisCount * (x * yAxisCount + y) + z] = vec4(offset + vec3(unitDistance * x, unitDistance * y, unitDistance * z), 1.0f);
				gLightColorAndIntensity[zAxisCount * (x * yAxisCount + y) + z] = colorAndIntensity;
			}
		}
	}
}

class TiledDeferredRendering: public IApp
{
public:

	bool Init()
	{
		// FILE PATHS
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_SHADER_BINARIES, "CompiledShaders");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_GPU_CONFIG, "GPUCfg");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_TEXTURES, "Textures");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_MESHES, "Meshes");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_FONTS, "Fonts");
		fsSetPathForResourceDir(pSystemFileIO, RM_DEBUG, RD_SCREENSHOTS, "Screenshots");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_SCRIPTS, "Scripts");

		// window and renderer setup
		RendererDesc settings;
		memset(&settings, 0, sizeof(settings));
		settings.mShaderTarget = SHADER_TARGET_6_0;
		//settings.mEnableGPUBasedValidation = true;
		initRenderer(GetName(), &settings, &pRenderer);

		//check for init success
		if (!pRenderer)
			return false;

		QueueDesc queueDesc = {};
		queueDesc.mType = QUEUE_TYPE_GRAPHICS;
		queueDesc.mFlag = QUEUE_FLAG_INIT_MICROPROFILE;
		addQueue(pRenderer, &queueDesc, &pGraphicsQueue);

		GpuCmdRingDesc cmdRingDesc = {};
		cmdRingDesc.pQueue = pGraphicsQueue;
		cmdRingDesc.mPoolCount = gDataBufferCount;
		cmdRingDesc.mCmdPerPoolCount = 1;
		cmdRingDesc.mAddSyncPrimitives = true;
		addGpuCmdRing(pRenderer, &cmdRingDesc, &gGraphicsCmdRing);
		addSemaphore(pRenderer, &pImageAcquiredSemaphore);

		initScreenshotInterface(pRenderer, pGraphicsQueue);

		initResourceLoaderInterface(pRenderer);

		// Load fonts
		FontDesc font = {};
		font.pFontPath = "TitilliumText/TitilliumText-Bold.otf";
		fntDefineFonts(&font, 1, &gFontID);

		FontSystemDesc fontRenderDesc = {};
		fontRenderDesc.pRenderer = pRenderer;
		if (!initFontSystem(&fontRenderDesc))
			return false;

		// Initialize Forge User Interface Rendering
		UserInterfaceDesc uiRenderDesc = {};
		uiRenderDesc.pRenderer = pRenderer;
		initUserInterface(&uiRenderDesc);

		// Initialize micro profiler and its UI.
		ProfilerDesc profiler = {};
		profiler.pRenderer = pRenderer;
		profiler.mWidthUI = mSettings.mWidth;
		profiler.mHeightUI = mSettings.mHeight;
		initProfiler(&profiler);

		// Gpu profiler can only be added after initProfile.
		gGpuProfileToken = addGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");

		/************************************************************************/
		// GUI
		/************************************************************************/
		UIComponentDesc guiDesc = {};
		guiDesc.mStartPosition = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.2f);
		uiCreateComponent(GetName(), &guiDesc, &pGuiWindow);

		// Take a screenshot with a button.
		ButtonWidget screenshot;
		UIWidget* pScreenshot = uiCreateComponentWidget(pGuiWindow, "Screenshot", &screenshot, WIDGET_TYPE_BUTTON);
		uiSetWidgetOnEditedCallback(pScreenshot, nullptr, takeScreenshot);
		REGISTER_LUA_WIDGET(pScreenshot);

		SamplerDesc samplerDesc = { FILTER_LINEAR,       FILTER_LINEAR,       MIPMAP_MODE_LINEAR,
			ADDRESS_MODE_REPEAT, ADDRESS_MODE_REPEAT, ADDRESS_MODE_REPEAT };
		addSampler(pRenderer, &samplerDesc, &pSamplerBilinear);

		BufferLoadDesc camBuffDesc = {};
		camBuffDesc.mDesc.pName = "camBuff";
		camBuffDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		camBuffDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		camBuffDesc.mDesc.mSize = sizeof(UniformCamData);
		camBuffDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		camBuffDesc.pData = NULL;

		BufferLoadDesc extCamBuffDesc = {};
		extCamBuffDesc.mDesc.pName = "extCamBuff";
		extCamBuffDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		extCamBuffDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		extCamBuffDesc.mDesc.mSize = sizeof(UniformExtCamData);
		extCamBuffDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		extCamBuffDesc.pData = NULL;

		BufferLoadDesc tileBuffDesc = {};
		tileBuffDesc.mDesc.pName = "tileBuff";
		tileBuffDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		tileBuffDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		tileBuffDesc.mDesc.mSize = sizeof(UniformTileCullData);
		tileBuffDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		tileBuffDesc.pData = NULL;

		BufferLoadDesc lightPosBuffDesc = {};
		lightPosBuffDesc.mDesc.pName = "lightPosBuff";
		lightPosBuffDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_BUFFER;
		lightPosBuffDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		lightPosBuffDesc.mDesc.mStartState = RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		lightPosBuffDesc.mDesc.mStructStride = sizeof(float) * 4;
		lightPosBuffDesc.mDesc.mFirstElement = 0;
		lightPosBuffDesc.mDesc.mElementCount = MAX_LIGHTS;
		lightPosBuffDesc.mDesc.mSize = lightPosBuffDesc.mDesc.mStructStride * lightPosBuffDesc.mDesc.mElementCount;
		lightPosBuffDesc.pData = NULL;

		BufferLoadDesc lightColorBuffDesc = {};
		lightColorBuffDesc.mDesc.pName = "lightColorBuff";
		lightColorBuffDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_BUFFER;
		lightColorBuffDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		lightColorBuffDesc.mDesc.mStartState = RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		lightColorBuffDesc.mDesc.mStructStride = sizeof(float) * 4;
		lightColorBuffDesc.mDesc.mFirstElement = 0;
		lightColorBuffDesc.mDesc.mElementCount = MAX_LIGHTS;
		lightColorBuffDesc.mDesc.mSize = lightColorBuffDesc.mDesc.mStructStride * lightColorBuffDesc.mDesc.mElementCount;
		lightColorBuffDesc.pData = NULL;

		for (uint32_t i = 0; i < gDataBufferCount; ++i) {

			camBuffDesc.ppBuffer = &pCameraBuffer[i];
			addResource(&camBuffDesc, NULL);

			extCamBuffDesc.ppBuffer = &pExtCameraBuffer[i];
			addResource(&extCamBuffDesc, NULL);

			tileBuffDesc.ppBuffer = &pTileCullDataBuffer[i];
			addResource(&tileBuffDesc, NULL);

			lightPosBuffDesc.ppBuffer = &pLightPosAndRadiusBuffer[i];
			addResource(&lightPosBuffDesc, NULL);

			lightColorBuffDesc.ppBuffer = &pLightColorAndIntensityBuffer[i];
			addResource(&lightColorBuffDesc, NULL);
		}

		float screenQuadPoints[] = {
			-1.0f, 3.0f, 0.5f, 0.0f, -1.0f, -1.0f, -1.0f, 0.5f, 0.0f, 1.0f, 3.0f, -1.0f, 0.5f, 2.0f, 1.0f,
		};

		uint64_t       screenQuadDataSize = 5 * 3 * sizeof(float);
		BufferLoadDesc screenQuadVbDesc = {};
		screenQuadVbDesc.mDesc.pName = "screenQuadVb";
		screenQuadVbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		screenQuadVbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		screenQuadVbDesc.mDesc.mStartState = RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		screenQuadVbDesc.mDesc.mSize = screenQuadDataSize;
		screenQuadVbDesc.pData = screenQuadPoints;
		screenQuadVbDesc.ppBuffer = &pScreenQuadVertexBuffer;
		addResource(&screenQuadVbDesc, NULL);

		gVertexLayoutModel.mBindingCount = 1;
		gVertexLayoutModel.mAttribCount = 3;
		
		gVertexLayoutModel.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		gVertexLayoutModel.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
		gVertexLayoutModel.mAttribs[0].mBinding = 0;
		gVertexLayoutModel.mAttribs[0].mLocation = 0;
		gVertexLayoutModel.mAttribs[0].mOffset = 0;

		gVertexLayoutModel.mAttribs[1].mSemantic = SEMANTIC_NORMAL;
		gVertexLayoutModel.mAttribs[1].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
		gVertexLayoutModel.mAttribs[1].mBinding = 0;
		gVertexLayoutModel.mAttribs[1].mLocation = 1;
		gVertexLayoutModel.mAttribs[1].mOffset = sizeof(float) * 3;

		gVertexLayoutModel.mAttribs[2].mSemantic = SEMANTIC_TEXCOORD0;
		gVertexLayoutModel.mAttribs[2].mFormat = TinyImageFormat_R32G32_SFLOAT;
		gVertexLayoutModel.mAttribs[2].mBinding = 0;
		gVertexLayoutModel.mAttribs[2].mLocation = 2;
		gVertexLayoutModel.mAttribs[2].mOffset = sizeof(float) * 6;

		// Update ObjectData	
		gObjectInfo[SPONZA_MODEL].mPosition = float3(0.0f, -5.0f, 0.0f);
		gObjectInfo[SPONZA_MODEL].mRotation = float3(0.0f, -1.5708f, 0.0f);
		gObjectInfo[SPONZA_MODEL].mScale = 0.02f;
	
		gObjectInfo[LION_MODEL].mPosition = float3(0.0f, -5.0f, 5.0f);
		gObjectInfo[LION_MODEL].mRotation = float3(0.0f, 0.0f, 0.0f);
		gObjectInfo[LION_MODEL].mScale = 0.2f;
		gObjectInfo[LION_MODEL].mMaterial = { 81, 83, 6, 6 };
		
		// Load Mesh
		for (uint32_t i = 0; i < MODEL_COUNT; ++i) 
		{
			GeometryLoadDesc geomLoadDesc = {};
			geomLoadDesc.pFileName = gModelNames[i];
			geomLoadDesc.ppGeometry = &gModels[i];
			geomLoadDesc.pVertexLayout = &gVertexLayoutModel;
			addResource(&geomLoadDesc, NULL);
		}

		// Load Texture
		for (uint32_t i = 0; i < TOTAL_IMGS; ++i) {
			TextureLoadDesc texLoadDesc = {};
			texLoadDesc.pFileName = pMaterialImageFileNames[i];
			texLoadDesc.ppTexture = &pMaterialTextures[i];
			if (strstr(pMaterialImageFileNames[i], "Albedo") || strstr(pMaterialImageFileNames[i], "diffuse"))
			{
				// Textures representing color should be stored in SRGB or HDR format
				texLoadDesc.mCreationFlag = TEXTURE_CREATION_FLAG_SRGB;
			}

			addResource(&texLoadDesc, NULL);
		}

		waitForAllResourceLoads();

		// Widget
		// light map draw on/off
		CheckboxWidget boolCheck;
		boolCheck.pData = &bDebugDraw;
		luaRegisterWidget(uiCreateComponentWidget(pGuiWindow, "Debug Draw", &boolCheck, WIDGET_TYPE_CHECKBOX));
		// dynamic light on/off
		boolCheck.pData = &bDynamicLight;
		luaRegisterWidget( uiCreateComponentWidget(pGuiWindow, "Dynamic Light", &boolCheck, WIDGET_TYPE_CHECKBOX));
		
		// light spawn box scale
		SliderFloatWidget floatSlider;
		floatSlider.mMin = 2.0f;
		floatSlider.mMax = 10.0f;
		floatSlider.mStep = 0.1f;
		floatSlider.pData = &gLightSpawnBoxScale;
		luaRegisterWidget(uiCreateComponentWidget(pGuiWindow, "Light Spawn Boundary Scale", &floatSlider, WIDGET_TYPE_SLIDER_FLOAT));

		// num of light slider
		SliderUintWidget numLightSlider;
		numLightSlider.mMin = 1;
		numLightSlider.mMax = MAX_LIGHTS;
		numLightSlider.mStep = 1;
		numLightSlider.pData = &gCurrentLightCount;
		luaRegisterWidget(uiCreateComponentWidget(pGuiWindow, "Number of Lights", &numLightSlider, WIDGET_TYPE_SLIDER_UINT));

		ButtonWidget randLightButton;
		UIWidget* pRandButton = uiCreateComponentWidget(pGuiWindow, "Generate Random lights position", &randLightButton, WIDGET_TYPE_BUTTON);
		uiSetWidgetOnEditedCallback(pRandButton, nullptr, [](void* pUserData) {
			bRandomizePosition = true;
			});
		luaRegisterWidget(pRandButton);

		ButtonWidget scenarioLightButton;
		UIWidget* pScenearioButton = uiCreateComponentWidget(pGuiWindow, "Light Scenario", &scenarioLightButton, WIDGET_TYPE_BUTTON);
		uiSetWidgetOnEditedCallback(pScenearioButton, nullptr, scenarioLightPosition);
		luaRegisterWidget(pScenearioButton);

		// lion position
		SliderFloat3Widget float3Slider;
		float3Slider.mMin = float3(-10.0f);
		float3Slider.mMax = float3(10.0f);
		float3Slider.pData = &gObjectInfo[LION_MODEL].mPosition;
		luaRegisterWidget(uiCreateComponentWidget(pGuiWindow, "Lion Position", &float3Slider, WIDGET_TYPE_SLIDER_FLOAT3));

		// lion rotation
		float3Slider.mMin = float3(-PI);
		float3Slider.mMax = float3(PI);
		float3Slider.pData = &gObjectInfo[LION_MODEL].mRotation;
		luaRegisterWidget(uiCreateComponentWidget(pGuiWindow, "Lion Rotation", &float3Slider, WIDGET_TYPE_SLIDER_FLOAT3));

		// lion uniform scale
		floatSlider.mMin = 0.02f;
		floatSlider.mMax = 2.0f;
		floatSlider.pData = &gObjectInfo[LION_MODEL].mScale;
		luaRegisterWidget(uiCreateComponentWidget(pGuiWindow, "Lion Scale", &floatSlider, WIDGET_TYPE_SLIDER_FLOAT));

		static const char* enumTileCullModeNames[] = { "Basic Deferred Rendering","Tile Culling Baseline", "Tile Culling Half-Z", "Tile Culling Modified - Z"};
		DropdownWidget ddCullMode;
		ddCullMode.pData = &gTileCullMode;
		ddCullMode.pNames = enumTileCullModeNames;
		ddCullMode.mCount = sizeof(enumTileCullModeNames) / sizeof(enumTileCullModeNames[0]);
		luaRegisterWidget(uiCreateComponentWidget(pGuiWindow, "Render Mode", &ddCullMode, WIDGET_TYPE_DROPDOWN));

		// Camera Control & Input setting
		{
			CameraMotionParameters cmp{ 16.0f, 60.0f, 20.0f };
			vec3                   camPos{ 0.0f, 0.0f, -25.0f };
			vec3                   lookAt{ vec3(0, 0.0f, 1.0f) };

			pCameraController = initFpsCameraController(camPos, lookAt);

			pCameraController->setMotionParameters(cmp);

			InputSystemDesc inputDesc = {};
			inputDesc.pRenderer = pRenderer;
			inputDesc.pWindow = pWindow;
			if (!initInputSystem(&inputDesc))
				return false;

			// App Actions
			InputActionDesc actionDesc = { DefaultInputActions::DUMP_PROFILE_DATA, [](InputActionContext* ctx) {  dumpProfileData(((Renderer*)ctx->pUserData)->pName); return true; }, pRenderer };
			addInputAction(&actionDesc);
			actionDesc = { DefaultInputActions::TOGGLE_FULLSCREEN, [](InputActionContext* ctx) {
				WindowDesc* winDesc = ((IApp*)ctx->pUserData)->pWindow;
				if (winDesc->fullScreen)
					winDesc->borderlessWindow ? setBorderless(winDesc, getRectWidth(&winDesc->clientRect), getRectHeight(&winDesc->clientRect)) : setWindowed(winDesc, getRectWidth(&winDesc->clientRect), getRectHeight(&winDesc->clientRect));
				else
					setFullscreen(winDesc);
				return true;
			}, this };
			addInputAction(&actionDesc);
			actionDesc = { DefaultInputActions::EXIT, [](InputActionContext* ctx) { requestShutdown(); return true; } };
			addInputAction(&actionDesc);
			InputActionCallback onAnyInput = [](InputActionContext* ctx)
			{
				if (ctx->mActionId > UISystemInputActions::UI_ACTION_START_ID_)
				{
					uiOnInput(ctx->mActionId, ctx->mBool, ctx->pPosition, &ctx->mFloat2);
				}

				return true;
			};

			typedef bool(*CameraInputHandler)(InputActionContext* ctx, uint32_t index);
			static CameraInputHandler onCameraInput = [](InputActionContext* ctx, uint32_t index)
			{
				if (*(ctx->pCaptured))
				{
					float2 delta = uiIsFocused() ? float2(0.f, 0.f) : ctx->mFloat2;
					index ? pCameraController->onRotate(delta) : pCameraController->onMove(delta);
				}
				return true;
			};
			actionDesc = { DefaultInputActions::CAPTURE_INPUT, [](InputActionContext* ctx) {setEnableCaptureInput(!uiIsFocused() && INPUT_ACTION_PHASE_CANCELED != ctx->mPhase);	return true; }, NULL };
			addInputAction(&actionDesc);
			actionDesc = { DefaultInputActions::ROTATE_CAMERA, [](InputActionContext* ctx) { return onCameraInput(ctx, 1); }, NULL };
			addInputAction(&actionDesc);
			actionDesc = { DefaultInputActions::TRANSLATE_CAMERA, [](InputActionContext* ctx) { return onCameraInput(ctx, 0); }, NULL };
			addInputAction(&actionDesc);
			actionDesc = { DefaultInputActions::RESET_CAMERA, [](InputActionContext* ctx) { if (!uiWantTextInput()) pCameraController->resetView(); return true; } };
			addInputAction(&actionDesc);
			GlobalInputActionDesc globalInputActionDesc = { GlobalInputActionDesc::ANY_BUTTON_ACTION, onAnyInput, this };
			setGlobalInputAction(&globalInputActionDesc);
		}

		assignSponzaTextures();

		gFrameIndex = 0; 

		return true;
	}

	void Exit()
	{
		exitInputSystem();

		exitCameraController(pCameraController);

		exitUserInterface();

		exitFontSystem();

		// Exit profile
		exitProfiler();
		
		// Remove Uniform Buffer
		for(uint32_t i = 0; i < gDataBufferCount; ++i) 
		{
			removeResource(pCameraBuffer[i]);
			removeResource(pExtCameraBuffer[i]);
			removeResource(pTileCullDataBuffer[i]);
			removeResource(pLightPosAndRadiusBuffer[i]);
			removeResource(pLightColorAndIntensityBuffer[i]);
		}


		// Remove Geomtry
		for (uint32_t i = 0; i < MODEL_COUNT; ++i) 
		{
			removeResource(gModels[i]);
		}

		removeResource(pScreenQuadVertexBuffer);

		// Remove Texture
		for (uint32_t i = 0; i < TOTAL_IMGS; ++i) 
		{
			if (pMaterialTextures[i])
			{
				removeResource(pMaterialTextures[i]);
			}
		}

		removeSampler(pRenderer, pSamplerBilinear);

		exitResourceLoaderInterface(pRenderer);
		exitScreenshotInterface();

		removeSemaphore(pRenderer, pImageAcquiredSemaphore);
		removeGpuCmdRing(pRenderer, &gGraphicsCmdRing);
		removeQueue(pRenderer, pGraphicsQueue);

		exitRenderer(pRenderer);
		pRenderer = NULL; 
	}

	bool Load(ReloadDesc* pReloadDesc)
	{
		if (pReloadDesc->mType & RELOAD_TYPE_SHADER)
		{
			addShaders();
			addRootSignatures();
			addDescriptorSets();
		}

		if (pReloadDesc->mType & (RELOAD_TYPE_RESIZE | RELOAD_TYPE_RENDERTARGET))
		{
			if (!addSwapChain())
				return false;

			if (!addDepthBuffer())
				return false;

			if (!addGBuffers())
				return false;

			if (!addSceneBuffer())
				return false;

		}

		if (pReloadDesc->mType & (RELOAD_TYPE_SHADER | RELOAD_TYPE_RENDERTARGET))
		{
			addPipelines();
		}

		prepareDescriptorSets();

		UserInterfaceLoadDesc uiLoad = {};
		uiLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
		uiLoad.mHeight = mSettings.mHeight;
		uiLoad.mWidth = mSettings.mWidth;
		uiLoad.mLoadType = pReloadDesc->mType;
		loadUserInterface(&uiLoad);

		FontSystemLoadDesc fontLoad = {};
		fontLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
		fontLoad.mHeight = mSettings.mHeight;
		fontLoad.mWidth = mSettings.mWidth;
		fontLoad.mLoadType = pReloadDesc->mType;
		loadFontSystem(&fontLoad);

		return true;
	}

	void Unload(ReloadDesc* pReloadDesc)
	{
		waitQueueIdle(pGraphicsQueue);

		unloadFontSystem(pReloadDesc->mType);
		unloadUserInterface(pReloadDesc->mType);

		if (pReloadDesc->mType & (RELOAD_TYPE_SHADER | RELOAD_TYPE_RENDERTARGET))
		{
			removePipelines();
		}

		if (pReloadDesc->mType & (RELOAD_TYPE_RESIZE | RELOAD_TYPE_RENDERTARGET))
		{
			removeSwapChain(pRenderer, pSwapChain);
			removeRenderTarget(pRenderer, pDepthBuffer);
			removeRenderTarget(pRenderer, pSceneBuffer);

			for (uint32_t i = 0; i < DEFERRED_RT_COUNT; ++i)
			{
				removeRenderTarget(pRenderer, pGbufferRenderTargets[i]);
			}
		}

		if (pReloadDesc->mType & RELOAD_TYPE_SHADER)
		{
			removeDescriptorSets();
			removeRootSignatures();
			removeShaders();
		}
	}

	void updateLightPosition(float deltaTime)
	{
		static float currentTime = 0.0f;
		currentTime += deltaTime * 10.0f;
		
		// rotate based on initial Light position(gInitLightPos) with speed(10.0f)
		for (uint32_t i = 0; i < gUniformTileCullData.mNumOfLights; ++i)
			gLightPositionAndRadius[i].setXYZ(vec3(gInitLightPos[i].x + cos(degToRad(currentTime)) * gInitLightPos[i].y, gInitLightPos[i].y + sin(degToRad(currentTime)) * gInitLightPos[i].x, gInitLightPos[i].z));

		// update the light buffer until the next 2 frames.
		gLightFrameCount = 0;
	}

	/**
	 * @brief Updates light data with randomized value inside a unit cube.
	 */
	void randomizeLightPosition()
	{
		std::mt19937 mt;
		std::normal_distribution<float> distribution(0.0f, 2.0f);
		gUniformTileCullData.mNumOfLights = gCurrentLightCount;
		
		// generate N points in unit cube
		for (uint32_t i = 0; i < gCurrentLightCount; ++i)
		{
			float3 v(distribution(mt), distribution(mt), distribution(mt));
			gInitLightPos[i] = float3(v * gLightSpawnBoxScale);
			gLightPositionAndRadius[i] = vec4(gInitLightPos[i].x + gInitLightPos[i].y, gInitLightPos[i].y, gInitLightPos[i].z,  randomFloat(0.0f, 3.0f));
			gLightColorAndIntensity[i] = vec4(abs(v[0]), abs(v[1]), abs(v[2]), 1.0f);
		}

		// set to light frame count = 0, and update the light buffer per frame
		gLightFrameCount = 0;
		bRandomizePosition = false;
	}
	
	void Update(float deltaTime)
	{
		updateInputSystem(deltaTime, mSettings.mWidth, mSettings.mHeight);

		pCameraController->update(deltaTime);
		/************************************************************************/
		// Scene Update
		/************************************************************************/
		// Light moving dynamically
		// Update if it's dynamic light or if it's first frame after randomization
		if (bRandomizePosition)
			randomizeLightPosition(); // change initial position of lights

		if (bDynamicLight)
			updateLightPosition(deltaTime); // rotate light based on the initial position of lights

		// update camera 
		const float aspectInverse = (float)mSettings.mHeight / (float)mSettings.mWidth;
		const float horizontal_fov = PI / 2.0f;
		mat4 viewMat = pCameraController->getViewMatrix();
		mat4 projMat = mat4::perspectiveLH_ReverseZ(horizontal_fov, aspectInverse, 0.1f, 1000.0f);
		gUniformCamData.mCamPos = pCameraController->getViewPosition();
		gUniformCamData.mProjectView = projMat * viewMat;
		gUniformCamData.mProjectViewInv = inverse(gUniformCamData.mProjectView);

		mat4 viewPortMat = mat4(
			Vector4(2.0f / (float)mSettings.mWidth, 0.0f, 0.0f, 0.0f),
			Vector4(0.0f, -2.0f / (float)mSettings.mHeight, 0.0f, 0.0f),
			Vector4(0.0f, 0.0f, 1.0f, 0.0f),
			Vector4(-1.0f, 1.0f, 0.0f, 1.0f)
		);

		gUniformExtCamData.mView = viewMat;
		gUniformExtCamData.mProjectView = gUniformCamData.mProjectView;
		gUniformExtCamData.mProjectInv = inverse(projMat);
		gUniformExtCamData.mProjectViewInvViewport = gUniformCamData.mProjectViewInv * viewPortMat;
		gUniformExtCamData.mCamPos = gUniformCamData.mCamPos;

		gUniformTileCullData.mNumTilesX = (mSettings.mWidth + TILE_RES - 1) / TILE_RES;
		gUniformTileCullData.mNumTilesY = (mSettings.mHeight + TILE_RES - 1) / TILE_RES;
		gUniformTileCullData.mDebugDraw = bDebugDraw ? 1 : 0;
		gUniformTileCullData.mResolution = uint2(mSettings.mWidth, mSettings.mHeight);
	}

	void Draw()
	{
		if (pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
		{
			waitQueueIdle(pGraphicsQueue);
			::toggleVSync(pRenderer, &pSwapChain);
		}

		uint32_t swapchainImageIndex;
		acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore, NULL, &swapchainImageIndex);

		RenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];

		GpuCmdRingElement elem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);

		// Stall if CPU is running "gDataBufferCount" frames ahead of GPU
		FenceStatus fenceStatus;
		getFenceStatus(pRenderer, elem.pFence, &fenceStatus);
		if (fenceStatus == FENCE_STATUS_INCOMPLETE)
			waitForFences(pRenderer, 1, &elem.pFence);

		// Reset cmd pool for this frame
		resetCmdPool(pRenderer, elem.pCmdPool);

		// Update uniform buffers
		// camera ubo update
		BufferUpdateDesc camBuffUpdateDesc = {};
		camBuffUpdateDesc.pBuffer = pCameraBuffer[gFrameIndex];
		beginUpdateResource(&camBuffUpdateDesc);
		*(UniformCamData*)camBuffUpdateDesc.pMappedData = gUniformCamData;
		endUpdateResource(&camBuffUpdateDesc, NULL);

		// ext camera ubo update
		BufferUpdateDesc extCamBuffUpdateDesc = {};
		extCamBuffUpdateDesc.pBuffer = pExtCameraBuffer[gFrameIndex];
		beginUpdateResource(&extCamBuffUpdateDesc);
		*(UniformExtCamData*)extCamBuffUpdateDesc.pMappedData = gUniformExtCamData;
		endUpdateResource(&extCamBuffUpdateDesc, NULL);

		if (bDynamicLight || (gDataBufferCount > gLightFrameCount))
		{
			// update light buffer
			BufferUpdateDesc lightColorBuffUpdateDesc = { pLightColorAndIntensityBuffer[gFrameIndex] };
			beginUpdateResource(&lightColorBuffUpdateDesc);
			memcpy_s(lightColorBuffUpdateDesc.pMappedData, sizeof(gLightColorAndIntensity), gLightColorAndIntensity, sizeof(gLightColorAndIntensity));
			endUpdateResource(&lightColorBuffUpdateDesc, NULL);

			BufferUpdateDesc lightPosBuffUpdateDesc = { pLightPosAndRadiusBuffer[gFrameIndex] };
			beginUpdateResource(&lightPosBuffUpdateDesc);
			memcpy_s(lightPosBuffUpdateDesc.pMappedData, sizeof(gLightPositionAndRadius), gLightPositionAndRadius, sizeof(gLightPositionAndRadius));
			endUpdateResource(&lightPosBuffUpdateDesc, NULL);

			++gLightFrameCount;
		}
		
		if (gTileCullMode != NON_TILE)
		{
			// tile (light cull) ubo update
			BufferUpdateDesc tileCullBuffUpdateDesc = {};
			tileCullBuffUpdateDesc.pBuffer = pTileCullDataBuffer[gFrameIndex];
			beginUpdateResource(&tileCullBuffUpdateDesc);
			*(UniformTileCullData*)tileCullBuffUpdateDesc.pMappedData = gUniformTileCullData;
			endUpdateResource(&tileCullBuffUpdateDesc, NULL);
		}


		Cmd* cmd = elem.pCmds[0];
		beginCmd(cmd);

		cmdBeginGpuFrameProfile(cmd, gGpuProfileToken, true);

		// Transfer G-buffers to render target state
		RenderTargetBarrier rtBarriers[DEFERRED_RT_COUNT + 2] = {
			{ pGbufferRenderTargets[0], RESOURCE_STATE_SHADER_RESOURCE, RESOURCE_STATE_RENDER_TARGET },
			{ pGbufferRenderTargets[1], RESOURCE_STATE_SHADER_RESOURCE, RESOURCE_STATE_RENDER_TARGET },
			{ pDepthBuffer, RESOURCE_STATE_SHADER_RESOURCE, RESOURCE_STATE_DEPTH_WRITE }
		};
		
		uint32_t rtBarrierCount = DEFERRED_RT_COUNT + 1;

		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, rtBarrierCount, rtBarriers);

		// Clear G-buffers and Depth buffer
		LoadActionsDesc loadActions = {};
		loadActions.mLoadActionsColor[0] = LOAD_ACTION_CLEAR;
		loadActions.mClearColorValues[0] = pGbufferRenderTargets[0]->mClearValue;
		loadActions.mLoadActionDepth = LOAD_ACTION_CLEAR;
		loadActions.mClearDepth.depth = 0.0f;
		loadActions.mClearDepth.stencil = 0;
		cmdBindRenderTargets(cmd, DEFERRED_RT_COUNT, pGbufferRenderTargets, pDepthBuffer, &loadActions, NULL, NULL, -1, -1);
		cmdSetViewport(cmd, 0.0f, 0.0f, (float)pGbufferRenderTargets[0]->mWidth, (float)pGbufferRenderTargets[0]->mHeight, 0.0f, 1.0f);
		cmdSetScissor(cmd, 0, 0, pGbufferRenderTargets[0]->mWidth, pGbufferRenderTargets[0]->mHeight);

		cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Fill Gbuffers");
		cmdBindPipeline(cmd, pGbufferPipeline);

		cmdBindDescriptorSet(cmd, 0, pDescriptorSetGbuffers[0]); // textureMap
		cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetGbuffers[1]); // cameraUBO, objectUBO

		// Draw Model ([0] = sponza, [1, model_count - 1] = single meshes)
		{
			static ConstantObjData gConstantObjData = {}; // push constant data per draw call
			static uint32_t constantSize = sizeof(ConstantObjData);
			static const uint32_t drawCount = (uint32_t)gModels[0]->mDrawArgCount;
			gConstantObjData.mWorldMat = mat4::translation(f3Tov3(gObjectInfo[0].mPosition)) * mat4::rotationZYX(f3Tov3(gObjectInfo[0].mRotation)) * mat4::scale(vec3(gObjectInfo[0].mScale));
			
			//Draw Sponza
			cmdBindVertexBuffer(cmd, 1, gModels[0]->pVertexBuffers, gModels[0]->mVertexStrides, NULL);
			cmdBindIndexBuffer(cmd, gModels[0]->pIndexBuffer, gModels[0]->mIndexType, NULL);
			for (uint32_t i = 0; i < drawCount; ++i)
			{
				int materialID = gMaterialIds[i];

				gConstantObjData.mMaterialId = ((gSponzaTextureIndexForMaterial[materialID][0] & 0xFF) << 0) |
					((gSponzaTextureIndexForMaterial[materialID][1] & 0xFF) << 8) |
					((gSponzaTextureIndexForMaterial[materialID][2] & 0xFF) << 16) |
					((gSponzaTextureIndexForMaterial[materialID][3] & 0xFF) << 24);

				cmdBindPushConstants(cmd, pGbufferRootSignature, gModelIdRootConstantIndex, &gConstantObjData);
				IndirectDrawIndexArguments& cmdData = gModels[0]->pDrawArgs[i];
				cmdDrawIndexed(cmd, cmdData.mIndexCount, cmdData.mStartIndex, cmdData.mVertexOffset);
			}

			for (uint32_t i = 1; i < MODEL_COUNT; ++i) {
				gConstantObjData.mWorldMat = mat4::translation(f3Tov3(gObjectInfo[i].mPosition)) * mat4::rotationZYX(f3Tov3(gObjectInfo[i].mRotation)) * mat4::scale(vec3(gObjectInfo[i].mScale));

				gConstantObjData.mMaterialId = ((gObjectInfo[i].mMaterial.albedoIndex & 0xFF) << 0) |
					((gObjectInfo[i].mMaterial.normalIndex & 0xFF) << 8) |
					((gObjectInfo[i].mMaterial.metallicIndex & 0xFF) << 16) |
					((gObjectInfo[i].mMaterial.roughnessIndex & 0xFF) << 24);

				cmdBindPushConstants(cmd, pGbufferRootSignature, gModelIdRootConstantIndex, &gConstantObjData);
				cmdBindVertexBuffer(cmd, 1, &gModels[i]->pVertexBuffers[0], &gModels[i]->mVertexStrides[0], NULL);
				cmdBindIndexBuffer(cmd, gModels[i]->pIndexBuffer, gModels[i]->mIndexType, 0);
				cmdDrawIndexed(cmd, gModels[i]->mIndexCount, 0, 0);
			}
		}
		
		cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
		cmdBindRenderTargets(cmd, 0, NULL, NULL, NULL, NULL, NULL, -1, -1);

		if (gTileCullMode != NON_TILE)
		{
			rtBarriers[0] = { pGbufferRenderTargets[0], RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_SHADER_RESOURCE };
			rtBarriers[1] = { pGbufferRenderTargets[1], RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_SHADER_RESOURCE };
			rtBarriers[2] = { pSceneBuffer, RESOURCE_STATE_SHADER_RESOURCE, RESOURCE_STATE_UNORDERED_ACCESS };
			rtBarriers[3] = { pDepthBuffer, RESOURCE_STATE_DEPTH_WRITE, RESOURCE_STATE_SHADER_RESOURCE };
			rtBarrierCount = 4;

			cmdResourceBarrier(cmd, 0, NULL, 0, NULL, rtBarrierCount, rtBarriers);

			// Light Cull 
			cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Light Culling Compute");


			if (gTileCullMode == TILE_BASE)
			{
				cmdBindPipeline(cmd, pTiledCullPipeline);
			}
			else if (gTileCullMode == TILE_HALFZ)
			{
				cmdBindPipeline(cmd, pTiledCullHalfZPipeline);
			}
			else //if(gTileCullMode == TILE_MODIFIED_Z)
			{
				cmdBindPipeline(cmd, pTiledCullModifiedZPipeline);
			}

			cmdBindDescriptorSet(cmd, 0, pDescriptorSetCullPass[0]);
			cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetCullPass[1]);
			cmdDispatch(cmd, gUniformTileCullData.mNumTilesX, gUniformTileCullData.mNumTilesY, 1);

			cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

			rtBarriers[0] = { pRenderTarget, RESOURCE_STATE_PRESENT, RESOURCE_STATE_RENDER_TARGET };
			rtBarriers[1] = { pSceneBuffer, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_SHADER_RESOURCE };
			rtBarrierCount = 2;
			cmdResourceBarrier(cmd, 0, NULL, 0, NULL, rtBarrierCount, rtBarriers);

			// Render Quad
			loadActions = {};
			loadActions.mLoadActionsColor[0] = LOAD_ACTION_LOAD;
			cmdBindRenderTargets(cmd, 1, &pRenderTarget, nullptr, &loadActions, NULL, NULL, -1, -1);
			cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
			cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

			cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Render Quad");

			const uint32_t quadStride = sizeof(float) * 5;
			cmdBindPipeline(cmd, pRenderQuadPipeline);
			cmdBindDescriptorSet(cmd, 0, pDescritporSetRenderQuad);
			cmdBindVertexBuffer(cmd, 1, &pScreenQuadVertexBuffer, &quadStride, NULL);
			cmdDraw(cmd, 3, 0);

			cmdEndGpuFrameProfile(cmd, gGpuProfileToken);
		}
		else // Deferred Rendering
		{
			rtBarriers[0] = { pGbufferRenderTargets[0], RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_SHADER_RESOURCE };
			rtBarriers[1] = { pGbufferRenderTargets[1], RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_SHADER_RESOURCE };
			rtBarriers[2] = { pDepthBuffer, RESOURCE_STATE_DEPTH_WRITE, RESOURCE_STATE_SHADER_RESOURCE };
			rtBarriers[3] = { pRenderTarget, RESOURCE_STATE_PRESENT, RESOURCE_STATE_RENDER_TARGET };
			rtBarrierCount = 4;

			cmdResourceBarrier(cmd, 0, NULL, 0, NULL, rtBarrierCount, rtBarriers);
			cmdBindRenderTargets(cmd, 1, &pRenderTarget, nullptr, &loadActions, NULL, NULL, -1, -1);

			// Light Pass 
			cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Deferred Rendering: Light Pass");

			const uint32_t quadStride = sizeof(float) * 5;
			cmdBindPipeline(cmd, pDeferredPipeline);
			cmdBindDescriptorSet(cmd, 0, pDescriptorSetDeferredLightPass[0]);
			cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetDeferredLightPass[1]);
			cmdBindPushConstants(cmd, pDeferredRootSignature, gLightCountRootConstantIndex, &gUniformTileCullData.mNumOfLights);
			cmdBindVertexBuffer(cmd, 1, &pScreenQuadVertexBuffer, &quadStride, NULL);
			cmdDraw(cmd, 3, 0);

			cmdEndGpuFrameProfile(cmd, gGpuProfileToken);
		}


		cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw UI");

		gFrameTimeDraw.mFontColor = 0xff00ffff;
		gFrameTimeDraw.mFontSize = 18.0f;
		gFrameTimeDraw.mFontID = gFontID;
		float2 txtSizePx = cmdDrawCpuProfile(cmd, float2(8.f, 15.f), &gFrameTimeDraw);
		cmdDrawGpuProfile(cmd, float2(8.f, txtSizePx.y + 75.f), gGpuProfileToken, &gFrameTimeDraw);

		cmdDrawUserInterface(cmd);

		cmdBindRenderTargets(cmd, 0, NULL, NULL, NULL, NULL, NULL, -1, -1);
		cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

		rtBarriers[0] = { pRenderTarget, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PRESENT };
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, rtBarriers);

		cmdEndGpuFrameProfile(cmd, gGpuProfileToken);
		endCmd(cmd);

		QueueSubmitDesc submitDesc = {};
		submitDesc.mCmdCount = 1;
		submitDesc.mSignalSemaphoreCount = 1;
		submitDesc.mWaitSemaphoreCount = 1;
		submitDesc.ppCmds = &cmd;
		submitDesc.ppSignalSemaphores = &elem.pSemaphore;
		submitDesc.ppWaitSemaphores = &pImageAcquiredSemaphore;
		submitDesc.pSignalFence = elem.pFence;
		queueSubmit(pGraphicsQueue, &submitDesc);
		QueuePresentDesc presentDesc = {};
		presentDesc.mIndex = swapchainImageIndex;
		presentDesc.mWaitSemaphoreCount = 1;
		presentDesc.pSwapChain = pSwapChain;
		presentDesc.ppWaitSemaphores = &elem.pSemaphore;
		presentDesc.mSubmitDone = true;

		// captureScreenshot() must be used before presentation.
		if (gTakeScreenshot)
		{
			// Metal platforms need one renderpass to prepare the swapchain textures for copy.
			if(prepareScreenshot(pSwapChain))
			{
				captureScreenshot(pSwapChain, swapchainImageIndex, RESOURCE_STATE_PRESENT, "00_TiledDeferredRendering.png");
				gTakeScreenshot = false;
			}
		}
		
		queuePresent(pGraphicsQueue, &presentDesc);

		flipProfiler();

		gFrameIndex = (gFrameIndex + 1) % gDataBufferCount;
	}

	const char* GetName() { return "00_Austyn_Park_UnitTest"; }

	bool addSwapChain()
	{
		SwapChainDesc swapChainDesc = {};
		swapChainDesc.mWindowHandle = pWindow->handle;
		swapChainDesc.mPresentQueueCount = 1;
		swapChainDesc.ppPresentQueues = &pGraphicsQueue;
		swapChainDesc.mWidth = mSettings.mWidth;
		swapChainDesc.mHeight = mSettings.mHeight;
		swapChainDesc.mImageCount = gDataBufferCount;
		swapChainDesc.mColorFormat = getRecommendedSwapchainFormat(true, true);
		swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;
        swapChainDesc.mFlags = SWAP_CHAIN_CREATION_FLAG_ENABLE_FOVEATED_RENDERING_VR;
		::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

		return pSwapChain != NULL;
	}

	bool addDepthBuffer()
	{
		// Add depth buffer
		RenderTargetDesc depthRT = {};
		depthRT.mArraySize = 1;
		depthRT.mClearValue.depth = 0.0f;
		depthRT.mClearValue.stencil = 0;
		depthRT.mDepth = 1;
		depthRT.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
		depthRT.mFormat = TinyImageFormat_D32_SFLOAT;
		depthRT.mStartState = RESOURCE_STATE_SHADER_RESOURCE;
		depthRT.mHeight = mSettings.mHeight;
		depthRT.mSampleCount = SAMPLE_COUNT_1;
		depthRT.mSampleQuality = 0;
		depthRT.mWidth = mSettings.mWidth;
		//depthRT.mFlags = TEXTURE_CREATION_FLAG_ON_TILE | TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
		depthRT.pName = "Depth Buffer";
		addRenderTarget(pRenderer, &depthRT, &pDepthBuffer);

		return pDepthBuffer != NULL;
	}

	bool addGBuffers()
	{
		RenderTargetDesc deferredRT = {};
		deferredRT.mArraySize = 1;
		deferredRT.mClearValue = { {0.0f, 0.0f, 0.0f, 0.0f} };
		deferredRT.mDepth = 1;
		deferredRT.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
		deferredRT.mWidth = mSettings.mWidth;
		deferredRT.mHeight = mSettings.mHeight;
		deferredRT.mSampleCount = SAMPLE_COUNT_1;
		deferredRT.mSampleQuality = 0;
		deferredRT.mStartState = RESOURCE_STATE_SHADER_RESOURCE;
		deferredRT.pName = "G-Buffer RTs";
		deferredRT.mFormat = TinyImageFormat_R8G8B8A8_SRGB;

		for (uint32_t i = 0; i < DEFERRED_RT_COUNT; ++i)
		{
			if (i == 1)
				deferredRT.mFormat = TinyImageFormat_R16G16B16A16_SFLOAT;

			addRenderTarget(pRenderer, &deferredRT, &pGbufferRenderTargets[i]);
		}

		for (uint32_t i = 0; i < DEFERRED_RT_COUNT; ++i)
		{
			if (pGbufferRenderTargets[i] == NULL)
				return false;
		}

		return true;
	}

	bool addSceneBuffer()
	{
		RenderTargetDesc sceneRT = {};
		sceneRT.mArraySize = 1;
		sceneRT.mClearValue = { {0.0f, 0.0f, 0.0f, 0.0f} };
		sceneRT.mDepth = 1;
		sceneRT.mDescriptors = DESCRIPTOR_TYPE_TEXTURE | DESCRIPTOR_TYPE_RW_TEXTURE;
		sceneRT.mFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
		sceneRT.mStartState = RESOURCE_STATE_SHADER_RESOURCE;

		sceneRT.mHeight = mSettings.mHeight;
		sceneRT.mWidth = mSettings.mWidth;

		sceneRT.mSampleCount = SAMPLE_COUNT_1;
		sceneRT.mSampleQuality = 0;
		sceneRT.pName = "Scene Buffer";

		addRenderTarget(pRenderer, &sceneRT, &pSceneBuffer);

		return pSceneBuffer != NULL;
	}

	void addDescriptorSets()
	{
		DescriptorSetDesc desc = { pGbufferRootSignature, DESCRIPTOR_UPDATE_FREQ_NONE, 1 };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetGbuffers[0]);
		desc = { pGbufferRootSignature, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gDataBufferCount };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetGbuffers[1]);

		desc = { pRenderQuadRootSignature, DESCRIPTOR_UPDATE_FREQ_NONE, 1 };
		addDescriptorSet(pRenderer, &desc, &pDescritporSetRenderQuad);

		desc = { pTiledCullRootSignature, DESCRIPTOR_UPDATE_FREQ_NONE, 1 };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetCullPass[0]);
		desc = { pTiledCullRootSignature, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gDataBufferCount };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetCullPass[1]);

		desc = { pDeferredRootSignature, DESCRIPTOR_UPDATE_FREQ_NONE, 1 };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetDeferredLightPass[0]);
		desc = { pDeferredRootSignature, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gDataBufferCount };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetDeferredLightPass[1]);
	}

	void removeDescriptorSets()
	{
		removeDescriptorSet(pRenderer, pDescriptorSetGbuffers[0]);
		removeDescriptorSet(pRenderer, pDescriptorSetGbuffers[1]);
		removeDescriptorSet(pRenderer, pDescritporSetRenderQuad);

		removeDescriptorSet(pRenderer, pDescriptorSetCullPass[0]);
		removeDescriptorSet(pRenderer, pDescriptorSetCullPass[1]);

		removeDescriptorSet(pRenderer, pDescriptorSetDeferredLightPass[0]);
		removeDescriptorSet(pRenderer, pDescriptorSetDeferredLightPass[1]);
	}

	void addRootSignatures()
	{
		const char* pStaticSamplersNames[] = { "defaultSampler" };
		Sampler* pStaticSamplers[] = { pSamplerBilinear };
		RootSignatureDesc rootDesc = {};

		// Gbuffer
		{
			rootDesc.ppShaders = &pGbufferShader;
			rootDesc.mShaderCount = 1;
			rootDesc.mStaticSamplerCount = 1;
			rootDesc.ppStaticSamplerNames = pStaticSamplersNames;
			rootDesc.ppStaticSamplers = pStaticSamplers;
			rootDesc.mMaxBindlessTextures = TOTAL_IMGS;

			addRootSignature(pRenderer, &rootDesc, &pGbufferRootSignature);
			gModelIdRootConstantIndex = getDescriptorIndexFromName(pGbufferRootSignature, "cbModelIdRootConstants");
		}

		// RenderQuad
		{
			rootDesc = {};
			rootDesc.ppShaders = &pRenderQuadShader;
			rootDesc.mShaderCount = 1;
			rootDesc.mStaticSamplerCount = 1;
			rootDesc.ppStaticSamplerNames = pStaticSamplersNames;
			rootDesc.ppStaticSamplers = pStaticSamplers;

			addRootSignature(pRenderer, &rootDesc, &pRenderQuadRootSignature);
		}

		{
			rootDesc = {};
			rootDesc.ppShaders = &pDeferredShader;
			rootDesc.mShaderCount = 1;
			rootDesc.mStaticSamplerCount = 1;
			rootDesc.ppStaticSamplerNames = pStaticSamplersNames;
			rootDesc.ppStaticSamplers = pStaticSamplers;

			addRootSignature(pRenderer, &rootDesc, &pDeferredRootSignature);
			gLightCountRootConstantIndex = getDescriptorIndexFromName(pDeferredRootSignature, "cbLightCountRootConstants");
		}

		// Depth Bound & Light Culling compute shader
		{
			Shader* shaders[] = {
				pTiledCullShader,
				pTiledCullHalfZShader,
				pTiledCullModifiedZShader
			};

			rootDesc = {};
			rootDesc.ppShaders = shaders;
			rootDesc.mShaderCount = sizeof(shaders) / sizeof(shaders[0]);
			addRootSignature(pRenderer, &rootDesc, &pTiledCullRootSignature);
		}
	}

	void removeRootSignatures()
	{
		removeRootSignature(pRenderer, pGbufferRootSignature);
		removeRootSignature(pRenderer, pRenderQuadRootSignature);
		removeRootSignature(pRenderer, pTiledCullRootSignature);
		removeRootSignature(pRenderer, pDeferredRootSignature);
	}

	void addShaders()
	{
		ShaderLoadDesc fillGbufferShader = {};
		fillGbufferShader.mStages[0].pFileName = "fillGbuffer.vert";
		fillGbufferShader.mStages[1].pFileName = "fillGbuffer.frag";
		addShader(pRenderer, &fillGbufferShader, &pGbufferShader);

		ShaderLoadDesc renderQuadShader = {};
		renderQuadShader.mStages[0].pFileName = "renderQuad.vert";
		renderQuadShader.mStages[1].pFileName = "renderQuad.frag";
		addShader(pRenderer, &renderQuadShader, &pRenderQuadShader);

		ShaderLoadDesc lightCullingShader = {};
		lightCullingShader.mStages[0].pFileName = "TiledCullBaseline.comp";
		addShader(pRenderer, &lightCullingShader, &pTiledCullShader);

		lightCullingShader.mStages[0].pFileName = "TiledCullHalfZ.comp";
		addShader(pRenderer, &lightCullingShader, &pTiledCullHalfZShader);
		
		lightCullingShader.mStages[0].pFileName = "TiledCullModifiedZ.comp";
		addShader(pRenderer, &lightCullingShader, &pTiledCullModifiedZShader);

		ShaderLoadDesc lightPassShader = {};
		lightPassShader.mStages[0].pFileName = "deferredLighting.vert";
		lightPassShader.mStages[1].pFileName = "deferredLighting.frag";
		addShader(pRenderer, &lightPassShader, &pDeferredShader);
	}

	void removeShaders()
	{
		removeShader(pRenderer, pGbufferShader);
		removeShader(pRenderer, pRenderQuadShader);
		removeShader(pRenderer, pTiledCullShader);
		removeShader(pRenderer, pTiledCullHalfZShader);
		removeShader(pRenderer, pTiledCullModifiedZShader);
		removeShader(pRenderer, pDeferredShader);
	}

	void addPipelines()
	{
		RasterizerStateDesc rasterizerStateDesc = {};
		rasterizerStateDesc.mCullMode = CULL_MODE_FRONT;
		rasterizerStateDesc.mFrontFace = FRONT_FACE_CCW;

		RasterizerStateDesc rasterizerNonStateDesc = {};
		rasterizerNonStateDesc.mCullMode = CULL_MODE_NONE;

		DepthStateDesc depthStateDesc = {};
		depthStateDesc.mDepthTest = true;
		depthStateDesc.mDepthWrite = true;
		// Use CMP_GEQUAL for perspectiveReverseZ
		depthStateDesc.mDepthFunc = CMP_GEQUAL;

		TinyImageFormat deferredFormats[DEFERRED_RT_COUNT] = {};
		for (uint32_t i = 0; i < DEFERRED_RT_COUNT; ++i)
		{
			deferredFormats[i] = pGbufferRenderTargets[i]->mFormat;
		}
		
		{
			// Gbuffer
			PipelineDesc desc = {};
			desc.mType = PIPELINE_TYPE_GRAPHICS;
			GraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
			pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
			pipelineSettings.mRenderTargetCount = DEFERRED_RT_COUNT;
			pipelineSettings.pDepthState = &depthStateDesc;
			pipelineSettings.pColorFormats = deferredFormats;

			pipelineSettings.mSampleCount = pGbufferRenderTargets[0]->mSampleCount;
			pipelineSettings.mSampleQuality = pGbufferRenderTargets[0]->mSampleQuality;
			pipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
			pipelineSettings.pRootSignature = pGbufferRootSignature;
			pipelineSettings.pShaderProgram = pGbufferShader;
			pipelineSettings.pVertexLayout = &gVertexLayoutModel;
			pipelineSettings.pRasterizerState = &rasterizerStateDesc;
			pipelineSettings.mVRFoveatedRendering = true;

			addPipeline(pRenderer, &desc, &pGbufferPipeline);
		}

		//RenderQuad
		//Position
		VertexLayout vertexLayoutScreenQuad = {};
		vertexLayoutScreenQuad.mBindingCount = 1;
		vertexLayoutScreenQuad.mAttribCount = 2;

		vertexLayoutScreenQuad.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayoutScreenQuad.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
		vertexLayoutScreenQuad.mAttribs[0].mBinding = 0;
		vertexLayoutScreenQuad.mAttribs[0].mLocation = 0;
		vertexLayoutScreenQuad.mAttribs[0].mOffset = 0;

		//Uv
		vertexLayoutScreenQuad.mAttribs[1].mSemantic = SEMANTIC_TEXCOORD0;
		vertexLayoutScreenQuad.mAttribs[1].mFormat = TinyImageFormat_R32G32_SFLOAT;
		vertexLayoutScreenQuad.mAttribs[1].mLocation = 1;
		vertexLayoutScreenQuad.mAttribs[1].mBinding = 0;
		vertexLayoutScreenQuad.mAttribs[1].mOffset = 3 * sizeof(float);    // first attribute contains 3 floats
		
		{
			PipelineDesc renderQuadDesc = {};
			renderQuadDesc.mType = PIPELINE_TYPE_GRAPHICS;
			GraphicsPipelineDesc& pipelineSettings = renderQuadDesc.mGraphicsDesc;

			pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
			pipelineSettings.mRenderTargetCount = 1;
			pipelineSettings.pDepthState = NULL;
			pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
			pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
			pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
			pipelineSettings.mDepthStencilFormat = TinyImageFormat_UNDEFINED;
			pipelineSettings.pRootSignature = pRenderQuadRootSignature;
			pipelineSettings.pShaderProgram = pRenderQuadShader;
			pipelineSettings.pVertexLayout = &vertexLayoutScreenQuad;
			pipelineSettings.pRasterizerState = &rasterizerNonStateDesc;
			addPipeline(pRenderer, &renderQuadDesc, &pRenderQuadPipeline);

			
			pipelineSettings.pRootSignature = pDeferredRootSignature;
			pipelineSettings.pShaderProgram = pDeferredShader;
			addPipeline(pRenderer, &renderQuadDesc, &pDeferredPipeline);
		}

		// Light Culling (compute shader)
		{
			PipelineDesc lightCullingDesc = {};
			lightCullingDesc.mType = PIPELINE_TYPE_COMPUTE;
			ComputePipelineDesc& cpipelineSettings = lightCullingDesc.mComputeDesc;
			cpipelineSettings.pShaderProgram = pTiledCullShader;
			cpipelineSettings.pRootSignature = pTiledCullRootSignature;
			addPipeline(pRenderer, &lightCullingDesc, &pTiledCullPipeline);

			cpipelineSettings.pShaderProgram = pTiledCullHalfZShader;
			cpipelineSettings.pRootSignature = pTiledCullRootSignature;
			addPipeline(pRenderer, &lightCullingDesc, &pTiledCullHalfZPipeline);

			cpipelineSettings.pShaderProgram = pTiledCullModifiedZShader;
			cpipelineSettings.pRootSignature = pTiledCullRootSignature;
			addPipeline(pRenderer, &lightCullingDesc, &pTiledCullModifiedZPipeline);
		}
	}

	void removePipelines()
	{
		removePipeline(pRenderer, pGbufferPipeline);
		removePipeline(pRenderer, pRenderQuadPipeline);

		removePipeline(pRenderer, pTiledCullPipeline);
		removePipeline(pRenderer, pTiledCullHalfZPipeline);
		removePipeline(pRenderer, pTiledCullModifiedZPipeline);

		removePipeline(pRenderer, pDeferredPipeline);
	}

	void prepareDescriptorSets()
	{
		// Gbuffer
		{
			DescriptorData param = {};
			param.pName = "textureMaps";
			param.ppTextures = pMaterialTextures;
			param.mCount = TOTAL_IMGS;
			updateDescriptorSet(pRenderer, 0, pDescriptorSetGbuffers[0], 1, &param);

			param = {};
			param.pName = "uniformBlockCamera";
			for (uint32_t i = 0; i < gDataBufferCount; ++i)
			{
				param.ppBuffers = &pCameraBuffer[i];
				updateDescriptorSet(pRenderer, i, pDescriptorSetGbuffers[1], 1, &param);
			}
		}
		
		// Light culling Pass
		{
			DescriptorData params[4] = {};
			params[0].pName = "albedoTexture";
			params[0].ppTextures = &pGbufferRenderTargets[0]->pTexture;
			params[1].pName = "normalTexture";
			params[1].ppTextures = &pGbufferRenderTargets[1]->pTexture;
			//params[2].pName = "roughnessTexture";
			//params[2].ppTextures = &pGbufferRenderTargets[2]->pTexture;
			params[2].pName = "depthTexture";
			params[2].ppTextures = &pDepthBuffer->pTexture;
			params[3].pName = "sceneTexture";
			params[3].ppTextures = &pSceneBuffer->pTexture;

			updateDescriptorSet(pRenderer, 0, pDescriptorSetCullPass[0], 4, params);

			params[0].pName = "uniformBlockExtCamera";
			params[1].pName = "uniformBlockLightCull";
			params[2].pName = "lightPosAndRadius";
			params[3].pName = "lightColorAndIntensity";

			for (uint32_t i = 0; i < gDataBufferCount; ++i)
			{
				params[0].ppBuffers = &pExtCameraBuffer[i];
				params[1].ppBuffers = &pTileCullDataBuffer[i];
				params[2].ppBuffers = &pLightPosAndRadiusBuffer[i];
				params[3].ppBuffers = &pLightColorAndIntensityBuffer[i];

				updateDescriptorSet(pRenderer, i, pDescriptorSetCullPass[1], 4, params);
			}
		}

		//RenderQuad
		{
			DescriptorData param = {};
			param.pName = "sceneTexture";
			param.ppTextures = &pSceneBuffer->pTexture;
			
			updateDescriptorSet(pRenderer, 0, pDescritporSetRenderQuad, 1, &param);
		}

		{
			DescriptorData params[3] = {};
			params[0].pName = "albedoTexture";
			params[0].ppTextures = &pGbufferRenderTargets[0]->pTexture;
			params[1].pName = "normalTexture";
			params[1].ppTextures = &pGbufferRenderTargets[1]->pTexture;
			//params[2].pName = "roughnessTexture";
			//params[2].ppTextures = &pGbufferRenderTargets[2]->pTexture;
			params[2].pName = "depthTexture";
			params[2].ppTextures = &pDepthBuffer->pTexture;

			updateDescriptorSet(pRenderer, 0, pDescriptorSetDeferredLightPass[0], 3, params);

			params[0].pName = "uniformBlockCamera";
			params[1].pName = "lightPosAndRadius";
			params[2].pName = "lightColorAndIntensity";

			for (uint32_t i = 0; i < gDataBufferCount; ++i)
			{
				params[0].ppBuffers = &pCameraBuffer[i];
				params[1].ppBuffers = &pLightPosAndRadiusBuffer[i];
				params[2].ppBuffers = &pLightColorAndIntensityBuffer[i];

				updateDescriptorSet(pRenderer, i, pDescriptorSetDeferredLightPass[1], 3, params);
			}
		}
	}

	/* Sponza Model Texture Index Setting */
	void assignSponzaTextures()
	{
		int AO = 5;
		int NoMetallic = 6;

		//00 : leaf
		gSponzaTextureIndexForMaterial[0][0] = 66;
		gSponzaTextureIndexForMaterial[0][1] = 67;
		gSponzaTextureIndexForMaterial[0][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[0][3] = 68;
		gSponzaTextureIndexForMaterial[0][4] = AO;

		//01 : vase_round
		gSponzaTextureIndexForMaterial[1][0] = 78;
		gSponzaTextureIndexForMaterial[1][1] = 79;
		gSponzaTextureIndexForMaterial[1][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[1][3] = 80;
		gSponzaTextureIndexForMaterial[1][4] = AO;

		// 02 : 16___Default (gi_flag)
		gSponzaTextureIndexForMaterial[2][0] = 8;
		gSponzaTextureIndexForMaterial[2][1] = 8;    // !!!!!!
		gSponzaTextureIndexForMaterial[2][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[2][3] = 8;    // !!!!!
		gSponzaTextureIndexForMaterial[2][4] = AO;

		//03 : Material__57 (Plant)
		gSponzaTextureIndexForMaterial[3][0] = 75;
		gSponzaTextureIndexForMaterial[3][1] = 76;
		gSponzaTextureIndexForMaterial[3][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[3][3] = 77;
		gSponzaTextureIndexForMaterial[3][4] = AO;

		// 04 : Material__298
		gSponzaTextureIndexForMaterial[4][0] = 9;
		gSponzaTextureIndexForMaterial[4][1] = 10;
		gSponzaTextureIndexForMaterial[4][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[4][3] = 11;
		gSponzaTextureIndexForMaterial[4][4] = AO;

		// 05 : bricks
		gSponzaTextureIndexForMaterial[5][0] = 22;
		gSponzaTextureIndexForMaterial[5][1] = 23;
		gSponzaTextureIndexForMaterial[5][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[5][3] = 24;
		gSponzaTextureIndexForMaterial[5][4] = AO;

		// 06 :  arch
		gSponzaTextureIndexForMaterial[6][0] = 19;
		gSponzaTextureIndexForMaterial[6][1] = 20;
		gSponzaTextureIndexForMaterial[6][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[6][3] = 21;
		gSponzaTextureIndexForMaterial[6][4] = AO;

		// 07 : ceiling
		gSponzaTextureIndexForMaterial[7][0] = 25;
		gSponzaTextureIndexForMaterial[7][1] = 26;
		gSponzaTextureIndexForMaterial[7][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[7][3] = 27;
		gSponzaTextureIndexForMaterial[7][4] = AO;

		// 08 : column_a
		gSponzaTextureIndexForMaterial[8][0] = 28;
		gSponzaTextureIndexForMaterial[8][1] = 29;
		gSponzaTextureIndexForMaterial[8][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[8][3] = 30;
		gSponzaTextureIndexForMaterial[8][4] = AO;

		// 09 : Floor
		gSponzaTextureIndexForMaterial[9][0] = 60;
		gSponzaTextureIndexForMaterial[9][1] = 61;
		gSponzaTextureIndexForMaterial[9][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[9][3] = 6;
		gSponzaTextureIndexForMaterial[9][4] = AO;

		// 10 : column_c
		gSponzaTextureIndexForMaterial[10][0] = 34;
		gSponzaTextureIndexForMaterial[10][1] = 35;
		gSponzaTextureIndexForMaterial[10][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[10][3] = 36;
		gSponzaTextureIndexForMaterial[10][4] = AO;

		// 11 : details
		gSponzaTextureIndexForMaterial[11][0] = 45;
		gSponzaTextureIndexForMaterial[11][1] = 47;
		gSponzaTextureIndexForMaterial[11][2] = 46;
		gSponzaTextureIndexForMaterial[11][3] = 48;
		gSponzaTextureIndexForMaterial[11][4] = AO;

		// 12 : column_b
		gSponzaTextureIndexForMaterial[12][0] = 31;
		gSponzaTextureIndexForMaterial[12][1] = 32;
		gSponzaTextureIndexForMaterial[12][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[12][3] = 33;
		gSponzaTextureIndexForMaterial[12][4] = AO;

		// 13 : flagpole
		gSponzaTextureIndexForMaterial[13][0] = 57;
		gSponzaTextureIndexForMaterial[13][1] = 58;
		gSponzaTextureIndexForMaterial[13][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[13][3] = 59;
		gSponzaTextureIndexForMaterial[13][4] = AO;

		// 14 : fabric_e (green)
		gSponzaTextureIndexForMaterial[14][0] = 51;
		gSponzaTextureIndexForMaterial[14][1] = 52;
		gSponzaTextureIndexForMaterial[14][2] = 53;
		gSponzaTextureIndexForMaterial[14][3] = 54;
		gSponzaTextureIndexForMaterial[14][4] = AO;

		// 15 : fabric_d (blue)
		gSponzaTextureIndexForMaterial[15][0] = 49;
		gSponzaTextureIndexForMaterial[15][1] = 50;
		gSponzaTextureIndexForMaterial[15][2] = 53;
		gSponzaTextureIndexForMaterial[15][3] = 54;
		gSponzaTextureIndexForMaterial[15][4] = AO;

		// 16 : fabric_a (red)
		gSponzaTextureIndexForMaterial[16][0] = 55;
		gSponzaTextureIndexForMaterial[16][1] = 56;
		gSponzaTextureIndexForMaterial[16][2] = 53;
		gSponzaTextureIndexForMaterial[16][3] = 54;
		gSponzaTextureIndexForMaterial[16][4] = AO;

		// 17 : fabric_g (curtain_blue)
		gSponzaTextureIndexForMaterial[17][0] = 37;
		gSponzaTextureIndexForMaterial[17][1] = 38;
		gSponzaTextureIndexForMaterial[17][2] = 43;
		gSponzaTextureIndexForMaterial[17][3] = 44;
		gSponzaTextureIndexForMaterial[17][4] = AO;

		// 18 : fabric_c (curtain_red)
		gSponzaTextureIndexForMaterial[18][0] = 41;
		gSponzaTextureIndexForMaterial[18][1] = 42;
		gSponzaTextureIndexForMaterial[18][2] = 43;
		gSponzaTextureIndexForMaterial[18][3] = 44;
		gSponzaTextureIndexForMaterial[18][4] = AO;

		// 19 : fabric_f (curtain_green)
		gSponzaTextureIndexForMaterial[19][0] = 39;
		gSponzaTextureIndexForMaterial[19][1] = 40;
		gSponzaTextureIndexForMaterial[19][2] = 43;
		gSponzaTextureIndexForMaterial[19][3] = 44;
		gSponzaTextureIndexForMaterial[19][4] = AO;

		// 20 : chain
		gSponzaTextureIndexForMaterial[20][0] = 12;
		gSponzaTextureIndexForMaterial[20][1] = 14;
		gSponzaTextureIndexForMaterial[20][2] = 13;
		gSponzaTextureIndexForMaterial[20][3] = 15;
		gSponzaTextureIndexForMaterial[20][4] = AO;

		// 21 : vase_hanging
		gSponzaTextureIndexForMaterial[21][0] = 72;
		gSponzaTextureIndexForMaterial[21][1] = 73;
		gSponzaTextureIndexForMaterial[21][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[21][3] = 74;
		gSponzaTextureIndexForMaterial[21][4] = AO;

		// 22 : vase
		gSponzaTextureIndexForMaterial[22][0] = 69;
		gSponzaTextureIndexForMaterial[22][1] = 70;
		gSponzaTextureIndexForMaterial[22][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[22][3] = 71;
		gSponzaTextureIndexForMaterial[22][4] = AO;

		// 23 : Material__25 (lion)
		gSponzaTextureIndexForMaterial[23][0] = 16;
		gSponzaTextureIndexForMaterial[23][1] = 17;
		gSponzaTextureIndexForMaterial[23][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[23][3] = 18;
		gSponzaTextureIndexForMaterial[23][4] = AO;

		// 24 : roof
		gSponzaTextureIndexForMaterial[24][0] = 63;
		gSponzaTextureIndexForMaterial[24][1] = 64;
		gSponzaTextureIndexForMaterial[24][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[24][3] = 65;
		gSponzaTextureIndexForMaterial[24][4] = AO;

		// 25 : Material__47 - it seems missing
		gSponzaTextureIndexForMaterial[25][0] = 19;
		gSponzaTextureIndexForMaterial[25][1] = 20;
		gSponzaTextureIndexForMaterial[25][2] = NoMetallic;
		gSponzaTextureIndexForMaterial[25][3] = 21;
		gSponzaTextureIndexForMaterial[25][4] = AO;
	}
};

DEFINE_APPLICATION_MAIN(TiledDeferredRendering)
