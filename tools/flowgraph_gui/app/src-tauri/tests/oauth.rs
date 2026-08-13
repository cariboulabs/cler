use std::path::PathBuf;
use std::sync::atomic::{AtomicUsize, Ordering};

use cler_flowgraph_gui::ai_agent::{self, Auth};
use cler_flowgraph_gui::oauth::{self, Tokens};

static COUNTER: AtomicUsize = AtomicUsize::new(0);

fn temp_dir(name: &str) -> PathBuf {
    let unique = COUNTER.fetch_add(1, Ordering::Relaxed);
    let dir = std::env::temp_dir().join(format!(
        "cler-gui-oauth-{}-{name}-{unique}",
        std::process::id()
    ));
    std::fs::create_dir_all(&dir).expect("temp directory");
    dir
}

fn tokens(access: &str, refresh: &str, expires_unix_ms: u64) -> Tokens {
    Tokens {
        access: access.to_string(),
        refresh: refresh.to_string(),
        expires_unix_ms,
    }
}

#[test]
fn pkce_yields_a_43_char_urlsafe_verifier_and_a_matching_challenge() {
    let (verifier, challenge) = oauth::pkce().expect("pkce");

    assert_eq!(verifier.len(), 43, "{verifier}");
    assert!(
        verifier
            .chars()
            .all(|ch| ch.is_ascii_alphanumeric() || ch == '-' || ch == '_'),
        "{verifier}"
    );
    assert_eq!(challenge, oauth::challenge_of(&verifier));
    assert_eq!(
        oauth::challenge_of("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
        "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
        "the RFC 7636 vector must hold"
    );

    let url = oauth::authorize_url(&challenge, &verifier);
    assert!(url.starts_with("https://claude.ai/oauth/authorize?code=true&"));
    assert!(url.contains(&format!("client_id={}", oauth::CLIENT_ID)));
    assert!(url.contains("code_challenge_method=S256"));
    assert!(url.contains(&format!("state={verifier}")));
    assert!(url.contains("redirect_uri=http%3A%2F%2Flocalhost%3A53692%2Fcallback"));
    assert!(url.contains("scope=org%3Acreate_api_key%20user%3Aprofile"));
}

#[test]
fn tokens_round_trip_and_lock_down() {
    let dir = temp_dir("roundtrip");
    assert_eq!(oauth::load(&dir), None);

    let wanted = tokens("acc-1", "ref-1", 1_999_999_999_000);
    oauth::store(&dir, &wanted).expect("store");
    assert_eq!(oauth::load(&dir), Some(wanted));

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mode = std::fs::metadata(oauth::token_path(&dir))
            .expect("token file")
            .permissions()
            .mode();
        assert_eq!(mode & 0o777, 0o600);
    }
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn a_stale_token_refreshes_and_the_rotated_refresh_is_persisted() {
    let dir = temp_dir("stale");
    let now = 1_000_000_000_000u64;
    oauth::store(&dir, &tokens("old-access", "old-refresh", now + 60_000)).expect("store stale");

    let mut asked_with = None;
    let access = oauth::access_via(&dir, now, |refresh| {
        asked_with = Some(refresh.to_string());
        Ok(tokens("new-access", "rotated-refresh", now + 3_600_000))
    })
    .expect("refresh path");

    assert_eq!(access, "new-access");
    assert_eq!(asked_with.as_deref(), Some("old-refresh"));
    assert_eq!(
        oauth::load(&dir),
        Some(tokens("new-access", "rotated-refresh", now + 3_600_000)),
        "the rotated refresh token must be persisted"
    );
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn a_fresh_token_is_used_without_refreshing() {
    let dir = temp_dir("fresh");
    let now = 1_000_000_000_000u64;
    oauth::store(&dir, &tokens("live-access", "live-refresh", now + 3_600_000)).expect("store");

    let access = oauth::access_via(&dir, now, |_| {
        panic!("a fresh token must not be refreshed");
    })
    .expect("fresh path");

    assert_eq!(access, "live-access");
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn without_any_tokens_access_says_not_signed_in() {
    let dir = temp_dir("absent");
    let refusal = oauth::access_via(&dir, 0, |_| unreachable!()).expect_err("no file");
    assert!(refusal.contains("not signed in"), "{refusal}");
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn status_names_the_auth_method_and_the_key_file_outranks_oauth() {
    let dir = temp_dir("status");
    std::env::remove_var(ai_agent::ANTHROPIC.key_env);

    let far = oauth::now_ms() + 3_600_000;
    oauth::store(&dir, &tokens("oauth-access", "oauth-refresh", far)).expect("store");
    let status = ai_agent::status(&dir);
    assert!(status.available);
    assert_eq!(status.method.as_deref(), Some("oauth"));
    assert_eq!(
        ai_agent::locate(None, &dir),
        Ok(Auth::OAuth("oauth-access".to_string()))
    );

    ai_agent::store_key("sk-ant-status-test", &dir).expect("key stores");
    let status = ai_agent::status(&dir);
    assert_eq!(status.method.as_deref(), Some("api_key"));
    assert_eq!(
        ai_agent::locate(None, &dir),
        Ok(Auth::ApiKey("sk-ant-status-test".to_string()))
    );

    std::fs::remove_file(ai_agent::key_path(&dir)).expect("drop key");
    let status = oauth::logout(&dir);
    assert!(!status.available);
    assert_eq!(status.method, None);
    assert!(!oauth::token_path(&dir).exists());
    let reason = status.reason.expect("a reason");
    assert!(reason.contains("sign in with Claude"), "{reason}");
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn an_oauth_request_opens_with_the_claude_code_preface() {
    let body: serde_json::Value =
        serde_json::from_str(&ai_agent::request("<graph_model/>", "why?", &[], false, true))
            .unwrap();

    assert_eq!(body["system"][0]["text"], ai_agent::OAUTH_PREFACE);
    assert_eq!(body["system"][2]["text"], "<graph_model/>");
    assert_eq!(
        body["system"][1]["cache_control"]["type"], "ephemeral",
        "the existing system blocks follow unchanged"
    );

    let keyed: serde_json::Value =
        serde_json::from_str(&ai_agent::request("<graph_model/>", "why?", &[], false, false))
            .unwrap();
    assert_ne!(keyed["system"][0]["text"], ai_agent::OAUTH_PREFACE);
    assert_eq!(keyed["system"].as_array().unwrap().len(), 2);
}

#[test]
fn callback_queries_parse_into_code_and_state_or_a_refusal() {
    assert_eq!(
        oauth::parse_callback("code=abc123&state=ver456"),
        Ok(("abc123".to_string(), "ver456".to_string()))
    );
    assert_eq!(
        oauth::parse_callback("state=ver456&code=a%2Bb&extra=1"),
        Ok(("a+b".to_string(), "ver456".to_string()))
    );

    let refused = oauth::parse_callback("error=access_denied").expect_err("error param");
    assert!(refused.contains("access_denied"), "{refused}");

    assert!(oauth::parse_callback("code=abc123").is_err(), "no state");
    assert!(oauth::parse_callback("state=ver456").is_err(), "no code");
    assert!(oauth::parse_callback("").is_err());
}

#[test]
fn manual_input_accepts_a_url_a_code_hash_state_or_a_bare_code() {
    assert_eq!(
        oauth::parse_manual("http://localhost:53692/callback?code=abc&state=ver"),
        Ok(("abc".to_string(), Some("ver".to_string())))
    );
    assert_eq!(
        oauth::parse_manual("  abc#ver  "),
        Ok(("abc".to_string(), Some("ver".to_string())))
    );
    assert_eq!(oauth::parse_manual("abc"), Ok(("abc".to_string(), None)));
    assert!(oauth::parse_manual("   ").is_err());
    assert!(oauth::parse_manual("#ver").is_err());
    assert!(
        oauth::parse_manual("https://claude.ai/cb?error=access_denied").is_err(),
        "a pasted error url is refused"
    );
}
