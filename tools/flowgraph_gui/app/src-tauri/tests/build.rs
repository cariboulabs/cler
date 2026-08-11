use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex, PoisonError};
use std::time::{Duration, Instant};

use cler_flowgraph_gui::build::{self, Emit, Jobs};
use cler_flowgraph_gui::document::{BuildRequirements, DraftState};
use cler_flowgraph_gui::provenance::{ArtifactCatalog, ArtifactRecord, ArtifactStatus};
use serde_json::Value;
use sha2::{Digest, Sha256};

static COUNTER: AtomicUsize = AtomicUsize::new(0);

type Seen = Arc<Mutex<Vec<(String, Value)>>>;

fn temp_dir(tag: &str) -> PathBuf {
    let unique = COUNTER.fetch_add(1, Ordering::Relaxed);
    let dir = std::env::temp_dir().join(format!(
        "cler-gui-build-{tag}-{}-{unique}",
        std::process::id()
    ));
    std::fs::remove_dir_all(&dir).ok();
    std::fs::create_dir_all(&dir).expect("temp directory");
    dir
}

fn write(target: &Path, text: &str) {
    if let Some(parent) = target.parent() {
        std::fs::create_dir_all(parent).expect("parent directory");
    }
    std::fs::write(target, text).expect("write");
}

fn draft_state(path: &Path, artifacts: ArtifactCatalog) -> DraftState {
    let bytes = std::fs::read(path).expect("draft source");
    DraftState {
        path: path.to_path_buf(),
        workspace: path.parent().expect("draft workspace").to_path_buf(),
        sha256: format!("{:x}", Sha256::digest(bytes)),
        requirements: BuildRequirements::default(),
        artifacts,
    }
}

fn repo(tag: &str) -> PathBuf {
    let root = temp_dir(tag);
    write(&root.join("include/cler.hpp"), "#pragma once\n");
    root
}

fn as_str(path: &Path) -> &str {
    path.to_str().expect("utf-8 path")
}

fn recorder() -> (Seen, Emit) {
    let seen: Seen = Arc::new(Mutex::new(Vec::new()));
    let sink = seen.clone();
    let emit: Emit = Arc::new(move |event: &str, payload: Value| {
        sink.lock()
            .unwrap_or_else(PoisonError::into_inner)
            .push((event.to_string(), payload));
    });
    (seen, emit)
}

fn await_event(seen: &Seen, name: &str) -> Value {
    let deadline = Instant::now() + Duration::from_secs(60);
    while Instant::now() < deadline {
        let found = seen
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .iter()
            .find(|(event, _)| event == name)
            .map(|(_, payload)| payload.clone());
        if let Some(payload) = found {
            return payload;
        }
        std::thread::sleep(Duration::from_millis(25));
    }
    panic!("{name} never arrived");
}

fn lines(seen: &Seen, name: &str) -> Vec<String> {
    seen.lock()
        .unwrap_or_else(PoisonError::into_inner)
        .iter()
        .filter(|(event, _)| event == name)
        .filter_map(|(_, payload)| payload["line"].as_str().map(str::to_string))
        .collect()
}

fn have_gxx() -> bool {
    std::process::Command::new("g++")
        .arg("--version")
        .output()
        .is_ok()
}

fn have_cmake() -> bool {
    std::process::Command::new("cmake")
        .arg("--version")
        .output()
        .is_ok()
}

#[test]
fn find_target_takes_the_newest_configured_build_directory() {
    let root = repo("newest");
    let source = root.join("desktop_examples/hello_world.cpp");
    write(&source, "int main() { return 0; }\n");
    write(&root.join("build-old/CMakeCache.txt"), "old\n");
    std::thread::sleep(Duration::from_millis(20));
    write(&root.join("build-new/CMakeCache.txt"), "new\n");

    let found = build::find_target(as_str(&source)).expect("target");
    assert!(found.available, "{:?}", found.reason);
    assert_eq!(found.name, "hello_world");
    assert_eq!(
        found.build_dir.as_deref(),
        Some(as_str(&root.join("build-new")))
    );
    assert_eq!(
        found.binary.as_deref(),
        Some(as_str(
            &root.join("build-new/desktop_examples/hello_world")
        ))
    );
}

#[test]
fn a_build_directory_that_holds_the_examples_beats_a_newer_one_without_them() {
    let root = repo("holds");
    let source = root.join("desktop_examples/hello_world.cpp");
    write(&source, "int main() { return 0; }\n");
    write(&root.join("build-full/CMakeCache.txt"), "full\n");
    std::fs::create_dir_all(root.join("build-full/desktop_examples")).expect("examples tree");
    std::thread::sleep(Duration::from_millis(20));
    write(&root.join("build-tsan/CMakeCache.txt"), "no examples\n");

    let found = build::find_target(as_str(&source)).expect("target");
    assert_eq!(
        found.build_dir.as_deref(),
        Some(as_str(&root.join("build-full")))
    );
}

#[test]
fn find_target_follows_the_example_subdirectory_into_the_build_tree() {
    let root = repo("subdir");
    let source = root.join("desktop_examples/ezgmsk/mod_demod_simulation.cpp");
    write(&source, "int main() { return 0; }\n");
    write(&root.join("build/CMakeCache.txt"), "configured\n");

    let found = build::find_target(as_str(&source)).expect("target");
    assert!(found.available);
    assert_eq!(
        found.binary.as_deref(),
        Some(as_str(
            &root.join("build/desktop_examples/ezgmsk/mod_demod_simulation")
        ))
    );
}

#[test]
fn a_file_outside_the_examples_has_no_target() {
    let root = repo("outside");
    let source = root.join("scratch/mine.cpp");
    write(&source, "int main() { return 0; }\n");
    write(&root.join("build/CMakeCache.txt"), "configured\n");

    let found = build::find_target(as_str(&source)).expect("answer");
    assert!(!found.available);
    assert_eq!(found.build_dir, None);
    let reason = found.reason.unwrap_or_default();
    assert!(reason.contains("desktop_examples"), "{reason}");
}

#[test]
fn without_a_configured_build_directory_the_reason_is_the_command_to_run() {
    let root = repo("unconfigured");
    let source = root.join("desktop_examples/hello_world.cpp");
    write(&source, "int main() { return 0; }\n");
    std::fs::create_dir_all(root.join("build")).expect("empty build dir");

    let found = build::find_target(as_str(&source)).expect("answer");
    assert!(!found.available);
    let reason = found.reason.unwrap_or_default();
    assert!(reason.contains("configure a build directory first"), "{reason}");
    assert!(
        reason.contains(&format!("cmake -B {}/build -S {}", root.display(), root.display())),
        "{reason}"
    );
}

#[test]
fn a_file_outside_a_cler_repository_is_refused() {
    let dir = temp_dir("stray");
    let source = dir.join("stray.cpp");
    write(&source, "int main() { return 0; }\n");

    let refusal = build::find_target(as_str(&source)).expect_err("no repository");
    assert!(refusal.contains("not inside a cler repository"), "{refusal}");

    let jobs = Jobs::default();
    let (_, emit) = recorder();
    let denied = build::check(&jobs, as_str(&source), emit).expect_err("no repository");
    assert!(denied.contains("include/cler.hpp"), "{denied}");
}

#[test]
fn check_streams_the_compiler_diagnostic_and_finishes_with_the_exit_code() {
    if !have_gxx() {
        eprintln!("skipping: no g++ on this machine");
        return;
    }
    let root = repo("check");
    let source = root.join("desktop_examples/broken.cpp");
    write(&source, "#include \"cler.hpp\"\nint main() { return nope; }\n");

    let jobs = Jobs::default();
    let (seen, emit) = recorder();
    build::check(&jobs, as_str(&source), emit).expect("check starts");

    let finished = await_event(&seen, "check-finished");
    assert_eq!(finished["path"].as_str(), Some(as_str(&source)));
    assert_eq!(finished["code"].as_i64(), Some(1));

    let streamed = lines(&seen, "check-output");
    let head = streamed
        .iter()
        .find(|line| line.contains("error:"))
        .unwrap_or_else(|| panic!("no diagnostic in {streamed:?}"));
    assert!(head.starts_with(&format!("{}:2:", source.display())), "{head}");
    assert!(head.contains("nope"), "{head}");
}

#[test]
fn a_clean_file_finishes_with_no_diagnostics() {
    if !have_gxx() {
        eprintln!("skipping: no g++ on this machine");
        return;
    }
    let root = repo("clean");
    let source = root.join("desktop_examples/fine.cpp");
    write(&source, "#include \"cler.hpp\"\nint main() { return 0; }\n");

    let jobs = Jobs::default();
    let (seen, emit) = recorder();
    build::check(&jobs, as_str(&source), emit).expect("check starts");

    assert_eq!(await_event(&seen, "check-finished")["code"].as_i64(), Some(0));
    assert_eq!(lines(&seen, "check-output"), Vec::<String>::new());
}

#[test]
fn a_draft_build_and_run_leave_the_repository_source_untouched() {
    if !have_cmake() {
        eprintln!("skipping: no cmake on this machine");
        return;
    }
    let root = repo("draft-project");
    write(
        &root.join("CMakeLists.txt"),
        "cmake_minimum_required(VERSION 3.16)\nproject(draft_test LANGUAGES CXX)\nadd_subdirectory(desktop_examples)\n",
    );
    write(
        &root.join("cmake/ClerBlockComponents.cmake"),
        "set(CLER_TEST_REGISTRY one)\n",
    );
    write(
        &root.join("desktop_examples/CMakeLists.txt"),
        "add_executable(hello_world hello_world.cpp)\n\
         target_include_directories(hello_world PRIVATE ${CMAKE_SOURCE_DIR}/include)\n\
         if(CLER_EDITOR_REQUIREMENTS_EXACT)\n\
           file(READ ${CLER_EDITOR_REQUIREMENTS_FILE} CLER_TEST_ORIGINS)\n\
           file(WRITE ${CMAKE_BINARY_DIR}/editor-requirements.txt ${CLER_TEST_ORIGINS})\n\
         endif()\n\
         add_custom_command(TARGET hello_world PRE_BUILD COMMAND ${CMAKE_COMMAND} -E sleep 1)\n",
    );
    let header = root.join("include/config.hpp");
    write(&header, "#define EXIT_CODE 7\n");
    let source = root.join("desktop_examples/hello_world.cpp");
    write(&source, "int main() { return 0; }\n");
    let configured = std::process::Command::new("cmake")
        .args(["-S", as_str(&root), "-B", as_str(&root.join("build"))])
        .status()
        .expect("configure source build");
    assert!(configured.success());

    let workspace = temp_dir("draft-workspace");
    let draft = workspace.join("hello_world.cpp");
    let draft_source = "#include \"config.hpp\"\nint main() { return EXIT_CODE; }\n";
    write(&draft, draft_source);
    let mut state = draft_state(&draft, ArtifactCatalog::default());
    state.requirements = BuildRequirements {
        origins: vec!["desktop_blocks/math".to_string()],
        exact: true,
    };
    let recorded: Arc<Mutex<Option<(String, ArtifactRecord)>>> = Arc::new(Mutex::new(None));
    let captured = recorded.clone();
    let jobs = Jobs::default();
    let (seen, emit) = recorder();
    let started = build::build_draft(
        &jobs,
        as_str(&source),
        &state,
        Arc::new(move |name, record| {
            *captured.lock().unwrap_or_else(PoisonError::into_inner) = Some((name, record));
            Ok(())
        }),
        emit,
    )
    .expect("draft build starts");
    assert!(matches!(
        build::find_draft_target(&jobs, as_str(&source), &state)
            .expect("building target")
            .artifact,
        ArtifactStatus::Building { .. }
    ));
    let changed_draft = workspace.join("changed.cpp");
    write(&changed_draft, "int main() { return 9; }\n");
    assert!(matches!(
        build::find_draft_target(
            &jobs,
            as_str(&source),
            &draft_state(&changed_draft, ArtifactCatalog::default()),
        )
        .expect("changed draft while building")
        .artifact,
        ArtifactStatus::Building { .. }
    ));
    let (_, duplicate_emit) = recorder();
    let refusal = build::build_draft(
        &jobs,
        as_str(&source),
        &state,
        Arc::new(|_, _| Ok(())),
        duplicate_emit,
    )
    .expect_err("a second window cannot replace the active build");
    assert!(
        refusal.contains(&format!("build job {}", started.job_id)),
        "{refusal}"
    );
    assert_eq!(await_event(&seen, "build-finished")["code"], 0);
    assert_eq!(
        seen.lock()
            .unwrap_or_else(PoisonError::into_inner)
            .iter()
            .filter(|(event, _)| event == "artifact-status-changed")
            .count(),
        2
    );
    assert_eq!(
        std::fs::read_to_string(&source).expect("source"),
        "int main() { return 0; }\n"
    );
    assert_eq!(
        std::fs::read_to_string(workspace.join("build/editor-requirements.txt"))
            .expect("CMake received requirements"),
        "desktop_blocks/math\n"
    );

    let (name, artifact) = recorded
        .lock()
        .unwrap_or_else(PoisonError::into_inner)
        .clone()
        .expect("artifact record");
    assert!(
        artifact
            .input_key
            .inputs
            .contains_key("dependency:repo:include/config.hpp"),
        "compiler dependencies are recorded"
    );
    let mut legacy = artifact.clone();
    legacy.input_key.inputs.remove("cmake");
    let mut legacy_artifacts = ArtifactCatalog::default();
    legacy_artifacts
        .put(name.clone(), legacy)
        .expect("legacy catalog accepts artifact");
    assert!(matches!(
        build::find_draft_target(
            &jobs,
            as_str(&source),
            &draft_state(&draft, legacy_artifacts),
        )
        .expect("legacy artifact")
        .artifact,
        ArtifactStatus::NeedsBuild { .. }
    ));
    let mut artifacts = ArtifactCatalog::default();
    artifacts.put(name, artifact).expect("catalog accepts artifact");
    let mut ready = draft_state(&draft, artifacts);
    ready.requirements = state.requirements.clone();
    let mut changed_requirements = ready.clone();
    changed_requirements.requirements = BuildRequirements {
        origins: vec!["desktop_blocks/udp".to_string()],
        exact: true,
    };
    assert!(matches!(
        build::find_draft_target(&jobs, as_str(&source), &changed_requirements)
            .expect("changed requirements")
            .artifact,
        ArtifactStatus::NeedsBuild { .. }
    ));
    let mut incomplete_requirements = ready.clone();
    incomplete_requirements.requirements.exact = false;
    assert!(matches!(
        build::find_draft_target(&jobs, as_str(&source), &incomplete_requirements)
            .expect("incomplete requirements")
            .artifact,
        ArtifactStatus::NeedsBuild { .. }
    ));
    let (run_seen, run_emit) = recorder();
    build::start_draft(&jobs, as_str(&source), &ready, run_emit).expect("draft run starts");
    assert_eq!(await_event(&run_seen, "run-finished")["code"], 7);

    write(&header, "#define EXIT_CODE 8\n");
    assert!(matches!(
        build::find_draft_target(&jobs, as_str(&source), &ready)
            .expect("changed dependency")
            .artifact,
        ArtifactStatus::NeedsBuild { .. }
    ));
    write(&header, "#define EXIT_CODE 7\n");

    let cmake_lists = root.join("desktop_examples/CMakeLists.txt");
    let cmake_source = std::fs::read_to_string(&cmake_lists).expect("CMake source");
    write(&cmake_lists, &format!("{cmake_source}# changed\n"));
    assert!(matches!(
        build::find_draft_target(&jobs, as_str(&source), &ready)
            .expect("changed CMake input")
            .artifact,
        ArtifactStatus::NeedsBuild { .. }
    ));
    write(&cmake_lists, &cmake_source);

    let registry = root.join("cmake/ClerBlockComponents.cmake");
    write(&registry, "set(CLER_TEST_REGISTRY two)\n");
    assert!(matches!(
        build::find_draft_target(&jobs, as_str(&source), &ready)
            .expect("changed component registry")
            .artifact,
        ArtifactStatus::NeedsBuild { .. }
    ));
    write(&registry, "set(CLER_TEST_REGISTRY one)\n");

    write(&draft, "int main() { return 9; }\n");
    let mut stale = draft_state(&draft, ready.artifacts.clone());
    stale.requirements = ready.requirements.clone();
    let (_, refused_emit) = recorder();
    let refusal = build::start_draft(&jobs, as_str(&source), &stale, refused_emit)
        .expect_err("a stale draft cannot run");
    assert!(refusal.contains("build the current draft"), "{refusal}");

    write(&draft, draft_source);
    let mut restored = draft_state(&draft, ready.artifacts.clone());
    restored.requirements = ready.requirements.clone();
    assert!(matches!(
        build::find_draft_target(&jobs, as_str(&source), &restored)
            .expect("restored target")
            .artifact,
        ArtifactStatus::Ready { .. }
    ));

    let cache = root.join("build/CMakeCache.txt");
    let configured = std::fs::read_to_string(&cache).expect("configured cache");
    write(
        &cache,
        &format!("CMAKE_BUILD_TYPE:STRING=Changed\n{configured}"),
    );
    assert!(matches!(
        build::find_draft_target(&jobs, as_str(&source), &restored)
            .expect("changed recipe")
            .artifact,
        ArtifactStatus::NeedsBuild { .. }
    ));
    write(&cache, &configured);

    let found =
        build::find_draft_target(&jobs, as_str(&source), &restored).expect("ready target");
    std::fs::remove_file(found.binary.expect("binary")).expect("remove artifact");
    assert!(matches!(
        build::find_draft_target(&jobs, as_str(&source), &restored)
            .expect("missing target")
            .artifact,
        ArtifactStatus::NeedsBuild { .. }
    ));
}

#[cfg(unix)]
#[test]
fn stop_target_interrupts_the_running_child() {
    use std::os::unix::fs::PermissionsExt;

    let root = repo("run");
    let source = root.join("desktop_examples/sleeper.cpp");
    write(&source, "int main() { return 0; }\n");
    write(&root.join("build/CMakeCache.txt"), "configured\n");
    let binary = root.join("build/desktop_examples/sleeper");
    write(&binary, "#!/bin/sh\necho awake\nexec sleep 30\n");
    std::fs::set_permissions(&binary, std::fs::Permissions::from_mode(0o755)).expect("chmod");

    let jobs = Jobs::default();
    let (seen, emit) = recorder();
    build::start(&jobs, as_str(&source), emit).expect("run starts");
    assert_eq!(await_event(&seen, "run-output")["line"].as_str(), Some("awake"));

    let started = Instant::now();
    build::stop(&jobs, as_str(&source)).expect("stop");
    await_event(&seen, "run-finished");
    assert!(started.elapsed() < Duration::from_secs(3), "the SIGINT was not enough");

    let missing = build::stop(&jobs, as_str(&source)).expect_err("nothing left to stop");
    assert!(missing.contains("nothing is running"), "{missing}");
}

#[cfg(unix)]
#[test]
fn an_old_waiter_cannot_remove_a_newer_job_for_the_same_target() {
    use std::os::unix::fs::PermissionsExt;

    let root = repo("replace-run");
    let source = root.join("desktop_examples/sleeper.cpp");
    write(&source, "int main() { return 0; }\n");
    write(&root.join("build/CMakeCache.txt"), "configured\n");
    let binary = root.join("build/desktop_examples/sleeper");
    write(&binary, "#!/bin/sh\necho first\nexec sleep 30\n");
    std::fs::set_permissions(&binary, std::fs::Permissions::from_mode(0o755)).expect("chmod");

    let jobs = Jobs::default();
    let (first_seen, first_emit) = recorder();
    let first = build::start(&jobs, as_str(&source), first_emit).expect("first starts");
    assert_eq!(await_event(&first_seen, "run-output")["jobId"], first.job_id);

    write(&binary, "#!/bin/sh\necho second\nexec sleep 30\n");
    std::fs::set_permissions(&binary, std::fs::Permissions::from_mode(0o755)).expect("chmod");
    let (second_seen, second_emit) = recorder();
    let second = build::start(&jobs, as_str(&source), second_emit).expect("second starts");
    assert!(second.job_id > first.job_id);
    assert_eq!(
        await_event(&second_seen, "run-output")["jobId"],
        second.job_id
    );

    std::thread::sleep(Duration::from_millis(100));
    build::stop(&jobs, as_str(&source)).expect("newer job remains owned");
    await_event(&second_seen, "run-finished");
}
