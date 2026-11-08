//! Client for the lobby service's HTTP API (tests, bots, tools).
//!
//! Plain HTTP/1.1 over `std::net::TcpStream`, one request per connection,
//! matching the service's minimal server.

use serde_json::{json, Value};
use std::io::{Read, Write};
use std::net::{SocketAddr, TcpStream};
use std::time::Duration;

#[derive(Debug)]
pub struct LobbyError {
    pub status: u16,
    pub message: String,
}

impl std::fmt::Display for LobbyError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "lobby error {}: {}", self.status, self.message)
    }
}

impl std::error::Error for LobbyError {}

/// One HTTP request; returns (status, JSON body).
pub fn request(
    addr: SocketAddr,
    method: &str,
    path: &str,
    token: Option<&str>,
    body: Option<&Value>,
) -> std::io::Result<(u16, Value)> {
    let mut s = TcpStream::connect_timeout(&addr, Duration::from_secs(3))?;
    s.set_read_timeout(Some(Duration::from_secs(10)))?;
    let body = body.map(|b| b.to_string()).unwrap_or_default();
    let auth = token
        .map(|t| format!("Authorization: Bearer {t}\r\n"))
        .unwrap_or_default();
    let req = format!(
        "{method} {path} HTTP/1.1\r\nHost: {addr}\r\n{auth}Content-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}",
        body.len()
    );
    s.write_all(req.as_bytes())?;
    let mut resp = Vec::new();
    s.read_to_end(&mut resp)?;
    let text = String::from_utf8_lossy(&resp);
    let (head, body) = text.split_once("\r\n\r\n").unwrap_or((&text, ""));
    let status = head
        .split_whitespace()
        .nth(1)
        .and_then(|c| c.parse().ok())
        .unwrap_or(0);
    let value = serde_json::from_str(body).unwrap_or(Value::Null);
    Ok((status, value))
}

/// A member of a lobby: its code, token and the latest view.
#[derive(Debug, Clone)]
pub struct LobbyClient {
    pub addr: SocketAddr,
    pub code: String,
    pub token: String,
    pub lobby: Value,
    /// Maps the service can host (from the latest response).
    pub maps: Vec<String>,
}

fn map_list(v: &Value) -> Option<Vec<String>> {
    v.get("maps")?.as_array().map(|a| {
        a.iter()
            .filter_map(|m| m.as_str().map(String::from))
            .collect()
    })
}

fn ok(status: u16, v: Value) -> Result<Value, LobbyError> {
    if (200..300).contains(&status) {
        Ok(v)
    } else {
        Err(LobbyError {
            status,
            message: v
                .get("error")
                .and_then(|e| e.as_str())
                .unwrap_or("")
                .to_string(),
        })
    }
}

fn io(e: std::io::Error) -> LobbyError {
    LobbyError {
        status: 0,
        message: e.to_string(),
    }
}

impl LobbyClient {
    fn from_response(addr: SocketAddr, v: Value) -> Self {
        let lobby = v["lobby"].clone();
        Self {
            addr,
            code: lobby["code"].as_str().unwrap_or("").to_string(),
            token: v["token"].as_str().unwrap_or("").to_string(),
            maps: map_list(&v).unwrap_or_default(),
            lobby,
        }
    }

    /// Creates a lobby and becomes its host.
    pub fn create(addr: SocketAddr, name: &str) -> Result<Self, LobbyError> {
        let (s, v) = request(
            addr,
            "POST",
            "/lobbies",
            None,
            Some(&json!({ "name": name })),
        )
        .map_err(io)?;
        Ok(Self::from_response(addr, ok(s, v)?))
    }

    /// Joins a lobby by invite code.
    pub fn join(addr: SocketAddr, code: &str, name: &str) -> Result<Self, LobbyError> {
        let (s, v) = request(
            addr,
            "POST",
            "/lobbies/join",
            None,
            Some(&json!({ "code": code, "name": name })),
        )
        .map_err(io)?;
        Ok(Self::from_response(addr, ok(s, v)?))
    }

    fn call(
        &mut self,
        method: &str,
        action: &str,
        body: Option<Value>,
    ) -> Result<&Value, LobbyError> {
        let path = if action.is_empty() {
            format!("/lobbies/{}", self.code)
        } else {
            format!("/lobbies/{}/{action}", self.code)
        };
        let (s, v) =
            request(self.addr, method, &path, Some(&self.token), body.as_ref()).map_err(io)?;
        let v = ok(s, v)?;
        if v.get("lobby").is_some() {
            self.lobby = v["lobby"].clone();
        }
        if let Some(m) = map_list(&v) {
            self.maps = m;
        }
        Ok(&self.lobby)
    }

    /// Refreshes the view (also the heartbeat that keeps the member alive).
    pub fn refresh(&mut self) -> Result<&Value, LobbyError> {
        self.call("GET", "", None)
    }

    pub fn set_settings(&mut self, settings: Value) -> Result<&Value, LobbyError> {
        self.call("POST", "settings", Some(settings))
    }

    pub fn set_squad(&mut self, squad: u8) -> Result<&Value, LobbyError> {
        self.call("POST", "squad", Some(json!({ "squad": squad })))
    }

    pub fn set_ready(&mut self, ready: bool) -> Result<&Value, LobbyError> {
        self.call("POST", "ready", Some(json!({ "ready": ready })))
    }

    pub fn start(&mut self) -> Result<&Value, LobbyError> {
        self.call("POST", "start", None)
    }

    pub fn leave(&mut self) -> Result<(), LobbyError> {
        self.call("POST", "leave", None).map(|_| ())
    }

    /// Game server address and join token once the match runs.
    pub fn game(&self) -> Option<(SocketAddr, u64)> {
        let g = self.lobby.get("game")?;
        let addr = g["address"].as_str()?.parse().ok()?;
        let token = u64::from_str_radix(g["join_token"].as_str()?, 16).ok()?;
        Some((addr, token))
    }
}
