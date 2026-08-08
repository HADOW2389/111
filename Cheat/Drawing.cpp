#include "Drawing.h"

LPCSTR Drawing::lpWindowName = "D3D11 Overlay ImGui";
ImVec2 Drawing::vWindowSize = { 350, 75 };
ImGuiWindowFlags Drawing::WindowFlags = 0;
bool Drawing::bDraw = false;
UI::WindowItem Drawing::lpSelectedWindow = { nullptr, "", "" };

bool Drawing::isActive()
{
	return bDraw == true;
}

void Drawing::Draw()
{
	uintptr_t UWorld = xe_decrypt(RPM<uintptr_t>(  offset::Base + offset::UWorld));

	if (!UWorld)
		return;

	uintptr_t GNames = xe_decrypt(RPM<uint64_t>(  offset::Base + offset::GNames));

	if (!GNames)
		return;

	GNames = xe_decrypt(RPM<uint64_t>(  GNames + offset::GNamesPtr));
	//printf("%p\n", UWorld);

	if (!GNames)
		return;

	uintptr_t ULevel = xe_decrypt(RPM<uintptr_t>(  UWorld + offset::CurrentLevel));

	if (!ULevel)
		return;
	//printf("%p\n", ULevel);
	uintptr_t Actor = xe_decrypt(RPM<uintptr_t>(  ULevel + offset::Actors));

	if (!Actor)
		return;

	int ActorCount = RPM<int>(Actor + 8);

	if (!ActorCount)
		return;

	//printf("%p\n", Actor);
	//printf("%d\n", ActorCount);
	Actor = (RPM<uintptr_t>(Actor));

	if (!Actor)
		return;

	//printf("%p\n", Actor);
	uintptr_t GameInstance = xe_decrypt(RPM<uint64_t>(  UWorld + offset::GameInstance));
	if (!GameInstance)
		return;

	uintptr_t LocalPlayers = xe_decrypt(RPM<uint64_t>(  RPM<uint64_t>(  GameInstance + offset::LocalPlayer)));
	if (!LocalPlayers)
		return;
	uintptr_t PlayerController = xe_decrypt(RPM<uint64_t>(  LocalPlayers + offset::PlayerController));
	if (!PlayerController)
		return;

/*	uintptr_t pawn = RPM <uint64_t>(PlayerController + offset::AcknowledgedPawn);
	if (IsBadReadPtr((void*)(pawn)))
		return;

	uintptr_t AcknowledgedPawn = xe_decrypt(pawn);

	if (IsBadReadPtr((void*)(AcknowledgedPawn)))
		return;
	*/
	uintptr_t PlayerCameraManager = RPM<uint64_t>( PlayerController + offset::PlayerCameraManager);

	if (!PlayerCameraManager)
		return;

	
	float CameraEntryPOVFOV = RPM<float>(  PlayerCameraManager + offset::CameraCacheFOV);
	UEVector CameraEntryPOVLocation = RPM<UEVector>(PlayerCameraManager + offset::CameraCacheLocation);
	UERotator CameraEntryPOVRotation = RPM<UERotator>(PlayerCameraManager + offset::CameraCacheRotation);

//	int MyteamId = RPM<int>(AcknowledgedPawn + offset::LastTeamNum);

/*	if (!MyteamId)
		return;*/

	

	for (size_t i = 0; i < ActorCount; i++)
	{
		uintptr_t actor = RPM<uintptr_t>(  Actor + (i * sizeof(uint64_t)));

		if (!actor)
			continue;

		int teamid = RPM<int>(actor + offset::LastTeamNum);

	/*	if (teamid == MyteamId)
			continue;*/

		uint32_t id = DecryptCIndex(RPM<uint32_t>(actor + offset::ObjID));

		if (!id)
			continue;
		
		std::string classname = GetNames(GNames, id);

		uintptr_t root = xe_decrypt(RPM<uintptr_t>(actor + offset::RootComponent));

		if (!root)
			continue;

		UEVector Pos = RPM<UEVector>(root + offset::ComponentLocation);
		if (Pos.X == 0 && Pos.Y == 0 && Pos.Z == 0)
			continue;

		Vector2D Pos2D = worldToScreen({ Pos.X, Pos.Y, Pos.Z },
			{ CameraEntryPOVLocation.X, CameraEntryPOVLocation.Y, CameraEntryPOVLocation.Z },
			{ CameraEntryPOVRotation.Pitch, CameraEntryPOVRotation.Yaw, CameraEntryPOVRotation.Roll },
			CameraEntryPOVFOV);

		if (Pos2D.IsZero())
			continue;

		if (classname == "DroppedItem")
			ImGui::GetBackgroundDrawList()->AddRect({ Pos2D.x - 2,Pos2D.y - 2 }, 
				{ Pos2D.x - 2 + 4, Pos2D.y - 2 + 4 }, ImColor(0, 255, 255, 255));

		int Marks = IsPlayer(classname);

		if (!Marks)
			continue;

		float Health = GetHealth(actor);

		if (Health < 1)
			continue;

		uintptr_t Mesh = RPM<uintptr_t>(  actor + offset::Mesh);

		if (Mesh < 65535)
			continue;

		uintptr_t StaticMesh = RPM<uintptr_t>(Mesh + offset::StaticMesh);

		if (!StaticMesh)
			continue;

		FTransform ComponentToWorld = RPM<FTransform>(Mesh + offset::ComponentToWorld);

		bool BoneValidity = true;
		constexpr std::array<int, 15> kBones{
			BoneIndex::Head, BoneIndex::Neck,  BoneIndex::Pelvis,
			BoneIndex::Lshoulder, BoneIndex::Lelbow, BoneIndex::Lhand,
			BoneIndex::Rshoulder, BoneIndex::Relbow, BoneIndex::Rhand,
			BoneIndex::Lbuttock,  BoneIndex::Lknee,  BoneIndex::Lfoot,
			BoneIndex::Rbuttock,  BoneIndex::Rknee,  BoneIndex::Rfoot
		};
		Vector2D posHead, posNeck, posPelvis,
			posLShoulder, posLElbow, posLHand,
			posRShoulder, posRElbow, posRHand,
			posLButtock, posLKnee, posLFoot,
			posRButtock, posRKnee, posRFoot;

		for (int BoneID : kBones)
		{
			Vector3 BonePos = GetBonePos(ComponentToWorld, StaticMesh, BoneID);
			if (BonePos.Length() <= 0) { BoneValidity = false; break; }

			Vector2D posScreen = worldToScreen({ BonePos.x, BonePos.y, BonePos.z },
				{ CameraEntryPOVLocation.X, CameraEntryPOVLocation.Y, CameraEntryPOVLocation.Z },
				{ CameraEntryPOVRotation.Pitch, CameraEntryPOVRotation.Yaw, CameraEntryPOVRotation.Roll },
				CameraEntryPOVFOV);
			if (posScreen.IsZero()) { BoneValidity = false; break; }

			switch (BoneID)
			{
			case BoneIndex::Head:      posHead = posScreen; break;
			case BoneIndex::Neck:      posNeck = posScreen; break;
			case BoneIndex::Pelvis:    posPelvis = posScreen; break;

			case BoneIndex::Lshoulder: posLShoulder = posScreen; break;
			case BoneIndex::Lelbow:    posLElbow = posScreen; break;
			case BoneIndex::Lhand:     posLHand = posScreen; break;

			case BoneIndex::Rshoulder: posRShoulder = posScreen; break;
			case BoneIndex::Relbow:    posRElbow = posScreen; break;
			case BoneIndex::Rhand:     posRHand = posScreen; break;

			case BoneIndex::Lbuttock:  posLButtock = posScreen; break;
			case BoneIndex::Lknee:     posLKnee = posScreen; break;
			case BoneIndex::Lfoot:     posLFoot = posScreen; break;

			case BoneIndex::Rbuttock:  posRButtock = posScreen; break;
			case BoneIndex::Rknee:     posRKnee = posScreen; break;
			case BoneIndex::Rfoot:     posRFoot = posScreen; break;
			}
		}

		if (!BoneValidity)
			continue;

		Vector3 HeadPos = GetBonePos(ComponentToWorld, StaticMesh, BoneIndex::Head);
		if (HeadPos.Length() <= 0)
			continue;

		

		//ESP
/*		Vector3 RFootPos = GetBonePos(ComponentToWorld, StaticMesh, BoneIndex::Rfoot);
		if (RFootPos.Length() <= 0)
			continue;

		Vector2D posHeadRect = worldToScreen({ HeadPos.x, HeadPos.y, HeadPos.z + 25 },
			{ CameraEntryPOVLocation.X, CameraEntryPOVLocation.Y, CameraEntryPOVLocation.Z },
			{ CameraEntryPOVRotation.Pitch, CameraEntryPOVRotation.Yaw, CameraEntryPOVRotation.Roll },
			CameraEntryPOVFOV);
		if (posHeadRect.IsZero())
			continue;

		Vector2D posFeetRect = worldToScreen({ RFootPos.x, RFootPos.y, RFootPos.z - 15 },
			{ CameraEntryPOVLocation.X, CameraEntryPOVLocation.Y, CameraEntryPOVLocation.Z },
			{ CameraEntryPOVRotation.Pitch, CameraEntryPOVRotation.Yaw, CameraEntryPOVRotation.Roll },
			CameraEntryPOVFOV);

		if (posFeetRect.IsZero())
			continue;

		float h = posFeetRect.y - posHeadRect.y;
		float w = h * 0.5f;
		float x = posHeadRect.x - w / 2.0f;
		float y = posHeadRect.y;*/

		
		if (Marks == 1)
		{
			//ImGui::GetBackgroundDrawList()->AddRect({ x,y }, { x + w, y + h }, ImColor(255, 0, 0, 255));

		}
		else
		{
			//ImGui::GetBackgroundDrawList()->AddRect({ x,y }, { x + w, y + h }, ImColor(0, 255, 0, 255));
		}
		//HealthBar
		//float scaleFactor = (float)Health / 100.f;
		//ImGui::GetBackgroundDrawList()->AddRectFilled({ x - 4, y + h * (1 - scaleFactor) + 1 }, { x - 1, posFeetRect.y - 1 }, ImColor(0, 255, 0));
		//ImGui::GetBackgroundDrawList()->AddRect({ x - 4, y }, { x - 4 + 4,y + h }, ImColor(0, 0, 0), 1);

		FilledLine(posHead.x, posHead.y, posNeck.x, posNeck.y, 1, ImColor(255, 255, 255));
		FilledLine(posNeck.x, posNeck.y, posPelvis.x, posPelvis.y, 1, ImColor(255, 255, 255));
		FilledLine(posLShoulder.x, posLShoulder.y, posNeck.x, posNeck.y, 1, ImColor(255, 255, 255));
		FilledLine(posLElbow.x, posLElbow.y, posLShoulder.x, posLShoulder.y, 1, ImColor(255, 255, 255));
		FilledLine(posLHand.x, posLHand.y, posLElbow.x, posLElbow.y, 1, ImColor(255, 255, 255));
		FilledLine(posRShoulder.x, posRShoulder.y, posNeck.x, posNeck.y, 1, ImColor(255, 255, 255));
		FilledLine(posRElbow.x, posRElbow.y, posRShoulder.x, posRShoulder.y, 1, ImColor(255, 255, 255));
		FilledLine(posRHand.x, posRHand.y, posRElbow.x, posRElbow.y, 1, ImColor(255, 255, 255));

		FilledLine(posLButtock.x, posLButtock.y, posPelvis.x, posPelvis.y, 1, ImColor(255, 255, 255));
		FilledLine(posLKnee.x, posLKnee.y, posLButtock.x, posLButtock.y, 1, ImColor(255, 255, 255));
		FilledLine(posLFoot.x, posLFoot.y, posLKnee.x, posLKnee.y, 1, ImColor(255, 255, 255));

		FilledLine(posRButtock.x, posRButtock.y, posPelvis.x, posPelvis.y, 1, ImColor(255, 255, 255));
		FilledLine(posRKnee.x, posRKnee.y, posRButtock.x, posRButtock.y, 1, ImColor(255, 255, 255));
		FilledLine(posRFoot.x, posRFoot.y, posRKnee.x, posRKnee.y, 1, ImColor(255, 255, 255));
	}
}