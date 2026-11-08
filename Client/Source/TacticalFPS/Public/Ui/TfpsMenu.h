// Copyright TacticalFPS project.
//
// Engine-independent front-end menu: screens, items, keyboard / mouse
// navigation, text entry, key rebinding, user settings (validation and
// config-file text) and the actions the menu asks the game to perform.
//
// The Unreal client feeds it input and draws `Items()` / `ItemRect()` on the
// Canvas (ATfpsHUD); everything here is unit tested without the engine
// (Client/NetCoreTests/src/MenuTests.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tfps
{
namespace ui
{

/** A rebindable action and the key bound to it (Unreal key name, e.g. "W", "LeftMouseButton"). */
struct KeyBinding
{
	std::string Action;
	std::string Key;
};

/** The default bindings, in display order (names match FTfpsKeyBindings fields). */
std::vector<KeyBinding> DefaultBindings();

/** Per-user settings, saved as `key=value` lines. */
struct UserSettings
{
	std::string PlayerName = "Operator";
	/** Lobby service, e.g. http://127.0.0.1:7700. */
	std::string LobbyUrl = "http://127.0.0.1:7700";
	/** Last direct-connect address (host:port). */
	std::string LastServer = "127.0.0.1:7777";
	/** Degrees per mouse count. */
	float MouseSensitivity = 0.07f;
	float AdsMultiplier = 0.65f;
	bool bInvertY = false;
	bool bToggleCrouch = true;
	/** Horizontal field of view in degrees. */
	float FieldOfView = 90.0f;
	float MasterVolume = 0.8f;
	float EffectsVolume = 1.0f;
	bool bShowNetStats = false;
	std::vector<KeyBinding> Keys = DefaultBindings();

	/** Clamps every value into its valid range and repairs the key list (missing actions get their default). */
	void Sanitize();
	std::string Serialize() const;
	/** Unknown keys and malformed lines are ignored; the result is sanitized. */
	static UserSettings Parse(const std::string& Text);
	const std::string* KeyFor(const std::string& Action) const;

	bool operator==(const UserSettings& O) const;
	bool operator!=(const UserSettings& O) const { return !(*this == O); }
};

/** Valid ranges (shared by Sanitize and the sliders). */
struct SettingRange
{
	float Min, Max, Step;
};
extern const SettingRange kSensitivityRange;
extern const SettingRange kAdsRange;
extern const SettingRange kFovRange;
extern const SettingRange kVolumeRange;

/** What the menu shows about the lobby (copied from the lobby client every frame). */
struct LobbyMember
{
	int Id = 0;
	std::string Name;
	int Squad = 0;
	bool bReady = false;
};

struct LobbyState
{
	bool bConfigured = false;
	bool bInLobby = false;
	std::string Code;
	std::string Status;
	int Host = 0;
	int You = 0;
	std::vector<LobbyMember> Members;
	std::vector<std::string> Maps;
	std::string Map = "harbor";
	std::string Mode = "defuse";
	int TeamSize = 5;
	int RoundsToWin = 4;
	int ScoreLimit = 50;
	int TickRate = 64;
	float RoundTime = 180.0f;
	float PrepTime = 30.0f;
	bool bFriendlyFire = false;
	std::string Error;

	bool IsHost() const { return bInLobby && Host == You; }
	const LobbyMember* Me() const;
};

enum class Screen : uint8_t
{
	None,
	Main,
	JoinLobby,
	DirectConnect,
	Lobby,
	Settings,
	Controls,
	Pause,
};

enum class ItemKind : uint8_t
{
	Button,
	Toggle,
	Choice,
	Slider,
	Text,
	KeyBind,
};

struct Item
{
	std::string Id;
	std::string Label;
	ItemKind Kind = ItemKind::Button;
	bool bEnabled = true;
	/** Current value as shown on the right ("On", "75%", "harbor", "W", text being typed). */
	std::string Value;
	/** Slider position 0..1 (for drawing the bar). */
	float Fraction = 0.0f;
};

enum class ActionKind : uint8_t
{
	HostLobby,
	JoinLobby,       // Value = invite code
	DirectConnect,   // Value = host:port, Key = map
	LobbySetting,    // Key = setting name, Value = new value (lobby API strings)
	SetSquad,        // Value = "0" / "1"
	SetReady,        // Value = "true" / "false"
	StartMatch,
	LeaveLobby,
	Resume,
	LeaveMatch,
	Quit,
	SettingsChanged, // apply and save Settings()
};

struct Action
{
	ActionKind Kind;
	std::string Key;
	std::string Value;
};

enum class Nav : uint8_t
{
	Up,
	Down,
	Left,
	Right,
	Accept,
	Back,
};

struct Rect
{
	float X, Y, W, H;
	bool Contains(float Px, float Py) const { return Px >= X && Px < X + W && Py >= Y && Py < Y + H; }
};

/** Menu layout is in a virtual 1920 x 1080 space; the HUD scales it to the viewport. */
constexpr float kVirtualWidth = 1920.0f;
constexpr float kVirtualHeight = 1080.0f;

class Menu
{
public:
	explicit Menu(UserSettings InSettings = UserSettings());

	/**
	 * Called every frame with the lobby state and whether a match is being
	 * played. Out of a match the menu always shows the front end (main menu,
	 * or the lobby screen while in a lobby); in a match it is closed unless
	 * the pause menu is open.
	 */
	void Sync(const LobbyState& Lobby, bool bInMatch);

	bool IsOpen() const { return Current() != Screen::None; }
	Screen Current() const { return Stack.empty() ? Screen::None : Stack.back(); }
	/** Esc in a match: opens the pause menu, or goes back / closes it. */
	void TogglePause();

	const std::vector<Item>& Items() const { return CachedItems; }
	int Focus() const { return FocusIndex; }
	const char* Title() const;
	/** Screen-space rectangle of item `Index` (virtual coordinates). */
	Rect ItemRect(int Index) const;
	/** Panel that holds the items. */
	Rect PanelRect() const;
	/** True while waiting for a key press to rebind the focused action. */
	bool IsCapturingKey() const { return bCapturingKey; }
	/** True while the focused item takes typed characters. */
	bool IsEditingText() const;

	void OnNav(Nav N);
	/** Printable character typed (ASCII; others are ignored). */
	void OnChar(char C);
	void OnBackspace();
	void OnMouseMove(float X, float Y);
	void OnClick(float X, float Y);
	/** A key was pressed while capturing (Unreal key name). Escape cancels. */
	void OnKeyCaptured(const std::string& KeyName);

	const UserSettings& Settings() const { return Prefs; }
	void SetSettings(const UserSettings& S);

	/** Actions requested since the last call. */
	std::vector<Action> TakeActions();

	/** Text being typed in the join-code / address fields. */
	const std::string& JoinCode() const { return JoinCodeText; }
	const std::string& ServerAddress() const { return AddressText; }
	const std::string& DirectMap() const { return DirectMapName; }

private:
	void Push(Screen S);
	void Pop();
	void Rebuild();
	void Activate(int Index, int Direction);
	void Emit(ActionKind Kind, std::string Key = std::string(), std::string Value = std::string());
	void ChangedSettings();
	std::string* EditedText();
	void MoveFocus(int Delta);

	UserSettings Prefs;
	LobbyState LobbyView;
	bool bInMatch = false;
	std::vector<Screen> Stack;
	/** Focus to restore when the screen above is popped. */
	std::vector<int> FocusStack;
	std::vector<Item> CachedItems;
	int FocusIndex = 0;
	bool bCapturingKey = false;
	std::string JoinCodeText;
	std::string AddressText;
	std::string DirectMapName = "harbor";
	std::vector<Action> Pending;
};

/** Formats seconds as m:ss. */
std::string FormatClock(float Seconds);

} // namespace ui
} // namespace tfps
