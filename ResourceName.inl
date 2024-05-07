#ifndef RESOURCENAME_H
#define RESOURCENAME_H

enum
{
	SPONZA_MODEL,
	LION_MODEL,
	MODEL_COUNT
};

const char* gModelNames[MODEL_COUNT] = { "Sponza.bin","lion.bin"};

const char* pMaterialImageFileNames[] = {
	"SponzaPBR_Textures/ao.tex",
	"SponzaPBR_Textures/ao.tex",
	"SponzaPBR_Textures/ao.tex",
	"SponzaPBR_Textures/ao.tex",
	"SponzaPBR_Textures/ao.tex",

	//common
	"SponzaPBR_Textures/ao.tex",
	"SponzaPBR_Textures/Dielectric_metallic.tex",
	"SponzaPBR_Textures/Metallic_metallic.tex",
	"SponzaPBR_Textures/gi_flag.tex",

	//Background
	"SponzaPBR_Textures/Background/Background_Albedo.tex",
	"SponzaPBR_Textures/Background/Background_Normal.tex",
	"SponzaPBR_Textures/Background/Background_Roughness.tex",

	//ChainTexture
	"SponzaPBR_Textures/ChainTexture/ChainTexture_Albedo.tex",
	"SponzaPBR_Textures/ChainTexture/ChainTexture_Metallic.tex",
	"SponzaPBR_Textures/ChainTexture/ChainTexture_Normal.tex",
	"SponzaPBR_Textures/ChainTexture/ChainTexture_Roughness.tex",

	//Lion
	"SponzaPBR_Textures/Lion/Lion_Albedo.tex",
	"SponzaPBR_Textures/Lion/Lion_Normal.tex",
	"SponzaPBR_Textures/Lion/Lion_Roughness.tex",

	//Sponza_Arch
	"SponzaPBR_Textures/Sponza_Arch/Sponza_Arch_diffuse.tex",
	"SponzaPBR_Textures/Sponza_Arch/Sponza_Arch_normal.tex",
	"SponzaPBR_Textures/Sponza_Arch/Sponza_Arch_roughness.tex",

	//Sponza_Bricks
	"SponzaPBR_Textures/Sponza_Bricks/Sponza_Bricks_a_Albedo.tex",
	"SponzaPBR_Textures/Sponza_Bricks/Sponza_Bricks_a_Normal.tex",
	"SponzaPBR_Textures/Sponza_Bricks/Sponza_Bricks_a_Roughness.tex",

	//Sponza_Ceiling
	"SponzaPBR_Textures/Sponza_Ceiling/Sponza_Ceiling_diffuse.tex",
	"SponzaPBR_Textures/Sponza_Ceiling/Sponza_Ceiling_normal.tex",
	"SponzaPBR_Textures/Sponza_Ceiling/Sponza_Ceiling_roughness.tex",

	//Sponza_Column
	"SponzaPBR_Textures/Sponza_Column/Sponza_Column_a_diffuse.tex",
	"SponzaPBR_Textures/Sponza_Column/Sponza_Column_a_normal.tex",
	"SponzaPBR_Textures/Sponza_Column/Sponza_Column_a_roughness.tex",

	"SponzaPBR_Textures/Sponza_Column/Sponza_Column_b_diffuse.tex",
	"SponzaPBR_Textures/Sponza_Column/Sponza_Column_b_normal.tex",
	"SponzaPBR_Textures/Sponza_Column/Sponza_Column_b_roughness.tex",

	"SponzaPBR_Textures/Sponza_Column/Sponza_Column_c_diffuse.tex",
	"SponzaPBR_Textures/Sponza_Column/Sponza_Column_c_normal.tex",
	"SponzaPBR_Textures/Sponza_Column/Sponza_Column_c_roughness.tex",

	//Sponza_Curtain
	"SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_Blue_diffuse.tex",
	"SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_Blue_normal.tex",

	"SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_Green_diffuse.tex",
	"SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_Green_normal.tex",

	"SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_Red_diffuse.tex",
	"SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_Red_normal.tex",

	"SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_metallic.tex",
	"SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_roughness.tex",

	//Sponza_Details
	"SponzaPBR_Textures/Sponza_Details/Sponza_Details_diffuse.tex",
	"SponzaPBR_Textures/Sponza_Details/Sponza_Details_metallic.tex",
	"SponzaPBR_Textures/Sponza_Details/Sponza_Details_normal.tex",
	"SponzaPBR_Textures/Sponza_Details/Sponza_Details_roughness.tex",

	//Sponza_Fabric
	"SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_Blue_diffuse.tex",
	"SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_Blue_normal.tex",

	"SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_Green_diffuse.tex",
	"SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_Green_normal.tex",

	"SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_metallic.tex",
	"SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_roughness.tex",

	"SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_Red_diffuse.tex",
	"SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_Red_normal.tex",

	//Sponza_FlagPole
	"SponzaPBR_Textures/Sponza_FlagPole/Sponza_FlagPole_diffuse.tex",
	"SponzaPBR_Textures/Sponza_FlagPole/Sponza_FlagPole_normal.tex",
	"SponzaPBR_Textures/Sponza_FlagPole/Sponza_FlagPole_roughness.tex",

	//Sponza_Floor
	"SponzaPBR_Textures/Sponza_Floor/Sponza_Floor_diffuse.tex",
	"SponzaPBR_Textures/Sponza_Floor/Sponza_Floor_normal.tex",
	"SponzaPBR_Textures/Sponza_Floor/Sponza_Floor_roughness.tex",

	//Sponza_Roof
	"SponzaPBR_Textures/Sponza_Roof/Sponza_Roof_diffuse.tex",
	"SponzaPBR_Textures/Sponza_Roof/Sponza_Roof_normal.tex",
	"SponzaPBR_Textures/Sponza_Roof/Sponza_Roof_roughness.tex",

	//Sponza_Thorn
	"SponzaPBR_Textures/Sponza_Thorn/Sponza_Thorn_diffuse.tex",
	"SponzaPBR_Textures/Sponza_Thorn/Sponza_Thorn_normal.tex",
	"SponzaPBR_Textures/Sponza_Thorn/Sponza_Thorn_roughness.tex",

	//Vase
	"SponzaPBR_Textures/Vase/Vase_diffuse.tex",
	"SponzaPBR_Textures/Vase/Vase_normal.tex",
	"SponzaPBR_Textures/Vase/Vase_roughness.tex",

	//VaseHanging
	"SponzaPBR_Textures/VaseHanging/VaseHanging_diffuse.tex",
	"SponzaPBR_Textures/VaseHanging/VaseHanging_normal.tex",
	"SponzaPBR_Textures/VaseHanging/VaseHanging_roughness.tex",

	//VasePlant
	"SponzaPBR_Textures/VasePlant/VasePlant_diffuse.tex",
	"SponzaPBR_Textures/VasePlant/VasePlant_normal.tex",
	"SponzaPBR_Textures/VasePlant/VasePlant_roughness.tex",

	//VaseRound
	"SponzaPBR_Textures/VaseRound/VaseRound_diffuse.tex",
	"SponzaPBR_Textures/VaseRound/VaseRound_normal.tex",
	"SponzaPBR_Textures/VaseRound/VaseRound_roughness.tex",

	"lion/lion_albedo.tex",
	"lion/lion_specular.tex",
	"lion/lion_normal.tex",

}; // total 84

const char* pSkyBoxImageFileNames[] = { "Skybox_right1",  "Skybox_left2",  "Skybox_top3",
										"Skybox_bottom4", "Skybox_front5", "Skybox_back6" };

#endif // !RESOURCENAME_H
