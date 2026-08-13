use std::path::PathBuf;
use std::sync::atomic::{AtomicUsize, Ordering};

use cler_flowgraph_gui::ai_agent::{self, Auth};
use cler_flowgraph_gui::oauth::{self, Tokens, ANTHROPIC_FLOW, CODEX_FLOW};

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

    let url = oauth::authorize_url(&ANTHROPIC_FLOW, &challenge, &verifier);
    assert_eq!(
        url,
        format!(
            "https://claude.ai/oauth/authorize?code=true&client_id=9d1c250a-e61b-44d9-88ed-5944d1962f5e&response_type=code&redirect_uri=http%3A%2F%2Flocalhost%3A53692%2Fcallback&scope=org%3Acreate_api_key%20user%3Aprofile%20user%3Ainference%20user%3Asessions%3Aclaude_code%20user%3Amcp_servers%20user%3Afile_upload&code_challenge={challenge}&code_challenge_method=S256&state={verifier}"
        ),
        "the Claude authorize url must not move"
    );
}

#[test]
fn the_codex_flow_hides_the_verifier_and_carries_its_own_params() {
    let url = oauth::authorize_url(&CODEX_FLOW, "chal-1", "state-1");
    assert!(url.starts_with("https://auth.openai.com/oauth/authorize?"), "{url}");
    assert!(url.contains("client_id=app_EMoamEEZ73f0CkXaXp7hrann"), "{url}");
    assert!(url.contains("code_challenge=chal-1&code_challenge_method=S256"), "{url}");
    assert!(url.contains("state=state-1"), "{url}");
    assert!(url.contains("id_token_add_organizations=true"), "{url}");
    assert!(url.contains("codex_cli_simplified_flow=true"), "{url}");
    assert!(url.contains("originator=cler"), "{url}");
    assert!(url.contains("scope=openid%20profile%20email%20offline_access"), "{url}");

    let body = oauth::exchange_body(&CODEX_FLOW, "code-1", "state-1", "verifier-1");
    assert_eq!(
        body,
        "grant_type=authorization_code&client_id=app_EMoamEEZ73f0CkXaXp7hrann&code=code-1&redirect_uri=http%3A%2F%2Flocalhost%3A1455%2Fauth%2Fcallback&code_verifier=verifier-1"
    );
    assert!(!body.contains("state"), "the verifier travels, the state does not");

    let claude = oauth::exchange_body(&ANTHROPIC_FLOW, "code-1", "ver-1", "ver-1");
    let claude: serde_json::Value = serde_json::from_str(&claude).expect("json body");
    assert_eq!(claude["state"], "ver-1");
    assert_eq!(claude["code_verifier"], "ver-1");
}

#[test]
fn a_callback_with_a_foreign_state_never_reaches_the_token_endpoint() {
    let dir = temp_dir("state");
    let refusal = oauth::complete(&CODEX_FLOW, &dir, "code-1", "elsewhere", "ours", "verifier-1")
        .expect_err("a mismatched state must be refused");
    assert!(refusal.contains("does not match"), "{refusal}");
    assert!(!oauth::token_path(&CODEX_FLOW, &dir).exists());
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn the_two_flows_keep_separate_token_files() {
    let dir = temp_dir("files");
    assert_ne!(
        oauth::token_path(&ANTHROPIC_FLOW, &dir),
        oauth::token_path(&CODEX_FLOW, &dir)
    );
    assert!(oauth::token_path(&CODEX_FLOW, &dir).ends_with("openai-codex-oauth.json"));
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn a_codex_answer_without_a_rotated_refresh_token_is_refused() {
    let whole = serde_json::json!({
        "access_token": "acc",
        "refresh_token": "ref",
        "expires_in": 3600
    });
    assert_eq!(
        oauth::tokens_of(&CODEX_FLOW, &whole).map(|got| got.refresh),
        Ok("ref".to_string())
    );

    let lacking = serde_json::json!({ "access_token": "acc", "expires_in": 3600 });
    let refusal = oauth::tokens_of(&CODEX_FLOW, &lacking).expect_err("must refuse");
    assert!(refusal.contains("sign in again"), "{refusal}");
    assert!(!refusal.contains("expires_in"), "{refusal}");
    // Claude's endpoint answers this way on purpose: the old refresh stands.
    assert!(oauth::tokens_of(&ANTHROPIC_FLOW, &lacking).is_ok());
}

// The rotated token is the only usable one left, so it must be kept even when
// the answer says nothing about how long the access token lasts.
#[test]
fn a_codex_answer_without_expires_in_still_keeps_the_rotated_refresh_token() {
    let dated = serde_json::json!({ "access_token": "acc", "refresh_token": "new" });
    let kept = oauth::tokens_of(&CODEX_FLOW, &dated).expect("the rotation is worth keeping");

    assert_eq!(kept.refresh, "new");
    assert!(!oauth::fresh(&kept, oauth::now_ms()), "reads as stale");
}

#[test]
fn tokens_round_trip_and_lock_down() {
    let dir = temp_dir("roundtrip");
    assert_eq!(oauth::load(&ANTHROPIC_FLOW, &dir), None);

    let wanted = tokens("acc-1", "ref-1", 1_999_999_999_000);
    oauth::store(&ANTHROPIC_FLOW, &dir, &wanted).expect("store");
    assert_eq!(oauth::load(&ANTHROPIC_FLOW, &dir), Some(wanted));

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mode = std::fs::metadata(oauth::token_path(&ANTHROPIC_FLOW, &dir))
            .expect("token file")
            .permissions()
            .mode();
        assert_eq!(mode & 0o777, 0o600);
    }
    let leftovers: Vec<_> = std::fs::read_dir(&dir)
        .expect("read dir")
        .filter_map(|entry| entry.ok().map(|entry| entry.file_name()))
        .filter(|name| name.to_string_lossy().ends_with(".tmp"))
        .collect();
    assert!(leftovers.is_empty(), "{leftovers:?}");
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn a_stale_token_refreshes_and_the_rotated_refresh_is_persisted() {
    let dir = temp_dir("stale");
    let now = 1_000_000_000_000u64;
    oauth::store(&ANTHROPIC_FLOW, &dir, &tokens("old-access", "old-refresh", now + 60_000)).expect("store stale");

    let mut asked_with = None;
    let access = oauth::access_via(&ANTHROPIC_FLOW, &dir, now, |refresh| {
        asked_with = Some(refresh.to_string());
        Ok(tokens("new-access", "rotated-refresh", now + 3_600_000))
    })
    .expect("refresh path");

    assert_eq!(access, "new-access");
    assert_eq!(asked_with.as_deref(), Some("old-refresh"));
    assert_eq!(
        oauth::load(&ANTHROPIC_FLOW, &dir),
        Some(tokens("new-access", "rotated-refresh", now + 3_600_000)),
        "the rotated refresh token must be persisted"
    );
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn racing_threads_renew_once_and_share_the_new_access_token() {
    let dir = temp_dir("race");
    let now = 1_000_000_000_000u64;
    oauth::store(&ANTHROPIC_FLOW, &dir, &tokens("old-access", "old-refresh", now + 60_000)).expect("store stale");

    let renewals = AtomicUsize::new(0);
    let (entered, started) = std::sync::mpsc::channel();
    let (first, second) = std::thread::scope(|scope| {
        let one = scope.spawn(|| {
            oauth::access_via(&ANTHROPIC_FLOW, &dir, now, |_| {
                renewals.fetch_add(1, Ordering::SeqCst);
                entered.send(()).ok();
                std::thread::sleep(std::time::Duration::from_millis(50));
                Ok(tokens("new-access", "rotated-refresh", now + 3_600_000))
            })
        });
        started.recv().expect("the first renew starts");
        let two = scope.spawn(|| {
            oauth::access_via(&ANTHROPIC_FLOW, &dir, now, |_| {
                renewals.fetch_add(1, Ordering::SeqCst);
                Ok(tokens("other-access", "other-refresh", now + 3_600_000))
            })
        });
        (one.join().expect("thread one"), two.join().expect("thread two"))
    });

    assert_eq!(renewals.load(Ordering::SeqCst), 1, "a single-use refresh may be spent once");
    assert_eq!(first.as_deref(), Ok("new-access"));
    assert_eq!(second.as_deref(), Ok("new-access"));
    assert_eq!(
        oauth::load(&ANTHROPIC_FLOW, &dir),
        Some(tokens("new-access", "rotated-refresh", now + 3_600_000))
    );
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn a_failed_store_or_renew_leaves_the_previous_tokens_complete() {
    let dir = temp_dir("survives");
    let now = 1_000_000_000_000u64;
    let previous = tokens("old-access", "old-refresh", now + 60_000);
    oauth::store(&ANTHROPIC_FLOW, &dir, &previous).expect("store stale");

    // A directory where the temp file belongs makes the write fail without
    // ever touching the real token file.
    let mut name = oauth::token_path(&ANTHROPIC_FLOW, &dir)
        .file_name()
        .expect("token file name")
        .to_os_string();
    name.push(".tmp");
    let blocker = dir.join(&name);
    std::fs::create_dir(&blocker).expect("blocker directory");

    let refusal = oauth::access_via(&ANTHROPIC_FLOW, &dir, now, |_| {
        Ok(tokens("new-access", "rotated-refresh", now + 3_600_000))
    })
    .expect_err("the store must fail");
    assert!(refusal.contains("cannot write"), "{refusal}");
    assert_eq!(oauth::load(&ANTHROPIC_FLOW, &dir), Some(previous.clone()));

    std::fs::remove_dir(&blocker).expect("clear blocker");
    let refusal = oauth::access_via(&ANTHROPIC_FLOW, &dir, now, |_| Err("the network is down".to_string()))
        .expect_err("the renew must fail");
    assert_eq!(refusal, "the network is down");
    assert!(oauth::token_path(&ANTHROPIC_FLOW, &dir).is_file());
    assert_eq!(oauth::load(&ANTHROPIC_FLOW, &dir), Some(previous));
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn a_fresh_token_is_used_without_refreshing() {
    let dir = temp_dir("fresh");
    let now = 1_000_000_000_000u64;
    oauth::store(&ANTHROPIC_FLOW, &dir, &tokens("live-access", "live-refresh", now + 3_600_000)).expect("store");

    let access = oauth::access_via(&ANTHROPIC_FLOW, &dir, now, |_| {
        panic!("a fresh token must not be refreshed");
    })
    .expect("fresh path");

    assert_eq!(access, "live-access");
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn without_any_tokens_access_says_not_signed_in() {
    let dir = temp_dir("absent");
    let refusal = oauth::access_via(&ANTHROPIC_FLOW, &dir, 0, |_| unreachable!()).expect_err("no file");
    assert!(refusal.contains("not signed in"), "{refusal}");
    std::fs::remove_dir_all(&dir).ok();
}

#[test]
fn status_names_the_auth_method_and_the_key_file_outranks_oauth() {
    let dir = temp_dir("status");
    std::env::remove_var(ai_agent::ANTHROPIC.key_env);

    let far = oauth::now_ms() + 3_600_000;
    oauth::store(&ANTHROPIC_FLOW, &dir, &tokens("oauth-access", "oauth-refresh", far)).expect("store");
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
    assert!(!oauth::token_path(&ANTHROPIC_FLOW, &dir).exists());
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
