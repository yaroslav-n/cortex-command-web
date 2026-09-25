#include "PauseMenuGUI.h"

#include "WindowMan.h"
#include "FrameMan.h"
#include "ConsoleMan.h"
#include "ActivityMan.h"
#ifdef __EMSCRIPTEN__
#include "MetaMan.h"
#endif
#include "UInputMan.h"
#include "SettingsMan.h"

#include "SaveLoadMenuGUI.h"
#include "SettingsGUI.h"
#include "ModManagerGUI.h"

#include "GUI.h"
#include "AllegroScreen.h"
#include "GUIInputWrapper.h"
#include "GUICollectionBox.h"
#include "GUIButton.h"

#include "AllegroTools.h"

using namespace RTE;

void PauseMenuGUI::Clear() {
	m_GUIControlManager = nullptr;
	m_ActiveDialogBox = nullptr;

	m_BackdropBitmap = nullptr;

	m_ActiveMenuScreen = PauseMenuScreen::MainScreen;
	m_UpdateResult = PauseMenuUpdateResult::NoEvent;
	m_ResumeButtonBlinkTimer.Reset();

	m_SaveLoadMenu = nullptr;
	m_SettingsMenu = nullptr;
	m_ModManagerMenu = nullptr;

	m_ButtonHoveredText.fill(std::string());
	m_ButtonUnhoveredText.fill(std::string());
	m_HoveredButton = nullptr;
	m_PrevHoveredButtonIndex = 0;

	m_SavingButtonsDisabled = false;
	m_ModManagerButtonDisabled = false;
#ifdef __EMSCRIPTEN__
	m_BackButtonTargetName = "Main";
	m_ScenarioLostLayout = false;
	m_ButtonsCentreY = 0;
#endif

	m_PauseMenuBox = nullptr;
	m_PauseMenuButtons.fill(nullptr);
}

void PauseMenuGUI::Create(AllegroScreen* guiScreen, GUIInputWrapper* guiInput) {
	m_GUIControlManager = std::make_unique<GUIControlManager>();
	RTEAssert(m_GUIControlManager->Create(guiScreen, guiInput, "Base.rte/GUIs/Skins/Menus", "MainMenuScreenSkin.ini"), "Failed to create GUI Control Manager and load it from Base.rte/GUIs/Skins/Menus/MainMenuScreenSkin.ini");
	m_GUIControlManager->Load("Base.rte/GUIs/PauseMenuGUI.ini");

	int rootBoxMaxWidth = g_WindowMan.FullyCoversAllDisplays() ? g_WindowMan.GetPrimaryWindowDisplayWidth() / g_WindowMan.GetResMultiplier() : g_WindowMan.GetResX();

	dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("root"))->Resize(rootBoxMaxWidth, g_WindowMan.GetResY());

	m_PauseMenuBox = dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("PauseScreen"));
	m_PauseMenuBox->CenterInParent(true, true);

	m_PauseMenuButtons[PauseMenuButton::BackToMainButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonBackToMain"));
	m_PauseMenuButtons[PauseMenuButton::SaveOrLoadGameButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonSaveOrLoadGame"));
	m_PauseMenuButtons[PauseMenuButton::SettingsButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonSettings"));
	m_PauseMenuButtons[PauseMenuButton::ModManagerButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonModManager"));
	m_PauseMenuButtons[PauseMenuButton::ResumeButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonResume"));
#ifdef __EMSCRIPTEN__
	m_PauseMenuButtons[PauseMenuButton::RestartButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonRestart"));
#endif

	for (size_t pauseMenuButton = 0; pauseMenuButton < m_PauseMenuButtons.size(); ++pauseMenuButton) {
		std::string buttonText = m_PauseMenuButtons[pauseMenuButton]->GetText();

		std::transform(buttonText.begin(), buttonText.end(), buttonText.begin(), ::toupper);
		m_ButtonHoveredText[pauseMenuButton] = buttonText;
		std::transform(buttonText.begin(), buttonText.end(), buttonText.begin(), ::tolower);
		m_ButtonUnhoveredText[pauseMenuButton] = buttonText;

		m_PauseMenuButtons[pauseMenuButton]->SetText(m_ButtonUnhoveredText[pauseMenuButton]);
		m_PauseMenuButtons[pauseMenuButton]->CenterInParent(true, false);
	}
#ifdef __EMSCRIPTEN__
	const GUIButton* resumeButton = m_PauseMenuButtons[PauseMenuButton::ResumeButton];
	m_ButtonsCentreY = m_PauseMenuBox->GetRelYPos() + (resumeButton->GetRelYPos() + resumeButton->GetHeight()) / 2;
#endif

	if (m_BackdropBitmap) {
		destroy_bitmap(m_BackdropBitmap);
	}
	const BITMAP* backbuffer = g_FrameMan.GetBackBuffer32();
	m_BackdropBitmap = create_bitmap_ex(FrameMan::c_BPP, backbuffer->w, backbuffer->h);
	unsigned int halfTransBlack = makeacol32(0, 0, 0, 96);
	clear_to_color(m_BackdropBitmap, halfTransBlack);

	m_SaveLoadMenu = std::make_unique<SaveLoadMenuGUI>(guiScreen, guiInput, true);
	m_SettingsMenu = std::make_unique<SettingsGUI>(guiScreen, guiInput, true);
	m_ModManagerMenu = std::make_unique<ModManagerGUI>(guiScreen, guiInput, true);
}

void PauseMenuGUI::SetBackButtonTargetName(const std::string& menuName) {
#ifdef __EMSCRIPTEN__
	m_BackButtonTargetName = menuName;
	SetButtonText(PauseMenuButton::BackToMainButton, "Back to " + menuName + " Menu");
#else
	std::string newButtonText = "Back to " + menuName + " Menu";

	std::transform(newButtonText.begin(), newButtonText.end(), newButtonText.begin(), ::toupper);
	m_ButtonHoveredText[PauseMenuButton::BackToMainButton] = newButtonText;
	std::transform(newButtonText.begin(), newButtonText.end(), newButtonText.begin(), ::tolower);
	m_ButtonUnhoveredText[PauseMenuButton::BackToMainButton] = newButtonText;

	int newButtonWidth = m_GUIControlManager->GetSkin()->GetFont("FontMainMenu.png")->CalculateWidth(newButtonText) + 50;

	m_PauseMenuButtons[PauseMenuButton::BackToMainButton]->SetSize(newButtonWidth, m_PauseMenuButtons[PauseMenuButton::BackToMainButton]->GetHeight());
	m_PauseMenuButtons[PauseMenuButton::BackToMainButton]->SetText(m_ButtonUnhoveredText[PauseMenuButton::BackToMainButton]);
	m_PauseMenuButtons[PauseMenuButton::BackToMainButton]->CenterInParent(true, false);
#endif
}

#ifdef __EMSCRIPTEN__
void PauseMenuGUI::SetButtonText(PauseMenuButton button, std::string text) {
	std::transform(text.begin(), text.end(), text.begin(), ::toupper);
	m_ButtonHoveredText[button] = text;
	std::transform(text.begin(), text.end(), text.begin(), ::tolower);
	m_ButtonUnhoveredText[button] = text;

	GUIButton* control = m_PauseMenuButtons[button];
	control->SetSize(m_GUIControlManager->GetSkin()->GetFont("FontMainMenu.png")->CalculateWidth(text) + 50, control->GetHeight());
	control->SetText(m_ButtonUnhoveredText[button]);
	control->CenterInParent(true, false);
}

void PauseMenuGUI::LayOutButtons(const std::vector<PauseMenuButton>& rows) {
	for (GUIButton* button: m_PauseMenuButtons) {
		button->SetVisible(false);
		button->SetEnabled(false);
	}
	const int rowHeight = m_PauseMenuButtons[PauseMenuButton::ResumeButton]->GetHeight();
	int columnHeight = 0;
	for (PauseMenuButton row: rows) {
		if (row != PauseMenuButton::ButtonCount) {
			GUIButton* button = m_PauseMenuButtons[row];
			button->SetVisible(true);
			button->SetEnabled(true);
			button->SetPositionRel(button->GetRelXPos(), columnHeight);
		}
		columnHeight += rowHeight;
	}
	// Moving the box moves its buttons.
	m_PauseMenuBox->SetPositionRel(m_PauseMenuBox->GetRelXPos(), m_ButtonsCentreY - columnHeight / 2);
}
#endif

void PauseMenuGUI::EnableOrDisablePauseMenuFeatures() {
	bool disableModManager = true;

	if (const Activity* activity = g_ActivityMan.GetActivity(); activity) {
		disableModManager = activity->GetClassName() != "GAScripted";
	}

#ifdef __EMSCRIPTEN__
	// The port lays the buttons out for three menus (specs/pause-menu.md): a lost
	// scenario's, with Restart Scenario, then Load Game and Exit to Menu; a scenario's,
	// with Restart Scenario above the rest; and upstream's, in Conquest and the editors
	// (whose Activity is named "None", as MenuMan tells them apart).
	const Activity* activity = g_ActivityMan.GetActivity();
	const bool scenario = activity && !g_MetaMan.GameInProgress() && activity->GetPresetName() != "None";
	m_ScenarioLostLayout = g_ActivityMan.ScenarioLost();
	m_ModManagerButtonDisabled = disableModManager;

	const PauseMenuButton gap = PauseMenuButton::ButtonCount;
	std::vector<PauseMenuButton> rows;
	if (m_ScenarioLostLayout) {
		SetButtonText(PauseMenuButton::SaveOrLoadGameButton, "Load Game");
		SetButtonText(PauseMenuButton::BackToMainButton, "Exit to Menu");
		rows = {PauseMenuButton::RestartButton, gap, PauseMenuButton::SaveOrLoadGameButton, PauseMenuButton::BackToMainButton};
	} else {
		SetButtonText(PauseMenuButton::SaveOrLoadGameButton, "Save or Load Game");
		SetButtonText(PauseMenuButton::BackToMainButton, "Back to " + m_BackButtonTargetName + " Menu");
		if (scenario) {
			rows = {PauseMenuButton::RestartButton, gap, PauseMenuButton::BackToMainButton};
		} else {
			rows = {PauseMenuButton::BackToMainButton, gap};
		}
		rows.insert(rows.end(), {PauseMenuButton::SaveOrLoadGameButton, PauseMenuButton::SettingsButton});
		if (!disableModManager) {
			rows.push_back(PauseMenuButton::ModManagerButton);
		}
		rows.insert(rows.end(), {gap, PauseMenuButton::ResumeButton});
	}
	LayOutButtons(rows);
#else
	if (m_ModManagerButtonDisabled != disableModManager) {
		GUIButton* modManagerButton = m_PauseMenuButtons[PauseMenuButton::ModManagerButton];

		modManagerButton->SetEnabled(!disableModManager);
		modManagerButton->SetVisible(!disableModManager);

		int yOffset = m_PauseMenuButtons[PauseMenuButton::ModManagerButton]->GetHeight();

		m_PauseMenuButtons[PauseMenuButton::ResumeButton]->MoveRelative(0, yOffset * (disableModManager ? -1 : 1));
		m_PauseMenuBox->MoveRelative(0, yOffset / 2 * -1);

		m_ModManagerButtonDisabled = disableModManager;
	}
#endif
}

void PauseMenuGUI::SetActiveMenuScreen(PauseMenuScreen screenToShow, bool playButtonPressSound) {
	if (screenToShow != m_ActiveMenuScreen) {
		m_ActiveMenuScreen = screenToShow;
		if (screenToShow == PauseMenuScreen::SaveOrLoadGameScreen) {
			m_SaveLoadMenu->Refresh();
		}
		if (playButtonPressSound) {
			g_GUISound.ButtonPressSound()->Play();
		}
	}
}

PauseMenuGUI::PauseMenuUpdateResult PauseMenuGUI::Update() {
	m_UpdateResult = PauseMenuUpdateResult::NoEvent;

	if (g_ConsoleMan.IsEnabled() && !g_ConsoleMan.IsReadOnly()) {
		return m_UpdateResult;
	}

	bool backToMainScreen = false;

	switch (m_ActiveMenuScreen) {
		case PauseMenuScreen::MainScreen:
			backToMainScreen = HandleInputEvents();
			BlinkResumeButton();
			break;
		case PauseMenuScreen::SaveOrLoadGameScreen:
			backToMainScreen = m_SaveLoadMenu->HandleInputEvents(this);
#ifdef __EMSCRIPTEN__
			// A lost scenario's menu has no Resume, so a game loaded from it is played at once.
			if (backToMainScreen && m_ScenarioLostLayout && !g_ActivityMan.ScenarioLost()) {
				SetActiveMenuScreen(PauseMenuScreen::MainScreen, false);
				m_UpdateResult = PauseMenuUpdateResult::ActivityResumed;
				return m_UpdateResult;
			}
#endif
			break;
		case PauseMenuScreen::SettingsScreen:
			backToMainScreen = m_SettingsMenu->HandleInputEvents();
			m_ActiveDialogBox = m_SettingsMenu->GetActiveDialogBox();
			break;
		case PauseMenuScreen::ModManagerScreen:
			backToMainScreen = m_ModManagerMenu->HandleInputEvents();
			break;
		default:
			break;
	}
	HandleBackNavigation(backToMainScreen);

	return m_UpdateResult;
}

void PauseMenuGUI::HandleBackNavigation(bool backButtonPressed) {
	if (!m_ActiveDialogBox && (backButtonPressed || g_UInputMan.KeyPressed(SDLK_ESCAPE))) {
		if (m_ActiveMenuScreen != PauseMenuScreen::MainScreen) {
			if (m_ActiveMenuScreen == PauseMenuScreen::SettingsScreen || m_ActiveMenuScreen == PauseMenuScreen::ModManagerScreen) {
				if (m_ActiveMenuScreen == PauseMenuScreen::SettingsScreen) {
					m_SettingsMenu->RefreshActiveSettingsMenuScreen();
				}
				g_SettingsMan.UpdateSettingsFile();
			}
			m_ActiveDialogBox = nullptr;
			SetActiveMenuScreen(PauseMenuScreen::MainScreen, false);
		} else {
			m_UpdateResult = PauseMenuUpdateResult::ActivityResumed;
		}
		g_GUISound.BackButtonPressSound()->Play();
	} else if (m_ActiveMenuScreen == PauseMenuScreen::SettingsScreen && m_ActiveDialogBox && g_UInputMan.KeyPressed(SDLK_ESCAPE)) {
		m_SettingsMenu->CloseActiveDialogBox();
	}
}

bool PauseMenuGUI::HandleInputEvents() {
	if (m_ActiveMenuScreen == PauseMenuScreen::MainScreen) {
		int mousePosX;
		int mousePosY;
		m_GUIControlManager->GetManager()->GetInputController()->GetMousePosition(&mousePosX, &mousePosY);
		UpdateHoveredButton(dynamic_cast<GUIButton*>(m_GUIControlManager->GetControlUnderPoint(mousePosX, mousePosY, m_PauseMenuBox, 1)));
	}
	m_GUIControlManager->Update();

	GUIEvent guiEvent;
	while (m_GUIControlManager->GetEvent(&guiEvent)) {
		if (guiEvent.GetType() == GUIEvent::Command) {
			if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::ResumeButton]) {
				return true;
			} else if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::SaveOrLoadGameButton]) {
				SetActiveMenuScreen(PauseMenuScreen::SaveOrLoadGameScreen);
			} else if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::SettingsButton]) {
				SetActiveMenuScreen(PauseMenuScreen::SettingsScreen);
			} else if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::ModManagerButton]) {
				SetActiveMenuScreen(PauseMenuScreen::ModManagerScreen);
			} else if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::BackToMainButton]) {
				g_GUISound.BackButtonPressSound()->Play();
				m_UpdateResult = PauseMenuUpdateResult::BackToMain;
#ifdef __EMSCRIPTEN__
			} else if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::RestartButton]) {
				g_GUISound.ButtonPressSound()->Play();
				m_UpdateResult = PauseMenuUpdateResult::ActivityRestarted;
#endif
			}
		}
		if (guiEvent.GetType() == GUIEvent::Notification && (guiEvent.GetMsg() == GUIButton::Focused && dynamic_cast<GUIButton*>(guiEvent.GetControl()))) {
			g_GUISound.SelectionChangeSound()->Play();
		}
	}
	return false;
}

void PauseMenuGUI::UpdateHoveredButton(const GUIButton* hoveredButton) {
	int hoveredButtonIndex = -1;
	if (hoveredButton) {
		hoveredButtonIndex = std::distance(m_PauseMenuButtons.begin(), std::find(m_PauseMenuButtons.begin(), m_PauseMenuButtons.end(), hoveredButton));
		if (hoveredButton != m_HoveredButton) {
			m_PauseMenuButtons[hoveredButtonIndex]->SetText(m_ButtonHoveredText[hoveredButtonIndex]);
		}
		m_HoveredButton = m_PauseMenuButtons[hoveredButtonIndex];
	}
	if (!hoveredButton || hoveredButtonIndex != m_PrevHoveredButtonIndex) {
		m_PauseMenuButtons[m_PrevHoveredButtonIndex]->SetText(m_ButtonUnhoveredText[m_PrevHoveredButtonIndex]);
	}

	if (hoveredButtonIndex >= 0) {
		m_PrevHoveredButtonIndex = hoveredButtonIndex;
	} else {
		m_HoveredButton = nullptr;
	}
}

void PauseMenuGUI::BlinkResumeButton() {
	if (m_HoveredButton && m_HoveredButton == m_PauseMenuButtons[PauseMenuButton::ResumeButton]) {
		m_PauseMenuButtons[PauseMenuButton::ResumeButton]->SetText(m_ResumeButtonBlinkTimer.AlternateReal(500) ? m_ButtonHoveredText[PauseMenuButton::ResumeButton] : "]" + m_ButtonHoveredText[PauseMenuButton::ResumeButton] + "[");
	} else {
		m_PauseMenuButtons[PauseMenuButton::ResumeButton]->SetText(m_ResumeButtonBlinkTimer.AlternateReal(500) ? m_ButtonUnhoveredText[PauseMenuButton::ResumeButton] : ">" + m_ButtonUnhoveredText[PauseMenuButton::ResumeButton] + "<");
	}
}

void PauseMenuGUI::Draw() {
	g_WindowMan.DrawPostProcessBuffer();
	blit(m_BackdropBitmap, g_FrameMan.GetBackBuffer32(), 0, 0, 0, 0, m_BackdropBitmap->w, m_BackdropBitmap->h);

	switch (m_ActiveMenuScreen) {
		case PauseMenuScreen::SaveOrLoadGameScreen:
			m_SaveLoadMenu->Draw();
			break;
		case PauseMenuScreen::SettingsScreen:
			m_SettingsMenu->Draw();
			break;
		case PauseMenuScreen::ModManagerScreen:
			m_ModManagerMenu->Draw();
			break;
		default:
			m_GUIControlManager->Draw();
			break;
	}
	if (m_ActiveDialogBox) {
		SetTrueAlphaBlender();
		// Whatever this box may be at this point it's already been drawn by the owning GUIControlManager, but we need to draw it again on top of the overlay so it's not affected by it.
		m_ActiveDialogBox->Draw(m_GUIControlManager->GetScreen());
	}
	m_GUIControlManager->DrawMouse();
}
