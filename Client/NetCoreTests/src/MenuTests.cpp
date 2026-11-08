// Front-end menu model (engine-independent part of the Unreal menus).
#include "TestFramework.h"
#include "Ui/TfpsMenu.h"

#include <cmath>

using namespace tfps::ui;

namespace
{
	int IndexOf(const Menu& M, const std::string& Id)
	{
		for (size_t I = 0; I < M.Items().size(); ++I)
		{
			if (M.Items()[I].Id == Id)
			{
				return static_cast<int>(I);
			}
		}
		return -1;
	}

	/** Clicks the centre of an item. */
	void Click(Menu& M, const std::string& Id)
	{
		const int I = IndexOf(M, Id);
		TFPS_REQUIRE(I >= 0);
		const Rect R = M.ItemRect(I);
		M.OnClick(R.X + 10.0f, R.Y + R.H * 0.5f);
	}

	void Focus(Menu& M, const std::string& Id)
	{
		const int I = IndexOf(M, Id);
		TFPS_REQUIRE(I >= 0);
		const Rect R = M.ItemRect(I);
		M.OnMouseMove(R.X + 10.0f, R.Y + R.H * 0.5f);
	}

	LobbyState HostLobby()
	{
		LobbyState L;
		L.bConfigured = true;
		L.bInLobby = true;
		L.Code = "ABC123";
		L.Status = "open";
		L.Host = 1;
		L.You = 1;
		L.Members = {{1, "Host", 0, false}, {2, "Friend", 1, true}};
		L.Maps = {"consulate", "harbor", "villa"};
		L.Map = "villa";
		return L;
	}

	const Action* Find(const std::vector<Action>& A, ActionKind K)
	{
		for (const Action& X : A)
		{
			if (X.Kind == K)
			{
				return &X;
			}
		}
		return nullptr;
	}
}

TFPS_TEST(MenuSettingsRoundTripAndSanitize)
{
	UserSettings S;
	S.PlayerName = "Ghost_7";
	S.MouseSensitivity = 0.125f;
	S.FieldOfView = 103.0f;
	S.bInvertY = true;
	S.Keys[0].Key = "Up";
	const UserSettings Back = UserSettings::Parse(S.Serialize());
	TFPS_EXPECT(Back == S);

	// Out-of-range, malformed and hostile values are repaired.
	const UserSettings Bad = UserSettings::Parse(
		"name=<script>alert(1)</script>xxxxxxxxxxxxxxxx\n"
		"sensitivity=50\n"
		"fov=nan\n"
		"ads=-3\n"
		"master_volume=0.5garbage\n"
		"key.Forward=\n"
		"key.Jump=Escape\n"
		"key.Unknown=K\n"
		"no equals sign\n"
		"# comment\n");
	TFPS_EXPECT(Bad.PlayerName == "scriptalert1scri");
	TFPS_EXPECT(Bad.MouseSensitivity == kSensitivityRange.Max);
	TFPS_EXPECT(Bad.FieldOfView == UserSettings().FieldOfView);
	TFPS_EXPECT(Bad.AdsMultiplier == kAdsRange.Min);
	TFPS_EXPECT(Bad.MasterVolume == UserSettings().MasterVolume);
	TFPS_EXPECT(*Bad.KeyFor("Forward") == "W");
	TFPS_EXPECT(*Bad.KeyFor("Jump") == "SpaceBar");
	TFPS_EXPECT(Bad.KeyFor("Unknown") == nullptr);
	TFPS_EXPECT(Bad.Keys.size() == DefaultBindings().size());
	// Windows line endings and spaces around '='.
	TFPS_EXPECT(UserSettings::Parse("fov = 80\r\nname=Bob\r\n").FieldOfView == 80.0f);
	TFPS_EXPECT(UserSettings::Parse("fov = 80\r\nname=Bob\r\n").PlayerName == "Bob");
	TFPS_EXPECT(FormatClock(185.0f) == "3:05");
}

TFPS_TEST(MenuMainNavigationSkipsDisabledItems)
{
	Menu M;
	LobbyState Offline; // no lobby service: host / join disabled
	M.Sync(Offline, false);
	TFPS_EXPECT(M.Current() == Screen::Main);
	TFPS_EXPECT(!M.Items()[0].bEnabled && !M.Items()[1].bEnabled);
	M.OnNav(Nav::Down);
	TFPS_EXPECT(M.Items()[M.Focus()].Id == "direct");
	M.OnNav(Nav::Up);
	TFPS_EXPECT(M.Items()[M.Focus()].Id == "quit"); // wraps past the disabled items
	M.OnNav(Nav::Accept);
	TFPS_EXPECT(Find(M.TakeActions(), ActionKind::Quit) != nullptr);
	// Clicking a disabled item does nothing.
	Click(M, "host");
	TFPS_EXPECT(M.TakeActions().empty());
}

TFPS_TEST(MenuJoinByCodeFlowsIntoTheLobbyScreen)
{
	Menu M;
	LobbyState L;
	L.bConfigured = true;
	M.Sync(L, false);
	Click(M, "join");
	TFPS_REQUIRE(M.Current() == Screen::JoinLobby);
	TFPS_EXPECT(M.IsEditingText());
	TFPS_EXPECT(!M.Items()[IndexOf(M, "do_join")].bEnabled);
	for (char C : std::string("ab-c 12x9"))
	{
		M.OnChar(C);
	}
	TFPS_EXPECT(M.JoinCode() == "ABC12X"); // upper-cased, invalid chars dropped, 6 max
	M.OnBackspace();
	M.OnChar('3');
	TFPS_EXPECT(M.JoinCode() == "ABC123");
	M.OnNav(Nav::Accept); // Enter in the field joins
	const std::vector<Action> A = M.TakeActions();
	const Action* J = Find(A, ActionKind::JoinLobby);
	TFPS_REQUIRE(J != nullptr);
	TFPS_EXPECT(J->Value == "ABC123");
	// The lobby client reports we are in: the lobby screen replaces the stack.
	L = HostLobby();
	M.Sync(L, false);
	TFPS_EXPECT(M.Current() == Screen::Lobby);
	M.OnNav(Nav::Back); // the lobby root does not close by Esc
	TFPS_EXPECT(M.Current() == Screen::Lobby);
}

TFPS_TEST(MenuLobbyHostEditsSettingsGuestCannot)
{
	Menu M;
	LobbyState L = HostLobby();
	M.Sync(L, false);
	Focus(M, "map");
	M.OnNav(Nav::Right);
	M.OnNav(Nav::Left);
	std::vector<Action> A = M.TakeActions();
	TFPS_REQUIRE(A.size() == 2);
	TFPS_EXPECT(A[0].Kind == ActionKind::LobbySetting && A[0].Key == "map" && A[0].Value == "consulate"); // wraps
	TFPS_EXPECT(A[1].Value == "harbor");

	Focus(M, "team_size");
	M.OnNav(Nav::Accept);
	A = M.TakeActions();
	TFPS_REQUIRE(A.size() == 1);
	TFPS_EXPECT(A[0].Key == "team_size" && A[0].Value == "1"); // 5 wraps to 1

	Focus(M, "round_time");
	M.OnNav(Nav::Right);
	Focus(M, "prep_time");
	M.OnNav(Nav::Left);
	Focus(M, "tick_rate");
	M.OnNav(Nav::Right);
	Focus(M, "friendly_fire");
	M.OnNav(Nav::Accept);
	A = M.TakeActions();
	TFPS_REQUIRE(A.size() == 4);
	TFPS_EXPECT(A[0].Value == "210");
	TFPS_EXPECT(A[1].Value == "15");
	TFPS_EXPECT(A[2].Value == "128");
	TFPS_EXPECT(A[3].Key == "friendly_fire" && A[3].Value == "true");

	// Deathmatch shows a kill limit instead of rounds.
	L.Mode = "deathmatch";
	M.Sync(L, false);
	TFPS_EXPECT(IndexOf(M, "rounds_to_win") < 0 && IndexOf(M, "score_limit") >= 0);
	TFPS_EXPECT(M.Items()[IndexOf(M, "round_time")].Label == "Time limit");

	Focus(M, "squad");
	M.OnNav(Nav::Accept);
	Focus(M, "ready");
	M.OnNav(Nav::Accept);
	Click(M, "start");
	A = M.TakeActions();
	TFPS_REQUIRE(A.size() == 3);
	TFPS_EXPECT(A[0].Kind == ActionKind::SetSquad && A[0].Value == "1");
	TFPS_EXPECT(A[1].Kind == ActionKind::SetReady && A[1].Value == "true");
	TFPS_EXPECT(A[2].Kind == ActionKind::StartMatch);

	// A guest sees the settings but cannot change them or start.
	L.You = 2;
	M.Sync(L, false);
	TFPS_EXPECT(!M.Items()[IndexOf(M, "map")].bEnabled);
	TFPS_EXPECT(!M.Items()[IndexOf(M, "start")].bEnabled);
	TFPS_EXPECT(M.Items()[IndexOf(M, "squad")].Value == "Squad B");
	TFPS_EXPECT(M.Items()[IndexOf(M, "ready")].Value == "Ready");
	Click(M, "map");
	Click(M, "start");
	TFPS_EXPECT(M.TakeActions().empty());
	Click(M, "ready");
	A = M.TakeActions();
	TFPS_REQUIRE(A.size() == 1);
	TFPS_EXPECT(A[0].Value == "false");
}

TFPS_TEST(MenuSettingsSlidersTogglesAndMouse)
{
	Menu M;
	M.Sync(LobbyState(), false);
	Click(M, "settings");
	TFPS_REQUIRE(M.Current() == Screen::Settings);
	const float Before = M.Settings().MouseSensitivity;
	Focus(M, "sensitivity");
	M.OnNav(Nav::Right);
	TFPS_EXPECT(std::fabs(M.Settings().MouseSensitivity - (Before + kSensitivityRange.Step)) < 1e-5f);
	for (int I = 0; I < 200; ++I)
	{
		M.OnNav(Nav::Left);
	}
	TFPS_EXPECT(M.Settings().MouseSensitivity == kSensitivityRange.Min);
	// Clicking the right end of the FOV bar sets the maximum.
	const int Fov = IndexOf(M, "fov");
	const Rect R = M.ItemRect(Fov);
	M.OnClick(R.X + R.W - 1.0f, R.Y + 5.0f);
	TFPS_EXPECT(std::fabs(M.Settings().FieldOfView - kFovRange.Max) < 0.51f);
	Click(M, "invert_y");
	TFPS_EXPECT(M.Settings().bInvertY);
	TFPS_EXPECT(M.Items()[IndexOf(M, "invert_y")].Value == "On");
	TFPS_EXPECT(Find(M.TakeActions(), ActionKind::SettingsChanged) != nullptr);
	// Esc returns to the main menu with the focus where it was.
	M.OnNav(Nav::Back);
	TFPS_EXPECT(M.Current() == Screen::Main);
	TFPS_EXPECT(M.Items()[M.Focus()].Id == "settings");
}

TFPS_TEST(MenuKeyRebindingSwapsConflicts)
{
	Menu M;
	M.Sync(LobbyState(), false);
	Click(M, "settings");
	Click(M, "controls");
	TFPS_REQUIRE(M.Current() == Screen::Controls);
	Click(M, "key.Jump");
	TFPS_EXPECT(M.IsCapturingKey());
	TFPS_EXPECT(M.Items()[IndexOf(M, "key.Jump")].Value == "press a key...");
	M.OnNav(Nav::Down); // ignored while capturing
	M.OnKeyCaptured("W"); // W was Forward: Forward takes Space
	TFPS_EXPECT(!M.IsCapturingKey());
	TFPS_EXPECT(*M.Settings().KeyFor("Jump") == "W");
	TFPS_EXPECT(*M.Settings().KeyFor("Forward") == "SpaceBar");
	TFPS_EXPECT(Find(M.TakeActions(), ActionKind::SettingsChanged) != nullptr);
	// Escape cancels capture and changes nothing.
	Click(M, "key.Fire");
	M.OnKeyCaptured("Escape");
	TFPS_EXPECT(*M.Settings().KeyFor("Fire") == "LeftMouseButton");
	TFPS_EXPECT(M.Current() == Screen::Controls);
	TFPS_EXPECT(M.Items()[IndexOf(M, "key.Fire")].Value == "Left Mouse");
	Click(M, "reset_keys");
	TFPS_EXPECT(M.Settings().Keys.size() == DefaultBindings().size());
	TFPS_EXPECT(*M.Settings().KeyFor("Jump") == "SpaceBar");
	// Every item fits on screen even on the longest page.
	const Rect Last = M.ItemRect(static_cast<int>(M.Items().size()) - 1);
	TFPS_EXPECT(Last.Y + Last.H <= kVirtualHeight);
}

TFPS_TEST(MenuPauseInMatchAndFrontEndClosesOnMatchStart)
{
	Menu M;
	LobbyState L = HostLobby();
	M.Sync(L, false);
	Click(M, "settings");
	TFPS_EXPECT(M.Current() == Screen::Settings);
	// Match starts: the front end closes.
	M.Sync(L, true);
	TFPS_EXPECT(!M.IsOpen());
	M.TogglePause();
	TFPS_REQUIRE(M.Current() == Screen::Pause);
	Click(M, "settings");
	TFPS_EXPECT(M.Current() == Screen::Settings);
	M.Sync(L, true); // stays open across frames
	TFPS_EXPECT(M.Current() == Screen::Settings);
	M.TogglePause(); // back to pause
	TFPS_EXPECT(M.Current() == Screen::Pause);
	M.TogglePause(); // closes
	TFPS_EXPECT(!M.IsOpen());
	TFPS_EXPECT(Find(M.TakeActions(), ActionKind::Resume) != nullptr);
	M.TogglePause();
	Click(M, "leave_match");
	TFPS_EXPECT(!M.IsOpen());
	TFPS_EXPECT(Find(M.TakeActions(), ActionKind::LeaveMatch) != nullptr);
	// Match over: back to the lobby screen.
	M.Sync(L, false);
	TFPS_EXPECT(M.Current() == Screen::Lobby);
}

TFPS_TEST(MenuDirectConnectRemembersTheServer)
{
	UserSettings S;
	S.LastServer = "10.0.0.5:7777";
	Menu M(S);
	M.Sync(LobbyState(), false);
	Click(M, "direct");
	TFPS_REQUIRE(M.Current() == Screen::DirectConnect);
	TFPS_EXPECT(M.ServerAddress() == "10.0.0.5:7777");
	for (int I = 0; I < 4; ++I)
	{
		M.OnBackspace();
	}
	for (char C : std::string("7778"))
	{
		M.OnChar(C);
	}
	Focus(M, "direct_map");
	M.OnNav(Nav::Right);
	TFPS_EXPECT(M.DirectMap() == "test_arena");
	Click(M, "connect");
	const std::vector<Action> A = M.TakeActions();
	const Action* C = Find(A, ActionKind::DirectConnect);
	TFPS_REQUIRE(C != nullptr);
	TFPS_EXPECT(C->Value == "10.0.0.5:7778" && C->Key == "test_arena");
	TFPS_EXPECT(M.Settings().LastServer == "10.0.0.5:7778");
}
