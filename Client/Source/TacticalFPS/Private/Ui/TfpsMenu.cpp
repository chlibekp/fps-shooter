// Copyright TacticalFPS project.
#include "Ui/TfpsMenu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tfps
{
namespace ui
{

const SettingRange kSensitivityRange = {0.01f, 0.30f, 0.005f};
const SettingRange kAdsRange = {0.20f, 1.50f, 0.05f};
const SettingRange kFovRange = {70.0f, 110.0f, 1.0f};
const SettingRange kVolumeRange = {0.0f, 1.0f, 0.05f};

namespace
{
struct ActionInfo
{
	const char* Action;
	const char* DefaultKey;
	const char* Label;
};

// Escape is reserved for the menu and cannot be bound.
const ActionInfo kActions[] = {
	{"Forward", "W", "Move forward"},
	{"Back", "S", "Move back"},
	{"Left", "A", "Move left"},
	{"Right", "D", "Move right"},
	{"Jump", "SpaceBar", "Jump / vault"},
	{"Crouch", "LeftControl", "Crouch (hold)"},
	{"CrouchAlt", "C", "Crouch"},
	{"Prone", "Z", "Prone"},
	{"Sprint", "LeftShift", "Sprint"},
	{"Walk", "LeftAlt", "Walk"},
	{"LeanLeft", "Q", "Lean left"},
	{"LeanRight", "E", "Lean right"},
	{"Fire", "LeftMouseButton", "Fire"},
	{"Aim", "RightMouseButton", "Aim down sights"},
	{"Reload", "R", "Reload"},
	{"Use", "F", "Use / plant / reinforce"},
	{"PrimaryWeapon", "One", "Primary weapon"},
	{"SecondaryWeapon", "Two", "Secondary weapon"},
	{"Gadget1", "Three", "Gadget 1"},
	{"Gadget2", "Four", "Gadget 2"},
	{"Gadget3", "Five", "Gadget 3 (drone)"},
	{"Gadget4", "Six", "Gadget 4"},
	{"NextWeapon", "MouseScrollDown", "Next weapon"},
	{"PrevWeapon", "MouseScrollUp", "Previous weapon"},
	{"ToggleNetStats", "F3", "Network stats"},
};

const char* const kModes[] = {"deathmatch", "elimination", "defuse"};
const char* const kDefaultMaps[] = {"consulate", "harbor", "test_arena", "villa"};
constexpr size_t kMaxName = 16;
constexpr size_t kMaxUrl = 128;
constexpr size_t kMaxAddress = 64;
constexpr size_t kCodeLength = 6;

const char* ActionLabel(const std::string& Action)
{
	for (const ActionInfo& A : kActions)
	{
		if (Action == A.Action)
		{
			return A.Label;
		}
	}
	return "?";
}

std::string KeyDisplayName(const std::string& Key)
{
	static const char* const kNames[][2] = {
		{"LeftMouseButton", "Left Mouse"}, {"RightMouseButton", "Right Mouse"}, {"MiddleMouseButton", "Middle Mouse"},
		{"ThumbMouseButton", "Mouse 4"}, {"ThumbMouseButton2", "Mouse 5"}, {"MouseScrollUp", "Wheel Up"},
		{"MouseScrollDown", "Wheel Down"}, {"SpaceBar", "Space"}, {"LeftControl", "Left Ctrl"},
		{"RightControl", "Right Ctrl"}, {"LeftShift", "Left Shift"}, {"RightShift", "Right Shift"},
		{"LeftAlt", "Left Alt"}, {"RightAlt", "Right Alt"}, {"Zero", "0"}, {"One", "1"}, {"Two", "2"},
		{"Three", "3"}, {"Four", "4"}, {"Five", "5"}, {"Six", "6"}, {"Seven", "7"}, {"Eight", "8"},
		{"Nine", "9"},
	};
	for (const auto& N : kNames)
	{
		if (Key == N[0])
		{
			return N[1];
		}
	}
	return Key.empty() ? std::string("(unbound)") : Key;
}

float Clamp(float V, const SettingRange& R, float Default)
{
	if (!std::isfinite(V))
	{
		return Default;
	}
	return std::min(R.Max, std::max(R.Min, V));
}

float Step(float V, const SettingRange& R, int Direction)
{
	const float Steps = std::round((V - R.Min) / R.Step) + static_cast<float>(Direction);
	return Clamp(R.Min + Steps * R.Step, R, V);
}

float FractionOf(float V, const SettingRange& R)
{
	return (V - R.Min) / (R.Max - R.Min);
}

/** Locale-independent fixed-point formatting. */
std::string FormatFixed(float V, int Decimals)
{
	int64_t Scale = 1;
	for (int I = 0; I < Decimals; ++I)
	{
		Scale *= 10;
	}
	const bool bNeg = V < 0.0f;
	const int64_t Q = static_cast<int64_t>(std::llround(std::fabs(static_cast<double>(V)) * static_cast<double>(Scale)));
	std::string S = (bNeg && Q != 0 ? "-" : "") + std::to_string(Q / Scale);
	if (Decimals > 0)
	{
		std::string Frac = std::to_string(Q % Scale);
		while (Frac.size() < static_cast<size_t>(Decimals))
		{
			Frac.insert(Frac.begin(), '0');
		}
		S += "." + Frac;
	}
	return S;
}

/** Locale-independent parse of [-]digits[.digits]; false on anything else. */
bool ParseFloat(const std::string& S, float& Out)
{
	size_t I = 0;
	bool bNeg = false;
	if (I < S.size() && (S[I] == '-' || S[I] == '+'))
	{
		bNeg = S[I] == '-';
		++I;
	}
	double V = 0.0;
	bool bDigits = false;
	while (I < S.size() && S[I] >= '0' && S[I] <= '9')
	{
		V = V * 10.0 + (S[I] - '0');
		bDigits = true;
		++I;
	}
	if (I < S.size() && S[I] == '.')
	{
		++I;
		double Place = 0.1;
		while (I < S.size() && S[I] >= '0' && S[I] <= '9')
		{
			V += (S[I] - '0') * Place;
			Place *= 0.1;
			bDigits = true;
			++I;
		}
	}
	if (!bDigits || I != S.size())
	{
		return false;
	}
	Out = static_cast<float>(bNeg ? -V : V);
	return true;
}

bool IsNameChar(char C)
{
	return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' || C == '-';
}

bool IsCodeChar(char C)
{
	return (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9');
}

bool IsAddressChar(char C)
{
	return IsNameChar(C) || C == '.' || C == ':';
}

bool IsUrlChar(char C)
{
	return IsAddressChar(C) || C == '/';
}

std::string Filter(const std::string& S, bool (*Ok)(char), size_t MaxLen)
{
	std::string R;
	for (char C : S)
	{
		if (Ok(C) && R.size() < MaxLen)
		{
			R.push_back(C);
		}
	}
	return R;
}

std::string Trim(const std::string& S)
{
	size_t B = 0;
	size_t E = S.size();
	while (B < E && (S[B] == ' ' || S[B] == '\t' || S[B] == '\r'))
	{
		++B;
	}
	while (E > B && (S[E - 1] == ' ' || S[E - 1] == '\t' || S[E - 1] == '\r'))
	{
		--E;
	}
	return S.substr(B, E - B);
}

std::string Percent(float V)
{
	return std::to_string(static_cast<int>(std::lround(V * 100.0f))) + "%";
}

std::string OnOff(bool B)
{
	return B ? "On" : "Off";
}

/** Index of `V` in a stepped integer range, wrapped. */
int StepInt(int V, int Min, int Max, int StepBy, int Direction)
{
	int N = (V - Min) / StepBy + Direction;
	const int Count = (Max - Min) / StepBy + 1;
	N = ((N % Count) + Count) % Count;
	return Min + N * StepBy;
}

Item MakeItem(const char* Id, const char* Label, ItemKind Kind, std::string Value = std::string(), bool bEnabled = true)
{
	Item I;
	I.Id = Id;
	I.Label = Label;
	I.Kind = Kind;
	I.Value = std::move(Value);
	I.bEnabled = bEnabled;
	return I;
}
} // namespace

std::vector<KeyBinding> DefaultBindings()
{
	std::vector<KeyBinding> R;
	for (const ActionInfo& A : kActions)
	{
		R.push_back({A.Action, A.DefaultKey});
	}
	return R;
}

std::string FormatClock(float Seconds)
{
	const int S = std::max(0, static_cast<int>(std::lround(Seconds)));
	char Buf[16];
	std::snprintf(Buf, sizeof(Buf), "%d:%02d", S / 60, S % 60);
	return Buf;
}

// ---------------------------------------------------------------------------
// UserSettings

void UserSettings::Sanitize()
{
	const UserSettings D;
	PlayerName = Filter(PlayerName, IsNameChar, kMaxName);
	if (PlayerName.empty())
	{
		PlayerName = D.PlayerName;
	}
	LobbyUrl = Filter(LobbyUrl, IsUrlChar, kMaxUrl);
	LastServer = Filter(LastServer, IsAddressChar, kMaxAddress);
	MouseSensitivity = Clamp(MouseSensitivity, kSensitivityRange, D.MouseSensitivity);
	AdsMultiplier = Clamp(AdsMultiplier, kAdsRange, D.AdsMultiplier);
	FieldOfView = Clamp(FieldOfView, kFovRange, D.FieldOfView);
	MasterVolume = Clamp(MasterVolume, kVolumeRange, D.MasterVolume);
	EffectsVolume = Clamp(EffectsVolume, kVolumeRange, D.EffectsVolume);
	// Exactly the known actions, in display order; missing or empty ones get the default.
	std::vector<KeyBinding> Fixed;
	for (const ActionInfo& A : kActions)
	{
		std::string Key = A.DefaultKey;
		for (const KeyBinding& K : Keys)
		{
			if (K.Action == A.Action && !K.Key.empty() && K.Key != "Escape")
			{
				Key = K.Key;
			}
		}
		Fixed.push_back({A.Action, Key});
	}
	Keys = std::move(Fixed);
}

std::string UserSettings::Serialize() const
{
	std::string S;
	auto Line = [&S](const char* K, const std::string& V) { S += std::string(K) + "=" + V + "\n"; };
	Line("name", PlayerName);
	Line("lobby_url", LobbyUrl);
	Line("server", LastServer);
	Line("sensitivity", FormatFixed(MouseSensitivity, 4));
	Line("ads", FormatFixed(AdsMultiplier, 3));
	Line("invert_y", bInvertY ? "1" : "0");
	Line("toggle_crouch", bToggleCrouch ? "1" : "0");
	Line("fov", FormatFixed(FieldOfView, 1));
	Line("master_volume", FormatFixed(MasterVolume, 3));
	Line("effects_volume", FormatFixed(EffectsVolume, 3));
	Line("net_stats", bShowNetStats ? "1" : "0");
	for (const KeyBinding& K : Keys)
	{
		S += "key." + K.Action + "=" + K.Key + "\n";
	}
	return S;
}

UserSettings UserSettings::Parse(const std::string& Text)
{
	UserSettings U;
	size_t Pos = 0;
	while (Pos <= Text.size())
	{
		size_t End = Text.find('\n', Pos);
		if (End == std::string::npos)
		{
			End = Text.size();
		}
		const std::string LineText = Trim(Text.substr(Pos, End - Pos));
		Pos = End + 1;
		const size_t Eq = LineText.find('=');
		if (LineText.empty() || LineText[0] == '#' || LineText[0] == ';' || Eq == std::string::npos)
		{
			continue;
		}
		const std::string K = Trim(LineText.substr(0, Eq));
		const std::string V = Trim(LineText.substr(Eq + 1));
		float F = 0.0f;
		const bool bNum = ParseFloat(V, F);
		const bool bBool = V == "1" || V == "true" || V == "on";
		if (K == "name") { U.PlayerName = V; }
		else if (K == "lobby_url") { U.LobbyUrl = V; }
		else if (K == "server") { U.LastServer = V; }
		else if (K == "sensitivity" && bNum) { U.MouseSensitivity = F; }
		else if (K == "ads" && bNum) { U.AdsMultiplier = F; }
		else if (K == "invert_y") { U.bInvertY = bBool; }
		else if (K == "toggle_crouch") { U.bToggleCrouch = bBool; }
		else if (K == "fov" && bNum) { U.FieldOfView = F; }
		else if (K == "master_volume" && bNum) { U.MasterVolume = F; }
		else if (K == "effects_volume" && bNum) { U.EffectsVolume = F; }
		else if (K == "net_stats") { U.bShowNetStats = bBool; }
		else if (K.compare(0, 4, "key.") == 0)
		{
			const std::string Action = K.substr(4);
			for (KeyBinding& B : U.Keys)
			{
				if (B.Action == Action)
				{
					B.Key = V;
				}
			}
		}
		if (End == Text.size())
		{
			break;
		}
	}
	U.Sanitize();
	return U;
}

const std::string* UserSettings::KeyFor(const std::string& Action) const
{
	for (const KeyBinding& K : Keys)
	{
		if (K.Action == Action)
		{
			return &K.Key;
		}
	}
	return nullptr;
}

bool UserSettings::operator==(const UserSettings& O) const
{
	if (Keys.size() != O.Keys.size())
	{
		return false;
	}
	for (size_t I = 0; I < Keys.size(); ++I)
	{
		if (Keys[I].Action != O.Keys[I].Action || Keys[I].Key != O.Keys[I].Key)
		{
			return false;
		}
	}
	return PlayerName == O.PlayerName && LobbyUrl == O.LobbyUrl && LastServer == O.LastServer &&
		MouseSensitivity == O.MouseSensitivity && AdsMultiplier == O.AdsMultiplier && bInvertY == O.bInvertY &&
		bToggleCrouch == O.bToggleCrouch && FieldOfView == O.FieldOfView && MasterVolume == O.MasterVolume &&
		EffectsVolume == O.EffectsVolume && bShowNetStats == O.bShowNetStats;
}

const LobbyMember* LobbyState::Me() const
{
	for (const LobbyMember& M : Members)
	{
		if (M.Id == You)
		{
			return &M;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Menu

Menu::Menu(UserSettings InSettings)
	: Prefs(std::move(InSettings))
{
	Prefs.Sanitize();
	AddressText = Prefs.LastServer;
	Stack.push_back(Screen::Main);
	Rebuild();
}

void Menu::SetSettings(const UserSettings& S)
{
	Prefs = S;
	Prefs.Sanitize();
	Rebuild();
}

void Menu::Sync(const LobbyState& Lobby, bool bMatch)
{
	LobbyView = Lobby;
	bInMatch = bMatch;
	if (bInMatch)
	{
		// Front-end screens close when the match starts; the pause menu stays.
		if (!Stack.empty() && Stack.front() != Screen::Pause)
		{
			Stack.clear();
			FocusStack.clear();
			bCapturingKey = false;
			FocusIndex = 0;
		}
	}
	else
	{
		const Screen Root = Lobby.bInLobby ? Screen::Lobby : Screen::Main;
		if (Stack.empty() || Stack.front() != Root)
		{
			Stack.assign(1, Root);
			FocusStack.clear();
			bCapturingKey = false;
			FocusIndex = 0;
		}
	}
	Rebuild();
}

void Menu::Push(Screen S)
{
	FocusStack.push_back(FocusIndex);
	Stack.push_back(S);
	FocusIndex = 0;
	Rebuild();
	MoveFocus(0);
}

void Menu::Pop()
{
	if (Stack.empty())
	{
		return;
	}
	Stack.pop_back();
	FocusIndex = 0;
	if (!FocusStack.empty())
	{
		FocusIndex = FocusStack.back();
		FocusStack.pop_back();
	}
	Rebuild();
	MoveFocus(0);
}

void Menu::TogglePause()
{
	if (!bInMatch)
	{
		OnNav(Nav::Back);
		return;
	}
	if (Stack.empty())
	{
		Push(Screen::Pause);
		return;
	}
	OnNav(Nav::Back);
}

const char* Menu::Title() const
{
	switch (Current())
	{
	case Screen::Main: return "TACTICAL FPS";
	case Screen::JoinLobby: return "JOIN A LOBBY";
	case Screen::DirectConnect: return "DIRECT CONNECT";
	case Screen::Lobby: return "LOBBY";
	case Screen::Settings: return "SETTINGS";
	case Screen::Controls: return "CONTROLS";
	case Screen::Pause: return "PAUSED";
	case Screen::None: break;
	}
	return "";
}

Rect Menu::PanelRect() const
{
	const float Top = 150.0f;
	if (Current() == Screen::Lobby)
	{
		return {1000.0f, Top, 680.0f, kVirtualHeight - Top - 60.0f};
	}
	return {640.0f, Top, 640.0f, kVirtualHeight - Top - 60.0f};
}

Rect Menu::ItemRect(int Index) const
{
	const Rect P = PanelRect();
	const float Top = P.Y + 90.0f;
	const float Avail = P.Y + P.H - 20.0f - Top;
	const float N = static_cast<float>(std::max<size_t>(1, CachedItems.size()));
	const float Pitch = std::min(56.0f, Avail / N);
	return {P.X + 20.0f, Top + Pitch * static_cast<float>(Index), P.W - 40.0f, Pitch - 6.0f};
}

bool Menu::IsEditingText() const
{
	return FocusIndex >= 0 && FocusIndex < static_cast<int>(CachedItems.size()) &&
		CachedItems[FocusIndex].Kind == ItemKind::Text && CachedItems[FocusIndex].bEnabled;
}

std::string* Menu::EditedText()
{
	if (!IsEditingText())
	{
		return nullptr;
	}
	const std::string& Id = CachedItems[FocusIndex].Id;
	if (Id == "code") { return &JoinCodeText; }
	if (Id == "address") { return &AddressText; }
	if (Id == "name") { return &Prefs.PlayerName; }
	if (Id == "lobby_url") { return &Prefs.LobbyUrl; }
	return nullptr;
}

void Menu::Rebuild()
{
	const std::string FocusId =
		FocusIndex >= 0 && FocusIndex < static_cast<int>(CachedItems.size()) ? CachedItems[FocusIndex].Id : std::string();
	std::vector<Item>& R = CachedItems;
	R.clear();
	const bool bHost = LobbyView.IsHost();
	switch (Current())
	{
	case Screen::None:
		break;
	case Screen::Main:
		R.push_back(MakeItem("host", "Host a lobby", ItemKind::Button, "", LobbyView.bConfigured));
		R.push_back(MakeItem("join", "Join with invite code", ItemKind::Button, "", LobbyView.bConfigured));
		R.push_back(MakeItem("direct", "Direct connect", ItemKind::Button));
		R.push_back(MakeItem("name", "Name", ItemKind::Text, Prefs.PlayerName));
		R.push_back(MakeItem("settings", "Settings", ItemKind::Button));
		R.push_back(MakeItem("quit", "Quit", ItemKind::Button));
		break;
	case Screen::JoinLobby:
		R.push_back(MakeItem("code", "Invite code", ItemKind::Text, JoinCodeText));
		R.push_back(MakeItem("do_join", "Join", ItemKind::Button, "", JoinCodeText.size() == kCodeLength));
		R.push_back(MakeItem("back", "Back", ItemKind::Button));
		break;
	case Screen::DirectConnect:
	{
		R.push_back(MakeItem("address", "Server (host:port)", ItemKind::Text, AddressText));
		R.push_back(MakeItem("direct_map", "Map", ItemKind::Choice, DirectMapName));
		R.push_back(MakeItem("connect", "Connect", ItemKind::Button, "", AddressText.find(':') != std::string::npos));
		R.push_back(MakeItem("back", "Back", ItemKind::Button));
		break;
	}
	case Screen::Lobby:
	{
		const LobbyMember* Me = LobbyView.Me();
		const int Squad = Me != nullptr ? Me->Squad : 0;
		const bool bReady = Me != nullptr && Me->bReady;
		const bool bRounds = LobbyView.Mode != "deathmatch";
		R.push_back(MakeItem("squad", "Squad", ItemKind::Choice, Squad == 0 ? "Squad A" : "Squad B"));
		R.push_back(MakeItem("ready", "Ready", ItemKind::Toggle, bReady ? "Ready" : "Not ready"));
		R.push_back(MakeItem("map", "Map", ItemKind::Choice, LobbyView.Map, bHost));
		R.push_back(MakeItem("mode", "Mode", ItemKind::Choice, LobbyView.Mode, bHost));
		R.push_back(MakeItem("team_size", "Teams", ItemKind::Choice,
			std::to_string(LobbyView.TeamSize) + "v" + std::to_string(LobbyView.TeamSize), bHost));
		if (bRounds)
		{
			R.push_back(MakeItem("rounds_to_win", "Rounds to win", ItemKind::Choice, std::to_string(LobbyView.RoundsToWin), bHost));
			R.push_back(MakeItem("prep_time", "Preparation", ItemKind::Choice, FormatClock(LobbyView.PrepTime), bHost));
		}
		else
		{
			R.push_back(MakeItem("score_limit", "Kill limit", ItemKind::Choice, std::to_string(LobbyView.ScoreLimit), bHost));
		}
		R.push_back(MakeItem("round_time", bRounds ? "Round time" : "Time limit", ItemKind::Choice, FormatClock(LobbyView.RoundTime), bHost));
		R.push_back(MakeItem("friendly_fire", "Friendly fire", ItemKind::Toggle, OnOff(LobbyView.bFriendlyFire), bHost));
		R.push_back(MakeItem("tick_rate", "Server tick rate", ItemKind::Choice, std::to_string(LobbyView.TickRate) + " Hz", bHost));
		R.push_back(MakeItem("start", "Start match", ItemKind::Button, "", bHost));
		R.push_back(MakeItem("settings", "Settings", ItemKind::Button));
		R.push_back(MakeItem("leave", "Leave lobby", ItemKind::Button));
		break;
	}
	case Screen::Settings:
	{
		Item I = MakeItem("sensitivity", "Mouse sensitivity", ItemKind::Slider, FormatFixed(Prefs.MouseSensitivity, 3));
		I.Fraction = FractionOf(Prefs.MouseSensitivity, kSensitivityRange);
		R.push_back(I);
		I = MakeItem("ads", "Aiming sensitivity", ItemKind::Slider, Percent(Prefs.AdsMultiplier));
		I.Fraction = FractionOf(Prefs.AdsMultiplier, kAdsRange);
		R.push_back(I);
		R.push_back(MakeItem("invert_y", "Invert mouse Y", ItemKind::Toggle, OnOff(Prefs.bInvertY)));
		R.push_back(MakeItem("toggle_crouch", "Crouch key", ItemKind::Toggle, Prefs.bToggleCrouch ? "Toggle" : "Hold"));
		I = MakeItem("fov", "Field of view", ItemKind::Slider, std::to_string(static_cast<int>(std::lround(Prefs.FieldOfView))));
		I.Fraction = FractionOf(Prefs.FieldOfView, kFovRange);
		R.push_back(I);
		I = MakeItem("master_volume", "Master volume", ItemKind::Slider, Percent(Prefs.MasterVolume));
		I.Fraction = Prefs.MasterVolume;
		R.push_back(I);
		I = MakeItem("effects_volume", "Effects volume", ItemKind::Slider, Percent(Prefs.EffectsVolume));
		I.Fraction = Prefs.EffectsVolume;
		R.push_back(I);
		R.push_back(MakeItem("net_stats", "Network stats", ItemKind::Toggle, OnOff(Prefs.bShowNetStats)));
		R.push_back(MakeItem("lobby_url", "Lobby service", ItemKind::Text, Prefs.LobbyUrl));
		R.push_back(MakeItem("controls", "Controls", ItemKind::Button));
		R.push_back(MakeItem("back", "Back", ItemKind::Button));
		break;
	}
	case Screen::Controls:
		for (size_t K = 0; K < Prefs.Keys.size(); ++K)
		{
			const bool bThis = bCapturingKey && FocusId == "key." + Prefs.Keys[K].Action;
			Item I = MakeItem("", ActionLabel(Prefs.Keys[K].Action), ItemKind::KeyBind,
				bThis ? std::string("press a key...") : KeyDisplayName(Prefs.Keys[K].Key));
			I.Id = "key." + Prefs.Keys[K].Action;
			R.push_back(I);
		}
		R.push_back(MakeItem("reset_keys", "Reset to defaults", ItemKind::Button));
		R.push_back(MakeItem("back", "Back", ItemKind::Button));
		break;
	case Screen::Pause:
		R.push_back(MakeItem("resume", "Resume", ItemKind::Button));
		R.push_back(MakeItem("settings", "Settings", ItemKind::Button));
		R.push_back(MakeItem("leave_match", LobbyView.bInLobby ? "Leave match (back to lobby)" : "Disconnect", ItemKind::Button));
		R.push_back(MakeItem("quit", "Quit game", ItemKind::Button));
		break;
	}
	// Keep focus on the same item across rebuilds.
	if (!FocusId.empty())
	{
		for (size_t I = 0; I < R.size(); ++I)
		{
			if (R[I].Id == FocusId)
			{
				FocusIndex = static_cast<int>(I);
				return;
			}
		}
	}
	FocusIndex = std::min(FocusIndex, std::max(0, static_cast<int>(R.size()) - 1));
}

void Menu::MoveFocus(int Delta)
{
	const int N = static_cast<int>(CachedItems.size());
	if (N == 0)
	{
		FocusIndex = 0;
		return;
	}
	// Delta 0: make sure the focus sits on an enabled item (search forward).
	const int Dir = Delta < 0 ? -1 : 1;
	int I = Delta == 0 ? FocusIndex : FocusIndex + Dir;
	for (int Tries = 0; Tries < N; ++Tries)
	{
		I = ((I % N) + N) % N;
		if (CachedItems[I].bEnabled)
		{
			FocusIndex = I;
			return;
		}
		I += Dir;
	}
}

void Menu::Emit(ActionKind Kind, std::string Key, std::string Value)
{
	Pending.push_back({Kind, std::move(Key), std::move(Value)});
}

void Menu::ChangedSettings()
{
	Prefs.Sanitize();
	Emit(ActionKind::SettingsChanged);
	Rebuild();
}

std::vector<Action> Menu::TakeActions()
{
	std::vector<Action> R;
	R.swap(Pending);
	return R;
}

void Menu::OnNav(Nav N)
{
	if (bCapturingKey)
	{
		if (N == Nav::Back)
		{
			bCapturingKey = false;
			Rebuild();
		}
		return;
	}
	switch (N)
	{
	case Nav::Up: MoveFocus(-1); break;
	case Nav::Down: MoveFocus(1); break;
	case Nav::Left: Activate(FocusIndex, -1); break;
	case Nav::Right: Activate(FocusIndex, 1); break;
	case Nav::Accept: Activate(FocusIndex, 0); break;
	case Nav::Back:
		if (Current() == Screen::Pause && Stack.size() == 1)
		{
			Pop();
			Emit(ActionKind::Resume);
		}
		else if (Stack.size() > 1)
		{
			Pop();
		}
		break;
	}
}

void Menu::OnChar(char C)
{
	std::string* Text = EditedText();
	if (Text == nullptr || bCapturingKey)
	{
		return;
	}
	const std::string& Id = CachedItems[FocusIndex].Id;
	if (Id == "code")
	{
		if (C >= 'a' && C <= 'z')
		{
			C = static_cast<char>(C - 'a' + 'A');
		}
		if (IsCodeChar(C) && Text->size() < kCodeLength) { Text->push_back(C); }
	}
	else if (Id == "address")
	{
		if (IsAddressChar(C) && Text->size() < kMaxAddress) { Text->push_back(C); }
	}
	else if (Id == "name")
	{
		if (IsNameChar(C) && Text->size() < kMaxName) { Text->push_back(C); Emit(ActionKind::SettingsChanged); }
	}
	else if (Id == "lobby_url")
	{
		if (IsUrlChar(C) && Text->size() < kMaxUrl) { Text->push_back(C); Emit(ActionKind::SettingsChanged); }
	}
	Rebuild();
}

void Menu::OnBackspace()
{
	std::string* Text = EditedText();
	if (Text == nullptr || Text->empty() || bCapturingKey)
	{
		return;
	}
	Text->pop_back();
	const std::string& Id = CachedItems[FocusIndex].Id;
	if (Id == "name" || Id == "lobby_url")
	{
		// An empty name is restored to the default only when saved, so typing a new one works.
		Emit(ActionKind::SettingsChanged);
	}
	Rebuild();
}

void Menu::OnMouseMove(float X, float Y)
{
	if (bCapturingKey)
	{
		return;
	}
	for (size_t I = 0; I < CachedItems.size(); ++I)
	{
		if (CachedItems[I].bEnabled && ItemRect(static_cast<int>(I)).Contains(X, Y))
		{
			FocusIndex = static_cast<int>(I);
			return;
		}
	}
}

void Menu::OnClick(float X, float Y)
{
	if (bCapturingKey)
	{
		return;
	}
	for (size_t I = 0; I < CachedItems.size(); ++I)
	{
		const Rect R = ItemRect(static_cast<int>(I));
		if (!CachedItems[I].bEnabled || !R.Contains(X, Y))
		{
			continue;
		}
		FocusIndex = static_cast<int>(I);
		const Item& It = CachedItems[I];
		if (It.Kind == ItemKind::Slider)
		{
			// The bar fills the right half of the row: clicking sets the value there.
			const float BarX = R.X + R.W * 0.5f;
			const float F = std::min(1.0f, std::max(0.0f, (X - BarX) / (R.W * 0.5f)));
			if (X >= BarX)
			{
				const SettingRange* Range = It.Id == "sensitivity" ? &kSensitivityRange
					: It.Id == "ads"                               ? &kAdsRange
					: It.Id == "fov"                               ? &kFovRange
																   : &kVolumeRange;
				float V = Range->Min + F * (Range->Max - Range->Min);
				V = Range->Min + std::round((V - Range->Min) / Range->Step) * Range->Step;
				V = Clamp(V, *Range, V);
				if (It.Id == "sensitivity") { Prefs.MouseSensitivity = V; }
				else if (It.Id == "ads") { Prefs.AdsMultiplier = V; }
				else if (It.Id == "fov") { Prefs.FieldOfView = V; }
				else if (It.Id == "master_volume") { Prefs.MasterVolume = V; }
				else if (It.Id == "effects_volume") { Prefs.EffectsVolume = V; }
				ChangedSettings();
			}
			return;
		}
		Activate(static_cast<int>(I), 0);
		return;
	}
}

void Menu::OnKeyCaptured(const std::string& KeyName)
{
	if (!bCapturingKey)
	{
		return;
	}
	bCapturingKey = false;
	if (KeyName.empty() || KeyName == "Escape")
	{
		Rebuild();
		return;
	}
	const std::string Action = CachedItems[FocusIndex].Id.substr(4);
	std::string Old;
	for (KeyBinding& K : Prefs.Keys)
	{
		if (K.Action == Action)
		{
			Old = K.Key;
			K.Key = KeyName;
		}
	}
	// A key does one thing: the action that had it takes this action's old key.
	for (KeyBinding& K : Prefs.Keys)
	{
		if (K.Action != Action && K.Key == KeyName)
		{
			K.Key = Old;
		}
	}
	ChangedSettings();
}

void Menu::Activate(int Index, int Direction)
{
	if (Index < 0 || Index >= static_cast<int>(CachedItems.size()) || !CachedItems[Index].bEnabled)
	{
		return;
	}
	const Item It = CachedItems[Index];
	const std::string& Id = It.Id;
	// Choices and toggles: Left / Right step, Accept steps forward.
	const int Dir = Direction == 0 ? 1 : Direction;
	const bool bAccept = Direction == 0;

	switch (It.Kind)
	{
	case ItemKind::Button:
		if (!bAccept)
		{
			return;
		}
		if (Id == "host") { Emit(ActionKind::HostLobby); }
		else if (Id == "join") { Push(Screen::JoinLobby); }
		else if (Id == "direct") { Push(Screen::DirectConnect); }
		else if (Id == "settings") { Push(Screen::Settings); }
		else if (Id == "controls") { Push(Screen::Controls); }
		else if (Id == "back") { Pop(); }
		else if (Id == "quit") { Emit(ActionKind::Quit); }
		else if (Id == "do_join") { Emit(ActionKind::JoinLobby, "", JoinCodeText); }
		else if (Id == "connect")
		{
			Prefs.LastServer = AddressText;
			Emit(ActionKind::DirectConnect, DirectMapName, AddressText);
			ChangedSettings();
		}
		else if (Id == "start") { Emit(ActionKind::StartMatch); }
		else if (Id == "leave") { Emit(ActionKind::LeaveLobby); }
		else if (Id == "resume")
		{
			Pop();
			Emit(ActionKind::Resume);
		}
		else if (Id == "leave_match")
		{
			Stack.clear();
			FocusStack.clear();
			Emit(ActionKind::LeaveMatch);
			Rebuild();
		}
		else if (Id == "reset_keys")
		{
			Prefs.Keys = DefaultBindings();
			ChangedSettings();
		}
		return;
	case ItemKind::Text:
		if (!bAccept)
		{
			return;
		}
		// Enter in a field: submit where there is one obvious action, else next field.
		if (Id == "code" && JoinCodeText.size() == kCodeLength) { Emit(ActionKind::JoinLobby, "", JoinCodeText); }
		else if (Id == "address" && AddressText.find(':') != std::string::npos) { Activate(Index + 2, 0); }
		else
		{
			if (Id == "name" || Id == "lobby_url")
			{
				ChangedSettings();
			}
			MoveFocus(1);
		}
		return;
	case ItemKind::KeyBind:
		if (bAccept)
		{
			bCapturingKey = true;
			Rebuild();
		}
		return;
	case ItemKind::Slider:
	{
		if (bAccept)
		{
			return;
		}
		if (Id == "sensitivity") { Prefs.MouseSensitivity = Step(Prefs.MouseSensitivity, kSensitivityRange, Dir); }
		else if (Id == "ads") { Prefs.AdsMultiplier = Step(Prefs.AdsMultiplier, kAdsRange, Dir); }
		else if (Id == "fov") { Prefs.FieldOfView = Step(Prefs.FieldOfView, kFovRange, Dir); }
		else if (Id == "master_volume") { Prefs.MasterVolume = Step(Prefs.MasterVolume, kVolumeRange, Dir); }
		else if (Id == "effects_volume") { Prefs.EffectsVolume = Step(Prefs.EffectsVolume, kVolumeRange, Dir); }
		ChangedSettings();
		return;
	}
	case ItemKind::Toggle:
		if (Id == "invert_y") { Prefs.bInvertY = !Prefs.bInvertY; ChangedSettings(); }
		else if (Id == "toggle_crouch") { Prefs.bToggleCrouch = !Prefs.bToggleCrouch; ChangedSettings(); }
		else if (Id == "net_stats") { Prefs.bShowNetStats = !Prefs.bShowNetStats; ChangedSettings(); }
		else if (Id == "ready")
		{
			const LobbyMember* Me = LobbyView.Me();
			Emit(ActionKind::SetReady, "", Me != nullptr && Me->bReady ? "false" : "true");
		}
		else if (Id == "friendly_fire")
		{
			Emit(ActionKind::LobbySetting, Id, LobbyView.bFriendlyFire ? "false" : "true");
		}
		return;
	case ItemKind::Choice:
		break;
	}

	// Choices.
	if (Id == "squad")
	{
		const LobbyMember* Me = LobbyView.Me();
		Emit(ActionKind::SetSquad, "", Me != nullptr && Me->Squad == 0 ? "1" : "0");
		return;
	}
	if (Id == "map" || Id == "direct_map")
	{
		std::vector<std::string> Maps = LobbyView.Maps;
		if (Maps.empty())
		{
			Maps.assign(std::begin(kDefaultMaps), std::end(kDefaultMaps));
		}
		const std::string& Cur = Id == "map" ? LobbyView.Map : DirectMapName;
		int At = 0;
		for (size_t I = 0; I < Maps.size(); ++I)
		{
			if (Maps[I] == Cur)
			{
				At = static_cast<int>(I);
			}
		}
		const int Count = static_cast<int>(Maps.size());
		const std::string Next = Maps[static_cast<size_t>(((At + Dir) % Count + Count) % Count)];
		if (Id == "map")
		{
			Emit(ActionKind::LobbySetting, "map", Next);
		}
		else
		{
			DirectMapName = Next;
			Rebuild();
		}
		return;
	}
	if (Id == "mode")
	{
		int At = 0;
		for (int I = 0; I < 3; ++I)
		{
			if (LobbyView.Mode == kModes[I])
			{
				At = I;
			}
		}
		Emit(ActionKind::LobbySetting, "mode", kModes[((At + Dir) % 3 + 3) % 3]);
		return;
	}
	if (Id == "team_size") { Emit(ActionKind::LobbySetting, Id, std::to_string(StepInt(LobbyView.TeamSize, 1, 5, 1, Dir))); }
	else if (Id == "rounds_to_win") { Emit(ActionKind::LobbySetting, Id, std::to_string(StepInt(LobbyView.RoundsToWin, 1, 10, 1, Dir))); }
	else if (Id == "score_limit") { Emit(ActionKind::LobbySetting, Id, std::to_string(StepInt(LobbyView.ScoreLimit, 10, 200, 10, Dir))); }
	else if (Id == "round_time")
	{
		Emit(ActionKind::LobbySetting, Id, std::to_string(StepInt(static_cast<int>(std::lround(LobbyView.RoundTime)), 30, 600, 30, Dir)));
	}
	else if (Id == "prep_time")
	{
		Emit(ActionKind::LobbySetting, Id, std::to_string(StepInt(static_cast<int>(std::lround(LobbyView.PrepTime)), 0, 90, 15, Dir)));
	}
	else if (Id == "tick_rate") { Emit(ActionKind::LobbySetting, Id, LobbyView.TickRate == 64 ? "128" : "64"); }
}

} // namespace ui
} // namespace tfps
