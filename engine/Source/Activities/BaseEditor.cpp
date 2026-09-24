#include "BaseEditor.h"

#include "CameraMan.h"
#include "PresetMan.h"
#include "MovableMan.h"
#include "FrameMan.h"
#include "UInputMan.h"
#include "SceneMan.h"
#include "MetaMan.h"
// #include "AHuman.h"
// #include "MOPixel.h"
#include "SLTerrain.h"
#include "Controller.h"
// #include "AtomGroup.h"
#include "Actor.h"
#include "AHuman.h"
#include "ACrab.h"
#include "ACRocket.h"
#include "HeldDevice.h"
#include "Scene.h"
#include "DataModule.h"

#include "SceneEditorGUI.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace RTE;

ConcreteClassInfo(BaseEditor, Activity, 0);

BaseEditor::BaseEditor() {
	Clear();
}

BaseEditor::~BaseEditor() {
	Destroy(true);
}

void BaseEditor::Clear() {
	m_pEditorGUI = 0;
	m_NeedSave = false;
	m_EditingPlayer = Players::PlayerOne;
	m_RevealedAround.clear();
}

int BaseEditor::Create() {
	if (Activity::Create() < 0)
		return -1;

	return 0;
}

int BaseEditor::Create(const BaseEditor& reference) {
	if (Activity::Create(reference) < 0)
		return -1;

	if (m_Description.empty())
		m_Description = "Build or Edit a base on this Scene.";

	return 0;
}

int BaseEditor::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return Activity::ReadProperty(propName, reader));
	/*
	    MatchProperty("CPUTeam", { reader >> m_CPUTeam; });
	    MatchProperty("Difficulty", { reader >> m_Difficulty; });
	    MatchProperty("DeliveryDelay", { reader >> m_DeliveryDelay; });
	*/
	EndPropertyList;
}

int BaseEditor::Save(Writer& writer) const {
	Activity::Save(writer);
	return 0;
}

void BaseEditor::Destroy(bool notInherited) {
	delete m_pEditorGUI;

	if (!notInherited)
		Activity::Destroy();
	Clear();
}

int BaseEditor::Start() {
	// Set the split screen config before the Scene (and it SceneLayers, specifially) are loaded
	g_FrameMan.ResetSplitScreens(false, false);
	// Too diff
	//    int error = Activity::Start();
	int error = 0;

	m_ActivityState = ActivityState::Running;
	m_Paused = false;

	// Reset the mousemoving so that it won't trap the mouse if the window isn't in focus (common after loading)
	g_UInputMan.DisableMouseMoving(true);
	g_UInputMan.DisableMouseMoving(false);

	// Enable keys again
	g_UInputMan.DisableKeys(false);

	// Load the scene now
	error = g_SceneMan.LoadScene();
	if (error < 0)
		return error;

	// Open all doors so we can pathfind past them for brain placement
	g_MovableMan.OpenAllDoors(true, Teams::NoTeam);

	///////////////////////////////////////
	// Set up player - ONLY ONE ever in a base building activity

	// Figure which player is editing this base.. the first active one we find
	int editingPlayer = Players::PlayerOne;
	for (int player = Players::PlayerOne; player < Players::MaxPlayerCount; ++player)
		if (m_IsActive[player])
			editingPlayer = player;
	// TODO: support multiple coop players editing the same base?? - A: NO, silly
	m_EditingPlayer = editingPlayer;
	m_RevealedAround.clear();

	//    for (int player = Players::PlayerOne; player < Players::MaxPlayerCount; ++player)
	//    {
	//        if (!m_IsActive[player])
	//            continue;
	m_ViewState[editingPlayer] = ViewState::Normal;
	g_FrameMan.ClearScreenText(ScreenOfPlayer(editingPlayer));
	// Set the team associations with the first screen so that the correct unseen are shows up
	g_CameraMan.SetScreenTeam(m_Team[editingPlayer], ScreenOfPlayer(editingPlayer));
	g_CameraMan.SetScreenOcclusion(Vector(), ScreenOfPlayer(editingPlayer));

	m_PlayerController[editingPlayer].Reset();
	m_PlayerController[editingPlayer].Create(Controller::CIM_PLAYER, editingPlayer);
	m_PlayerController[editingPlayer].SetTeam(m_Team[editingPlayer]);

	m_MessageTimer[editingPlayer].Reset();
	//    }

	// Kill off any actors not of this player's team.. they're not supposed to be here
	g_MovableMan.KillAllEnemyActors(GetTeamOfPlayer(editingPlayer));

	//////////////////////////////////////////////
	// Allocate and (re)create the Editor GUI

	if (m_pEditorGUI)
		m_pEditorGUI->Destroy();
	else
		m_pEditorGUI = new SceneEditorGUI;
	m_pEditorGUI->Create(&(m_PlayerController[editingPlayer]), SceneEditorGUI::BLUEPRINTEDIT);

	// See if we are playing a metagame and which metaplayer this would be editing in this activity
	if (g_MetaMan.GameInProgress()) {
		MetaPlayer* pMetaPlayer = g_MetaMan.GetMetaPlayerOfInGamePlayer(editingPlayer);
		if (pMetaPlayer) {
			// Set the appropriate modifiers to the prices etc of this editor
			m_pEditorGUI->SetNativeTechModule(pMetaPlayer->GetNativeTechModule());
			m_pEditorGUI->SetForeignCostMultiplier(pMetaPlayer->GetForeignCostMultiplier());
		}
	}

	// A site taken without a battle (MetagameGUI::AutoResolveOffensive) keeps its resident
	// brain at (-1, -1) for the battle to put on the ground. Put it there now, so the player
	// sees the ground around it (RevealAroundOwnUnits) and builds where it stands; the pie
	// menu still moves it. See notes/parity.md.
	if (SceneObject* brain = g_SceneMan.GetScene()->GetResidentBrain(editingPlayer); brain && brain->GetPos() == Vector(-1, -1)) {
		brain->SetPos(GroundSpotForBrain(editingPlayer));
		brain->FullUpdate();
	}

	// Set the view to scroll to the brain of the editing player, if there is any
	if (g_SceneMan.GetScene()->GetResidentBrain(editingPlayer))
		m_pEditorGUI->SetCursorPos(g_SceneMan.GetScene()->GetResidentBrain(editingPlayer)->GetPos());

	// Test if the resident brain is still in a valid spot, after potentially building it into a tomb since last round
	m_pEditorGUI->TestBrainResidence();

	////////////////////////////////
	// Set up teams

	for (int team = Teams::TeamOne; team < Teams::MaxTeamCount; ++team) {
		if (!m_TeamActive[team])
			continue;
		m_FundsChanged[team] = false;
		m_TeamDeaths[team] = 0;
	}

	// Move any brains resident in the Scene to the MovableMan
	// Nope - these are manipulated by the SceneEditorGUI directly where they are in the resident lists
	//    g_SceneMan.GetScene()->PlaceResidentBrains(*this);

	// The get a list of all the placed objects in the Scene and set them to not kick around
	const std::list<SceneObject*>* pSceneObjectList = g_SceneMan.GetScene()->GetPlacedObjects(Scene::BLUEPRINT);
	for (std::list<SceneObject*>::const_iterator itr = pSceneObjectList->begin(); itr != pSceneObjectList->end(); ++itr) {
		Actor* pActor = dynamic_cast<Actor*>(*itr);
		if (pActor) {
			pActor->SetStatus(Actor::INACTIVE);
			pActor->GetController()->SetDisabled(true);
		}
	}

	// Update all blueprints so they look right after load
	g_SceneMan.GetScene()->UpdatePlacedObjects(Scene::BLUEPRINT);

	return error;
}

void BaseEditor::SetPaused(bool pause) {
	// Override the pause
	m_Paused = false;
}

void BaseEditor::End() {
	Activity::End();

	m_ActivityState = ActivityState::Over;
}

void BaseEditor::Update() {
	Activity::Update();

	if (!g_SceneMan.GetScene())
		return;

	// Update the loaded objects of the loaded scene so they look right
	g_SceneMan.GetScene()->UpdatePlacedObjects(Scene::BLUEPRINT);

	/////////////////////////////////////////////////////
	// Update the editor interface

	m_pEditorGUI->Update();

	// Any edits made, dirtying the scene?
	m_NeedSave = m_pEditorGUI->EditMade() || m_NeedSave;

	RevealAroundOwnUnits();

	// Get any mode change commands that the user gave the Editor GUI
	// Done with editing for now; save and return to campaign screen
	if (m_pEditorGUI->GetActivatedPieSlice() == PieSliceType::EditorDone) {
		m_pEditorGUI->SetEditorGUIMode(SceneEditorGUI::INACTIVE);

		if (m_NeedSave)
			SaveScene(g_SceneMan.GetScene()->GetPresetName());

		// Quit to metagame view
		g_ActivityMan.PauseActivity();
	}
}

void BaseEditor::DrawGUI(BITMAP* pTargetBitmap, const Vector& targetPos, int which) {
	m_pEditorGUI->Draw(pTargetBitmap, targetPos);

	Activity::DrawGUI(pTargetBitmap, targetPos, which);
}

void BaseEditor::Draw(BITMAP* pTargetBitmap, const Vector& targetPos) {
	Activity::Draw(pTargetBitmap, targetPos);
}

void BaseEditor::RevealAroundOwnUnits() {
	const int team = GetTeamOfPlayer(m_EditingPlayer);
	if (team < Teams::TeamOne || !g_SceneMan.GetScene()->GetUnseenLayer(team)) {
		return;
	}

	std::vector<const Actor*> units;
	if (const Actor* brain = dynamic_cast<const Actor*>(g_SceneMan.GetScene()->GetResidentBrain(m_EditingPlayer))) {
		units.push_back(brain);
	}
	for (const SceneObject* placed: *g_SceneMan.GetScene()->GetPlacedObjects(Scene::BLUEPRINT)) {
		if (placed->GetTeam() == team && (dynamic_cast<const AHuman*>(placed) || dynamic_cast<const ACrab*>(placed))) {
			units.push_back(static_cast<const Actor*>(placed));
		}
	}

	// Each unit sees all the way round as far as it would in a battle (Actor::CastSeeRays),
	// with its rays close enough that the boxes they reveal leave no gaps at their far ends.
	const Vector resolution = g_SceneMan.GetUnseenResolution(team);
	const float rayGap = std::max(1.0F, (40.0F - resolution.GetLargest()) / 2.0F);
	const int step = static_cast<int>(resolution.GetSmallest()) / 2;
	int sweeps = 0;
	for (const Actor* unit: units) {
		const Vector& pos = unit->GetPos();
		// A resident brain at (-1, -1) has not been placed yet (MetagameGUI::AutoResolveOffensive).
		if (!unit->GetCanRevealUnseen() || pos.m_X < 0 || pos.m_Y < 0 || pos.m_X >= g_SceneMan.GetSceneWidth() || pos.m_Y >= g_SceneMan.GetSceneHeight()) {
			continue;
		}
		const float range = static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) * 0.51F * unit->GetPerceptiveness();
		if (range < 1.0F || !m_RevealedAround.emplace(pos.GetFloorIntX(), pos.GetFloorIntY(), static_cast<int>(range)).second) {
			continue;
		}
		const int rays = static_cast<int>(std::ceil(c_TwoPI * range / rayGap));
		Vector rayEnd;
		for (int ray = 0; ray < rays; ++ray) {
			Vector look(range, 0);
			look.DegRotate(360.0F * static_cast<float>(ray) / static_cast<float>(rays));
			g_SceneMan.CastSeeRay(team, pos, look, rayEnd, 25, step);
		}
		// A few units a frame, so opening a large base does not stall a frame.
		if (++sweeps == 4) {
			break;
		}
	}
}

Vector BaseEditor::GroundSpotForBrain(int player) const {
	const float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	// This player's quarter first, then the whole width, as MetaFight.lua looks; away from
	// the edges of a site that does not wrap.
	for (float rangeWidth: {sceneWidth / static_cast<float>(Players::MaxPlayerCount), sceneWidth}) {
		float rangeStart = rangeWidth < sceneWidth ? rangeWidth * static_cast<float>(player) : 0.0F;
		float rangeEnd = rangeStart + rangeWidth;
		if (!g_SceneMan.SceneWrapsX()) {
			rangeStart += rangeWidth * 0.25F;
			rangeEnd -= rangeWidth * 0.25F;
		}
		const float middle = std::floor((rangeStart + rangeEnd) / 2.0F);
		for (float offset = 0; middle + offset <= rangeEnd; offset += 10.0F) {
			for (float x: {middle + offset, middle - offset}) {
				// MetaFight.lua's test: at least 25 pixels of air above the ground, at five points 10 pixels apart.
				bool roomAbove = true;
				for (int point = -2; point <= 2 && roomAbove; ++point) {
					roomAbove = g_SceneMan.FindAltitude(Vector(x + static_cast<float>(point * 10), 0), 0, 19) >= 25.0F;
				}
				if (roomAbove) {
					return g_SceneMan.MovePointToGround(Vector(x, 0), 20, 3);
				}
			}
		}
	}
	return g_SceneMan.MovePointToGround(Vector(std::floor(sceneWidth / static_cast<float>(Players::MaxPlayerCount) * (static_cast<float>(player) + 0.5F)), 0), 20, 3);
}

bool BaseEditor::SaveScene(std::string saveAsName, bool forceOverwrite) {
	/*
	    // Set the name of the current scene in effect
	    g_SceneMan.GetScene()->SetPresetName(saveAsName);
	    // Try to save to the data module
	    string sceneFilePath(g_PresetMan.GetDataModule(m_ModuleSpaceID)->GetFileName() + "/Scenes/" + saveAsName + ".ini");
	    if (g_PresetMan.AddEntityPreset(g_SceneMan.GetScene(), m_ModuleSpaceID, forceOverwrite, sceneFilePath))
	    {
	        // Does ini already exist? If yes, then no need to add it to a scenes.ini etc
	        bool sceneFileExisted = System::PathExistsCaseSensitive(sceneFilePath.c_str());
	        // Create the writer
	        Writer sceneWriter(sceneFilePath.c_str(), false);
	        sceneWriter.NewProperty("AddScene");
	// TODO: Check if the ini file already exists, and then ask if overwrite
	        // Write the scene out to the new ini
	        sceneWriter << g_SceneMan.GetScene();

	        if (!sceneFileExisted)
	        {
	            // First find/create  a .rte/Scenes.ini file to include the new .ini into
	            string scenesFilePath(g_PresetMan.GetDataModule(m_ModuleSpaceID)->GetFileName() + "/Scenes.ini");
	            bool scenesFileExisted = System::PathExistsCaseSensitive(scenesFilePath.c_str());
	            Writer scenesWriter(scenesFilePath.c_str(), true);
	            scenesWriter.NewProperty("\nIncludeFile");
	            scenesWriter << sceneFilePath;

	            // Also add a line to the end of the modules' Index.ini to include the newly created Scenes.ini next startup
	            // If it's already included, it doens't matter, the definitions will just bounce the second time
	            if (!scenesFileExisted)
	            {
	                string indexFilePath(g_PresetMan.GetDataModule(m_ModuleSpaceID)->GetFileName() + "/Index.ini");
	                Writer indexWriter(indexFilePath.c_str(), true);
	                // Add extra tab since the DataModule has everything indented
	                indexWriter.NewProperty("\tIncludeFile");
	                indexWriter << scenesFilePath;
	            }
	        }
	        return m_HasEverBeenSaved = true;
	    }
	*/
	return false;
}
