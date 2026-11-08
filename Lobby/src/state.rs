//! Lobby state machine (no I/O): lobbies, invite codes, members, settings,
//! readiness and the checks for starting a match. The HTTP layer
//! (`http.rs`) maps requests onto these operations; launching and stopping
//! game servers is done by `service.rs`.

use rand::Rng;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::time::{Duration, Instant};

/// Invite code alphabet: no 0/O, 1/I/L (read aloud and typed by players).
const CODE_ALPHABET: &[u8] = b"ABCDEFGHJKMNPQRSTUVWXYZ23456789";
pub const CODE_LEN: usize = 6;
pub const MAX_NAME_LEN: usize = 24;

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct MatchSettings {
    pub map: String,
    /// "deathmatch", "elimination" or "defuse".
    pub mode: String,
    /// Players per squad: 1 (1v1) to 5 (5v5).
    pub team_size: u8,
    pub rounds_to_win: u8,
    pub round_time: f32,
    pub prep_time: f32,
    /// Deathmatch kill limit.
    pub score_limit: u8,
    pub friendly_fire: bool,
    /// 64 or 128.
    pub tick_rate: u16,
}

impl Default for MatchSettings {
    fn default() -> Self {
        Self {
            map: "harbor".into(),
            mode: "defuse".into(),
            team_size: 5,
            rounds_to_win: 4,
            round_time: 180.0,
            prep_time: 30.0,
            score_limit: 50,
            friendly_fire: false,
            tick_rate: 64,
        }
    }
}

impl MatchSettings {
    /// Checks every field against what the server accepts.
    pub fn validate(&self, maps: &[String]) -> Result<(), LobbyError> {
        let bad = |m: &str| Err(LobbyError::BadRequest(m.to_string()));
        if !maps.iter().any(|m| m == &self.map) {
            return bad("unknown map");
        }
        if !matches!(self.mode.as_str(), "deathmatch" | "elimination" | "defuse") {
            return bad("mode must be deathmatch, elimination or defuse");
        }
        if !(1..=5).contains(&self.team_size) {
            return bad("team_size must be 1..=5");
        }
        if !(1..=10).contains(&self.rounds_to_win) {
            return bad("rounds_to_win must be 1..=10");
        }
        if !(30.0..=600.0).contains(&self.round_time) || !(0.0..=90.0).contains(&self.prep_time) {
            return bad("round_time 30..=600 s, prep_time 0..=90 s");
        }
        if !(1..=200).contains(&self.score_limit) {
            return bad("score_limit must be 1..=200");
        }
        if !matches!(self.tick_rate, 64 | 128) {
            return bad("tick_rate must be 64 or 128");
        }
        Ok(())
    }
}

#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct Member {
    pub id: u16,
    pub name: String,
    pub squad: u8,
    pub ready: bool,
    #[serde(skip)]
    pub token: u64,
    #[serde(skip)]
    pub last_seen: Option<Instant>,
}

/// Where the lobby's match is being played.
#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct GameInfo {
    /// host:port of the game server.
    pub address: String,
    /// Join token to send in ChallengeResponse (hex).
    pub join_token: String,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Status {
    Open,
    InGame,
}

#[derive(Clone, Debug)]
pub struct Lobby {
    pub code: String,
    pub host: u16,
    pub settings: MatchSettings,
    pub members: Vec<Member>,
    pub status: Status,
    pub game: Option<GameInfo>,
    next_id: u16,
}

/// What a member sees (no tokens, game details only for members).
#[derive(Clone, Debug, Serialize)]
pub struct LobbyView {
    pub code: String,
    pub host: u16,
    pub you: u16,
    pub settings: MatchSettings,
    pub members: Vec<Member>,
    pub status: Status,
    pub game: Option<GameInfo>,
}

#[derive(Debug, PartialEq)]
pub enum LobbyError {
    NotFound,
    Unauthorized,
    Full,
    BadRequest(String),
    TooManyAttempts,
}

impl LobbyError {
    pub fn status(&self) -> u16 {
        match self {
            LobbyError::NotFound => 404,
            LobbyError::Unauthorized => 403,
            LobbyError::Full => 409,
            LobbyError::BadRequest(_) => 400,
            LobbyError::TooManyAttempts => 429,
        }
    }

    pub fn message(&self) -> String {
        match self {
            LobbyError::NotFound => "lobby not found".into(),
            LobbyError::Unauthorized => "not allowed".into(),
            LobbyError::Full => "lobby is full".into(),
            LobbyError::BadRequest(m) => m.clone(),
            LobbyError::TooManyAttempts => "too many attempts, wait a minute".into(),
        }
    }
}

/// Upper bound on lobbies per service (memory and invite-code space).
pub const MAX_LOBBIES: usize = 1000;
/// Failed joins allowed per client address and minute (code guessing).
pub const MAX_FAILED_JOINS: u32 = 10;

#[derive(Default)]
pub struct State {
    pub lobbies: HashMap<String, Lobby>,
    pub maps: Vec<String>,
    failed_joins: HashMap<std::net::IpAddr, (Instant, u32)>,
}

pub fn clean_name(name: &str) -> Result<String, LobbyError> {
    let n: String = name.trim().chars().filter(|c| !c.is_control()).collect();
    if n.is_empty() || n.len() > MAX_NAME_LEN {
        return Err(LobbyError::BadRequest(format!(
            "name must be 1..={MAX_NAME_LEN} bytes"
        )));
    }
    Ok(n)
}

fn random_token() -> u64 {
    loop {
        let t: u64 = rand::rng().random();
        if t != 0 {
            return t;
        }
    }
}

impl Lobby {
    fn view(&self, you: u16) -> LobbyView {
        LobbyView {
            code: self.code.clone(),
            host: self.host,
            you,
            settings: self.settings.clone(),
            members: self.members.clone(),
            status: self.status.clone(),
            game: self.game.clone(),
        }
    }

    fn member(&self, token: u64) -> Result<&Member, LobbyError> {
        self.members
            .iter()
            .find(|m| m.token == token)
            .ok_or(LobbyError::Unauthorized)
    }

    fn capacity(&self) -> usize {
        2 * self.settings.team_size as usize
    }

    fn squad_size(&self, squad: u8) -> usize {
        self.members.iter().filter(|m| m.squad == squad).count()
    }

    fn add_member(&mut self, name: String, now: Instant) -> Result<(u16, u64), LobbyError> {
        if self.members.len() >= self.capacity() {
            return Err(LobbyError::Full);
        }
        let squad = if self.squad_size(1) < self.squad_size(0) {
            1
        } else {
            0
        };
        let id = self.next_id;
        self.next_id = self.next_id.wrapping_add(1).max(1);
        let token = random_token();
        self.members.push(Member {
            id,
            name,
            squad,
            ready: false,
            token,
            last_seen: Some(now),
        });
        Ok((id, token))
    }
}

impl State {
    pub fn new(maps: Vec<String>) -> Self {
        Self {
            maps,
            ..Default::default()
        }
    }

    fn new_code(&self) -> String {
        let mut rng = rand::rng();
        loop {
            let code: String = (0..CODE_LEN)
                .map(|_| CODE_ALPHABET[rng.random_range(0..CODE_ALPHABET.len())] as char)
                .collect();
            if !self.lobbies.contains_key(&code) {
                return code;
            }
        }
    }

    /// Creates a lobby hosted by `name`. Returns (view, token).
    pub fn create(&mut self, name: &str, now: Instant) -> Result<(LobbyView, u64), LobbyError> {
        let name = clean_name(name)?;
        if self.lobbies.len() >= MAX_LOBBIES {
            return Err(LobbyError::Full);
        }
        let code = self.new_code();
        let mut settings = MatchSettings::default();
        if !self.maps.iter().any(|m| m == &settings.map) {
            settings.map = self.maps.first().cloned().unwrap_or_default();
        }
        let mut lobby = Lobby {
            code: code.clone(),
            host: 1,
            settings,
            members: Vec::new(),
            status: Status::Open,
            game: None,
            next_id: 1,
        };
        let (id, token) = lobby.add_member(name, now)?;
        lobby.host = id;
        let view = lobby.view(id);
        self.lobbies.insert(code, lobby);
        Ok((view, token))
    }

    /// Joins a lobby by invite code; failed attempts are rate-limited per address.
    pub fn join(
        &mut self,
        code: &str,
        name: &str,
        from: std::net::IpAddr,
        now: Instant,
    ) -> Result<(LobbyView, u64), LobbyError> {
        let name = clean_name(name)?;
        if let Some((since, n)) = self.failed_joins.get(&from) {
            if now.duration_since(*since) < Duration::from_secs(60) && *n >= MAX_FAILED_JOINS {
                return Err(LobbyError::TooManyAttempts);
            }
        }
        let code = code.trim().to_ascii_uppercase();
        let Some(lobby) = self.lobbies.get_mut(&code) else {
            let e = self.failed_joins.entry(from).or_insert((now, 0));
            if now.duration_since(e.0) >= Duration::from_secs(60) {
                *e = (now, 0);
            }
            e.1 += 1;
            return Err(LobbyError::NotFound);
        };
        if lobby.status != Status::Open {
            return Err(LobbyError::BadRequest("match already in progress".into()));
        }
        let (id, token) = lobby.add_member(name, now)?;
        Ok((lobby.view(id), token))
    }

    fn lobby_mut(&mut self, code: &str) -> Result<&mut Lobby, LobbyError> {
        self.lobbies
            .get_mut(&code.to_ascii_uppercase())
            .ok_or(LobbyError::NotFound)
    }

    /// A member's view (also a heartbeat).
    pub fn view(&mut self, code: &str, token: u64, now: Instant) -> Result<LobbyView, LobbyError> {
        let lobby = self.lobby_mut(code)?;
        let id = lobby.member(token)?.id;
        if let Some(m) = lobby.members.iter_mut().find(|m| m.id == id) {
            m.last_seen = Some(now);
        }
        Ok(lobby.view(id))
    }

    pub fn update_settings(
        &mut self,
        code: &str,
        token: u64,
        settings: MatchSettings,
    ) -> Result<LobbyView, LobbyError> {
        let maps = self.maps.clone();
        let lobby = self.lobby_mut(code)?;
        let me = lobby.member(token)?.id;
        if me != lobby.host {
            return Err(LobbyError::Unauthorized);
        }
        if lobby.status != Status::Open {
            return Err(LobbyError::BadRequest("match in progress".into()));
        }
        settings.validate(&maps)?;
        if lobby.members.len() > 2 * settings.team_size as usize {
            return Err(LobbyError::BadRequest(
                "more members than the new team size allows".into(),
            ));
        }
        lobby.settings = settings;
        // Squads may now be over capacity: move extra members to the other squad.
        let cap = lobby.settings.team_size as usize;
        for squad in 0..2u8 {
            while lobby.squad_size(squad) > cap {
                if let Some(m) = lobby.members.iter_mut().rev().find(|m| m.squad == squad) {
                    m.squad ^= 1;
                }
            }
        }
        for m in lobby.members.iter_mut() {
            m.ready = false; // settings changed: everyone confirms again
        }
        Ok(lobby.view(me))
    }

    pub fn set_squad(
        &mut self,
        code: &str,
        token: u64,
        squad: u8,
    ) -> Result<LobbyView, LobbyError> {
        let lobby = self.lobby_mut(code)?;
        let me = lobby.member(token)?.id;
        if squad > 1 {
            return Err(LobbyError::BadRequest("squad must be 0 or 1".into()));
        }
        let current = lobby.member(token)?.squad;
        if current != squad && lobby.squad_size(squad) >= lobby.settings.team_size as usize {
            return Err(LobbyError::Full);
        }
        if let Some(m) = lobby.members.iter_mut().find(|m| m.id == me) {
            m.squad = squad;
            m.ready = false;
        }
        Ok(lobby.view(me))
    }

    pub fn set_ready(
        &mut self,
        code: &str,
        token: u64,
        ready: bool,
    ) -> Result<LobbyView, LobbyError> {
        let lobby = self.lobby_mut(code)?;
        let me = lobby.member(token)?.id;
        if let Some(m) = lobby.members.iter_mut().find(|m| m.id == me) {
            m.ready = ready;
        }
        Ok(lobby.view(me))
    }

    /// Removes a member; the lobby closes when empty, the host passes on.
    pub fn leave(&mut self, code: &str, token: u64) -> Result<(), LobbyError> {
        let lobby = self.lobby_mut(code)?;
        let me = lobby.member(token)?.id;
        lobby.members.retain(|m| m.id != me);
        if lobby.host == me {
            if let Some(m) = lobby.members.first() {
                lobby.host = m.id;
            }
        }
        let code = lobby.code.clone();
        if lobby.members.is_empty() {
            self.lobbies.remove(&code);
        }
        Ok(())
    }

    /// Checks that the host may start the match now.
    pub fn check_start(
        &mut self,
        code: &str,
        token: u64,
    ) -> Result<(MatchSettings, usize), LobbyError> {
        let lobby = self.lobby_mut(code)?;
        let me = lobby.member(token)?.id;
        if me != lobby.host {
            return Err(LobbyError::Unauthorized);
        }
        if lobby.status != Status::Open {
            return Err(LobbyError::BadRequest("match already in progress".into()));
        }
        if let Some(m) = lobby
            .members
            .iter()
            .find(|m| m.id != lobby.host && !m.ready)
        {
            return Err(LobbyError::BadRequest(format!("{} is not ready", m.name)));
        }
        let (a, b) = (lobby.squad_size(0), lobby.squad_size(1));
        if a == 0 || b == 0 {
            return Err(LobbyError::BadRequest(
                "both squads need at least one player".into(),
            ));
        }
        Ok((lobby.settings.clone(), lobby.members.len()))
    }

    pub fn set_in_game(&mut self, code: &str, game: GameInfo) {
        if let Some(l) = self.lobbies.get_mut(code) {
            l.status = Status::InGame;
            l.game = Some(game);
        }
    }

    pub fn set_open(&mut self, code: &str) {
        if let Some(l) = self.lobbies.get_mut(code) {
            l.status = Status::Open;
            l.game = None;
            for m in l.members.iter_mut() {
                m.ready = false;
            }
        }
    }

    /// Drops members that stopped polling (while the lobby is open) and empty lobbies.
    /// Returns the codes of removed lobbies.
    pub fn expire(&mut self, now: Instant, timeout: Duration) -> Vec<String> {
        let mut removed = Vec::new();
        for (code, l) in self.lobbies.iter_mut() {
            if l.status == Status::Open {
                l.members
                    .retain(|m| m.last_seen.is_some_and(|t| now.duration_since(t) < timeout));
                if !l.members.iter().any(|m| m.id == l.host) {
                    if let Some(m) = l.members.first() {
                        l.host = m.id;
                    }
                }
            }
            if l.members.is_empty() {
                removed.push(code.clone());
            }
        }
        for c in &removed {
            self.lobbies.remove(c);
        }
        self.failed_joins
            .retain(|_, (since, _)| now.duration_since(*since) < Duration::from_secs(60));
        removed
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::net::{IpAddr, Ipv4Addr};

    const IP: IpAddr = IpAddr::V4(Ipv4Addr::LOCALHOST);

    fn state() -> State {
        State::new(vec!["harbor".into(), "villa".into()])
    }

    #[test]
    fn host_creates_friends_join_by_code_and_squads_balance() {
        let mut s = state();
        let now = Instant::now();
        let (v, host) = s.create("Host", now).unwrap();
        assert_eq!(v.code.len(), CODE_LEN);
        assert!(v.code.bytes().all(|c| CODE_ALPHABET.contains(&c)));
        let lower = v.code.to_lowercase();
        let (v2, _) = s.join(&lower, " Friend ", IP, now).unwrap();
        assert_eq!(v2.members.len(), 2);
        assert_eq!(v2.members[1].name, "Friend");
        assert_eq!((v2.members[0].squad, v2.members[1].squad), (0, 1));
        // Tokens are never part of a view.
        let json = serde_json::to_string(&s.view(&v.code, host, now).unwrap()).unwrap();
        assert!(!json.contains("token\":"), "{json}");
    }

    #[test]
    fn only_the_host_changes_settings_and_starts() {
        let mut s = state();
        let now = Instant::now();
        let (v, host) = s.create("Host", now).unwrap();
        let (_, guest) = s.join(&v.code, "Guest", IP, now).unwrap();
        let one_v_one = MatchSettings {
            map: "villa".into(),
            mode: "elimination".into(),
            team_size: 1,
            ..Default::default()
        };
        assert_eq!(
            s.update_settings(&v.code, guest, one_v_one.clone())
                .unwrap_err(),
            LobbyError::Unauthorized
        );
        assert!(s
            .update_settings(
                &v.code,
                host,
                MatchSettings {
                    map: "moon".into(),
                    ..one_v_one.clone()
                }
            )
            .is_err());
        s.update_settings(&v.code, host, one_v_one).unwrap();
        // 1v1: a third player does not fit.
        assert_eq!(
            s.join(&v.code, "Third", IP, now).unwrap_err(),
            LobbyError::Full
        );
        assert_eq!(
            s.check_start(&v.code, guest).unwrap_err(),
            LobbyError::Unauthorized
        );
        assert!(s.check_start(&v.code, host).is_err(), "guest not ready");
        s.set_ready(&v.code, guest, true).unwrap();
        let (settings, n) = s.check_start(&v.code, host).unwrap();
        assert_eq!((settings.team_size, n), (1, 2));
    }

    #[test]
    fn squad_changes_respect_team_size() {
        let mut s = state();
        let now = Instant::now();
        let (v, host) = s.create("Host", now).unwrap();
        s.update_settings(
            &v.code,
            host,
            MatchSettings {
                team_size: 1,
                ..Default::default()
            },
        )
        .unwrap();
        let (_, guest) = s.join(&v.code, "Guest", IP, now).unwrap();
        assert_eq!(
            s.set_squad(&v.code, guest, 0).unwrap_err(),
            LobbyError::Full
        );
        assert!(s.check_start(&v.code, host).is_err(), "guest not ready yet");
    }

    #[test]
    fn guessing_codes_is_rate_limited() {
        let mut s = state();
        let now = Instant::now();
        for _ in 0..MAX_FAILED_JOINS {
            assert_eq!(
                s.join("ZZZZZZ", "Guess", IP, now).unwrap_err(),
                LobbyError::NotFound
            );
        }
        let (v, _) = s.create("Host", now).unwrap();
        assert_eq!(
            s.join(&v.code, "Guess", IP, now).unwrap_err(),
            LobbyError::TooManyAttempts
        );
        // A minute later it works again.
        s.expire(now + Duration::from_secs(61), Duration::from_secs(3600));
        assert!(s
            .join(&v.code, "Guess", IP, now + Duration::from_secs(61))
            .is_ok());
    }

    #[test]
    fn silent_members_expire_and_the_host_passes_on() {
        let mut s = state();
        let t0 = Instant::now();
        let (v, _host) = s.create("Host", t0).unwrap();
        let (_, guest) = s.join(&v.code, "Guest", IP, t0).unwrap();
        let t1 = t0 + Duration::from_secs(20);
        s.view(&v.code, guest, t1).unwrap();
        s.expire(t0 + Duration::from_secs(31), Duration::from_secs(30));
        let view = s.view(&v.code, guest, t1).unwrap();
        assert_eq!(view.members.len(), 1);
        assert_eq!(view.host, view.you, "the guest is the new host");
        s.leave(&v.code, guest).unwrap();
        assert!(s.lobbies.is_empty());
    }
}
