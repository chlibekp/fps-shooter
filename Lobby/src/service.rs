//! The lobby service: shared state, game server launch and shutdown.
//!
//! Matches run in-process: `start` calls `tfps_server::start_with_data` with
//! the lobby's settings and a fresh join token, so only lobby members can
//! connect. A monitor task stops a game server once its match has been
//! over for a few seconds (or it has been empty for a minute) and reopens
//! the lobby.

use crate::state::{GameInfo, LobbyError, LobbyView, MatchSettings, State};
use rand::Rng;
use std::collections::HashMap;
use std::net::{IpAddr, SocketAddr};
use std::path::PathBuf;
use std::sync::atomic::Ordering;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tfps_server::config::ServerConfig;
use tfps_server::game::{GameMode, MatchRules};
use tfps_server::ServerHandle;
use tfps_sim::{GadgetSet, MapDef, WeaponSet};
use tracing::info;

#[derive(Clone, Debug)]
pub struct LobbyConfig {
    /// HTTP API address.
    pub bind: SocketAddr,
    /// IP game servers bind to (port 0 = any free port).
    pub game_bind_ip: IpAddr,
    /// Host name or IP players use to reach game servers.
    pub public_host: String,
    pub definitions: PathBuf,
    /// Members that stop polling are removed after this long.
    pub member_timeout: Duration,
    /// After MatchOver the game server keeps running this long (results screen).
    pub match_over_linger: Duration,
    /// A game server nobody is connected to is stopped after this long.
    pub empty_timeout: Duration,
    /// Most concurrent game servers.
    pub max_games: usize,
}

impl Default for LobbyConfig {
    fn default() -> Self {
        Self {
            bind: "0.0.0.0:7700".parse().unwrap(),
            game_bind_ip: "0.0.0.0".parse().unwrap(),
            public_host: "127.0.0.1".into(),
            definitions: PathBuf::from("Shared/Definitions"),
            member_timeout: Duration::from_secs(30),
            match_over_linger: Duration::from_secs(10),
            empty_timeout: Duration::from_secs(60),
            max_games: 16,
        }
    }
}

struct RunningGame {
    handle: ServerHandle,
    started: Instant,
    over_since: Option<Instant>,
    empty_since: Option<Instant>,
}

pub struct Service {
    pub cfg: LobbyConfig,
    pub state: Mutex<State>,
    maps: HashMap<String, MapDef>,
    weapons: WeaponSet,
    gadgets: GadgetSet,
    games: Mutex<HashMap<String, RunningGame>>,
}

impl Service {
    /// Loads the shared definitions (every map in `<definitions>/maps`).
    pub fn new(cfg: LobbyConfig) -> anyhow::Result<Arc<Self>> {
        let mut maps = HashMap::new();
        for e in std::fs::read_dir(cfg.definitions.join("maps"))? {
            let p = e?.path();
            if p.extension().is_some_and(|x| x == "json") {
                let m = MapDef::load(&p)?;
                maps.insert(m.name.clone(), m);
            }
        }
        let weapons = WeaponSet::load(cfg.definitions.join("weapons.json"))?;
        let gadgets = GadgetSet::load(cfg.definitions.join("gadgets.json"))?;
        let mut names: Vec<String> = maps.keys().cloned().collect();
        names.sort();
        info!(maps = ?names, "lobby service loaded definitions");
        Ok(Arc::new(Self {
            state: Mutex::new(State::new(names)),
            cfg,
            maps,
            weapons,
            gadgets,
            games: Mutex::new(HashMap::new()),
        }))
    }

    /// Starts the lobby's match: launches a game server and publishes its
    /// address and join token to the members.
    pub fn start_match(&self, code: &str, token: u64) -> Result<LobbyView, LobbyError> {
        let code = code.to_ascii_uppercase();
        let (settings, _members) = self.state.lock().unwrap().check_start(&code, token)?;
        if self.games.lock().unwrap().len() >= self.cfg.max_games {
            return Err(LobbyError::Full);
        }
        let map = self
            .maps
            .get(&settings.map)
            .cloned()
            .ok_or_else(|| LobbyError::BadRequest("unknown map".into()))?;
        let join_token: u64 = loop {
            let t: u64 = rand::rng().random();
            if t != 0 {
                break t;
            }
        };
        let cfg = server_config(&self.cfg, &settings, join_token);
        let handle =
            tfps_server::start_with_data(cfg, map, self.weapons.clone(), self.gadgets.clone())
                .map_err(|e| {
                    LobbyError::BadRequest(format!("could not start a game server: {e}"))
                })?;
        let address = format!("{}:{}", self.cfg.public_host, handle.local_addr.port());
        info!(%code, %address, map = %settings.map, mode = %settings.mode, team_size = settings.team_size, "match started");
        self.games.lock().unwrap().insert(
            code.clone(),
            RunningGame {
                handle,
                started: Instant::now(),
                over_since: None,
                empty_since: None,
            },
        );
        let mut st = self.state.lock().unwrap();
        st.set_in_game(
            &code,
            GameInfo {
                address,
                join_token: format!("{join_token:016x}"),
            },
        );
        st.view(&code, token, Instant::now())
    }

    /// Periodic housekeeping: expire members, stop finished game servers.
    pub fn tick(&self, now: Instant) {
        let removed = self
            .state
            .lock()
            .unwrap()
            .expire(now, self.cfg.member_timeout);
        let mut to_stop: Vec<(String, ServerHandle)> = Vec::new();
        {
            let mut games = self.games.lock().unwrap();
            let codes: Vec<String> = games.keys().cloned().collect();
            for code in codes {
                let g = games.get_mut(&code).unwrap();
                let stats = &g.handle.stats;
                if stats.match_over.load(Ordering::Relaxed) {
                    g.over_since.get_or_insert(now);
                }
                let players = stats.players.load(Ordering::Relaxed);
                if players == 0 && now.duration_since(g.started) > Duration::from_secs(5) {
                    g.empty_since.get_or_insert(now);
                } else {
                    g.empty_since = None;
                }
                let done = g
                    .over_since
                    .is_some_and(|t| now.duration_since(t) >= self.cfg.match_over_linger)
                    || g.empty_since
                        .is_some_and(|t| now.duration_since(t) >= self.cfg.empty_timeout)
                    || removed.contains(&code)
                    || !g.handle.is_running();
                if done {
                    let g = games.remove(&code).unwrap();
                    to_stop.push((code, g.handle));
                }
            }
        }
        for (code, handle) in to_stop {
            // Joining the simulation thread can take a tick; do it off the lock.
            handle.shutdown();
            self.state.lock().unwrap().set_open(&code);
            info!(%code, "match ended, lobby reopened");
        }
    }

    pub fn running_games(&self) -> usize {
        self.games.lock().unwrap().len()
    }
}

fn server_config(lobby: &LobbyConfig, s: &MatchSettings, join_token: u64) -> ServerConfig {
    let mode = GameMode::parse(&s.mode).unwrap_or(GameMode::Defuse);
    ServerConfig {
        bind: SocketAddr::new(lobby.game_bind_ip, 0),
        tick_rate: s.tick_rate,
        max_players: 2 * s.team_size as u16,
        join_token: Some(join_token),
        stats_interval: Duration::from_secs(60),
        rules: MatchRules {
            mode,
            rounds_to_win: s.rounds_to_win,
            round_time: s.round_time,
            prep_time: s.prep_time,
            score_limit: s.score_limit,
            friendly_fire: s.friendly_fire,
            ..Default::default()
        },
        ..Default::default()
    }
}
