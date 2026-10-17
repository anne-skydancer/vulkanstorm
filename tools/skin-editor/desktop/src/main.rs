#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::{
    fs::{self, File},
    io::{self, BufRead, BufReader},
    path::{Path, PathBuf},
    process::{Child, Command, Stdio},
    sync::mpsc,
    thread,
    time::{Duration, Instant},
};

use rfd::{FileDialog, MessageButtons, MessageDialog, MessageDialogResult, MessageLevel};
use serde::Deserialize;
use serde_json::{Value, json};
use tao::{
    dpi::LogicalSize,
    event::{Event, WindowEvent},
    event_loop::{ControlFlow, EventLoopBuilder},
    window::WindowBuilder,
};
use tempfile::TempDir;
use wry::WebViewBuilder;

const BRIDGE: &str = include_str!("bridge.js");
type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

#[derive(Clone, Copy, Deserialize)]
#[serde(rename_all = "lowercase")]
enum SaveKind {
    Project,
    Export,
}

impl SaveKind {
    fn extension(self) -> &'static str {
        match self {
            Self::Project => "json",
            Self::Export => "zip",
        }
    }

    fn endpoint(self) -> &'static str {
        match self {
            Self::Project => "/api/validate",
            Self::Export => "/api/export",
        }
    }
}

#[derive(Deserialize)]
#[serde(tag = "action", rename_all = "lowercase", deny_unknown_fields)]
enum Request {
    Save {
        id: u32,
        kind: SaveKind,
        project: Value,
    },
    Close {
        dirty: bool,
    },
}

enum UserEvent {
    Request(Request),
    Saved { id: u32, result: Value },
    Error(String),
}

struct Server {
    child: Child,
    url: String,
    logs: TempDir,
    #[cfg(windows)]
    _job: Job,
}

impl Server {
    fn start(bundle: &Path) -> Result<Self> {
        let bundle = bundle.canonicalize().map_err(|error| {
            format!(
                "Cannot find bundled resources. Extract the entire archive before running: {error}"
            )
        })?;
        let python = bundle.join(if cfg!(windows) {
            "python/python.exe"
        } else {
            "python/bin/python3"
        });
        let editor = bundle.join("editor/editor.py");
        if !python.is_file() || !editor.is_file() {
            return Err(
                "Incomplete Skin Studio bundle. Extract the entire archive before running.".into(),
            );
        }
        let logs = tempfile::tempdir()?;
        let stderr = File::create(logs.path().join("server.log"))?;
        let mut command = Command::new(python);
        command
            .arg("-I")
            .arg("-B")
            .arg(editor)
            .arg("--root")
            .arg(bundle.join("library"))
            .args(["--port", "0"])
            .env_remove("PYTHONHOME")
            .env_remove("PYTHONPATH")
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(stderr);
        #[cfg(target_os = "linux")]
        install_parent_death_signal(&mut command);
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x08000000); // CREATE_NO_WINDOW
        }
        #[cfg(windows)]
        let job = Job::new()?;
        let child = command.spawn()?;
        let mut server = Self {
            child,
            url: String::new(),
            logs,
            #[cfg(windows)]
            _job: job,
        };
        #[cfg(windows)]
        server._job.assign(&server.child)?;
        let stdout = server.child.stdout.take().ok_or("Missing server stdout")?;
        let (sender, receiver) = mpsc::channel();
        thread::spawn(move || {
            for line in BufReader::new(stdout).lines() {
                match line {
                    Ok(line) => {
                        if let Some(url) = parse_announcement(&line) {
                            let _ = sender.send(url);
                        }
                    }
                    Err(error) => {
                        eprintln!("Cannot read Skin Studio server output: {error}");
                        break;
                    }
                }
            }
        });
        server.url = receiver
            .recv_timeout(Duration::from_secs(30))
            .map_err(|error| {
                format!(
                    "The editor server did not become ready: {error}\n{}",
                    server.diagnostics()
                )
            })?;
        let response = agent()
            .get(format!("{}/api/catalog", server.url))
            .call()
            .map_err(|error| format!("Cannot reach the editor server: {error}"))?;
        if response.status() != 200 {
            return Err(format!("Editor catalog failed: HTTP {}", response.status()).into());
        }
        Ok(server)
    }

    fn diagnostics(&self) -> String {
        match fs::read_to_string(self.logs.path().join("server.log")) {
            Ok(text) => text
                .chars()
                .rev()
                .take(8000)
                .collect::<String>()
                .chars()
                .rev()
                .collect(),
            Err(error) => format!("Cannot read server log: {error}"),
        }
    }

    fn stop(&mut self) -> io::Result<()> {
        if self.child.try_wait()?.is_none() {
            self.child.kill()?;
        }
        self.child.wait()?;
        Ok(())
    }
}

#[cfg(target_os = "linux")]
fn install_parent_death_signal(command: &mut Command) {
    use std::os::unix::process::CommandExt;

    let parent_pid = std::process::id() as libc::pid_t;
    unsafe {
        command.pre_exec(move || {
            if libc::prctl(libc::PR_SET_PDEATHSIG, libc::SIGKILL) == -1 {
                return Err(io::Error::last_os_error());
            }
            if libc::getppid() != parent_pid {
                return Err(io::Error::other(
                    "Skin Studio exited before its server could be started",
                ));
            }
            Ok(())
        });
    }
}

#[cfg(windows)]
struct Job {
    handle: *mut std::ffi::c_void,
}

#[cfg(windows)]
impl Job {
    fn new() -> io::Result<Self> {
        use std::{ffi::c_void, mem::size_of, ptr::null_mut};

        let handle = unsafe { CreateJobObjectW(null_mut(), std::ptr::null()) };
        if handle.is_null() {
            return Err(io::Error::last_os_error());
        }
        let mut limits = JobObjectExtendedLimitInformation {
            basic: JobObjectBasicLimitInformation {
                per_process_user_time_limit: 0,
                per_job_user_time_limit: 0,
                limit_flags: 0x2000,
                minimum_working_set_size: 0,
                maximum_working_set_size: 0,
                active_process_limit: 0,
                affinity: 0,
                priority_class: 0,
                scheduling_class: 0,
            },
            io: IoCounters {
                read_operation_count: 0,
                write_operation_count: 0,
                other_operation_count: 0,
                read_transfer_count: 0,
                write_transfer_count: 0,
                other_transfer_count: 0,
            },
            process_memory_limit: 0,
            job_memory_limit: 0,
            peak_process_memory_used: 0,
            peak_job_memory_used: 0,
        };
        let configured = unsafe {
            SetInformationJobObject(
                handle,
                9,
                (&mut limits as *mut JobObjectExtendedLimitInformation).cast::<c_void>(),
                size_of::<JobObjectExtendedLimitInformation>() as u32,
            )
        };
        if configured == 0 {
            let error = io::Error::last_os_error();
            unsafe {
                CloseHandle(handle);
            }
            return Err(error);
        }
        Ok(Self { handle })
    }

    fn assign(&self, child: &Child) -> io::Result<()> {
        use std::os::windows::io::AsRawHandle;

        let process = child.as_raw_handle().cast();
        let assigned = unsafe { AssignProcessToJobObject(self.handle, process) };
        if assigned == 0 {
            let error = io::Error::last_os_error();
            let mut in_job = 0;
            let queried = unsafe { IsProcessInJob(process, std::ptr::null_mut(), &mut in_job) };
            if queried == 0 {
                let query_error = io::Error::last_os_error();
                Err(io::Error::new(
                    error.kind(),
                    format!(
                        "Cannot assign server process to its job: {error}; \
                         process membership query failed: {query_error}"
                    ),
                ))
            } else {
                Err(io::Error::new(
                    error.kind(),
                    format!("{error}; child already belongs to a job: {}", in_job != 0),
                ))
            }
        } else {
            Ok(())
        }
    }
}

#[cfg(windows)]
impl Drop for Job {
    fn drop(&mut self) {
        if unsafe { CloseHandle(self.handle) } == 0 {
            eprintln!(
                "Cannot close Skin Studio process job: {}",
                io::Error::last_os_error()
            );
        }
    }
}

#[cfg(windows)]
#[repr(C)]
struct JobObjectBasicLimitInformation {
    per_process_user_time_limit: i64,
    per_job_user_time_limit: i64,
    limit_flags: u32,
    minimum_working_set_size: usize,
    maximum_working_set_size: usize,
    active_process_limit: u32,
    affinity: usize,
    priority_class: u32,
    scheduling_class: u32,
}

#[cfg(windows)]
#[repr(C)]
struct IoCounters {
    read_operation_count: u64,
    write_operation_count: u64,
    other_operation_count: u64,
    read_transfer_count: u64,
    write_transfer_count: u64,
    other_transfer_count: u64,
}

#[cfg(windows)]
#[repr(C)]
struct JobObjectExtendedLimitInformation {
    basic: JobObjectBasicLimitInformation,
    io: IoCounters,
    process_memory_limit: usize,
    job_memory_limit: usize,
    peak_process_memory_used: usize,
    peak_job_memory_used: usize,
}

#[cfg(windows)]
#[link(name = "kernel32")]
unsafe extern "system" {
    fn CreateJobObjectW(
        job_attributes: *mut std::ffi::c_void,
        name: *const u16,
    ) -> *mut std::ffi::c_void;
    fn SetInformationJobObject(
        job: *mut std::ffi::c_void,
        information_class: u32,
        information: *mut std::ffi::c_void,
        information_length: u32,
    ) -> i32;
    fn AssignProcessToJobObject(job: *mut std::ffi::c_void, process: *mut std::ffi::c_void) -> i32;
    fn IsProcessInJob(
        process: *mut std::ffi::c_void,
        job: *mut std::ffi::c_void,
        result: *mut i32,
    ) -> i32;
    fn CloseHandle(handle: *mut std::ffi::c_void) -> i32;
}

impl Drop for Server {
    fn drop(&mut self) {
        if let Err(error) = self.stop() {
            eprintln!("Cannot stop Skin Studio server: {error}");
        }
    }
}

fn agent() -> ureq::Agent {
    ureq::Agent::config_builder()
        .timeout_global(Some(Duration::from_secs(120)))
        .http_status_as_error(false)
        .build()
        .into()
}

fn parse_announcement(line: &str) -> Option<String> {
    let rest = line.strip_prefix("Skin editor: http://127.0.0.1:")?;
    let port = rest
        .strip_suffix(" (Ctrl+C to stop)")?
        .parse::<u16>()
        .ok()?;
    (port != 0).then(|| format!("http://127.0.0.1:{port}"))
}

fn save_to(url: &str, kind: SaveKind, project: &Value, path: &Path) -> Result<()> {
    let mut response = agent()
        .post(format!("{url}{}", kind.endpoint()))
        .send_json(project)?;
    if response.status() != 200 {
        return Err(format!(
            "Editor rejected the project: {}",
            response.body_mut().read_to_string()?
        )
        .into());
    }
    let parent = path.parent().ok_or("Save path has no parent directory")?;
    let mut output = tempfile::NamedTempFile::new_in(parent)?;
    io::copy(&mut response.body_mut().as_reader(), &mut output)?;
    output.as_file().sync_all()?;
    output.persist(path)?;
    Ok(())
}

fn save(url: &str, kind: SaveKind, project: &Value) -> Result<Option<String>> {
    let folder = project
        .get("folder")
        .and_then(Value::as_str)
        .ok_or("Missing project folder")?;
    if folder.is_empty()
        || !folder
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'_')
    {
        return Err("Invalid project folder".into());
    }
    let extension = kind.extension();
    let path = FileDialog::new()
        .set_title(match kind {
            SaveKind::Project => "Save Skin Studio project",
            SaveKind::Export => "Export viewer skin ZIP",
        })
        .set_file_name(format!("{folder}.{extension}"))
        .add_filter(extension, &[extension])
        .save_file();
    let Some(mut path) = path else {
        return Ok(None);
    };
    if path.extension().is_none() {
        path.set_extension(extension);
    }
    save_to(url, kind, project, &path)?;
    Ok(Some(path.display().to_string()))
}

fn show_error(message: &str) {
    eprintln!("{message}");
    MessageDialog::new()
        .set_level(MessageLevel::Error)
        .set_title("Skin Studio")
        .set_description(message)
        .show();
}

fn run(bundle: PathBuf, check: bool) -> Result<()> {
    let mut server = Server::start(&bundle)?;
    if check {
        let mut response = agent()
            .get(format!("{}/api/project?skin=default&theme=", server.url))
            .call()?;
        if response.status() != 200 {
            return Err(format!("Project check failed: {}", response.status()).into());
        }
        let project: Value = response.body_mut().read_json()?;
        let output = tempfile::tempdir()?;
        save_to(
            &server.url,
            SaveKind::Project,
            &project,
            &output.path().join("project.json"),
        )?;
        save_to(
            &server.url,
            SaveKind::Export,
            &project,
            &output.path().join("skin.zip"),
        )?;
        server.stop()?;
        println!("Bundled Python, catalog, project save, ZIP export and server shutdown: OK");
        return Ok(());
    }

    let event_loop = EventLoopBuilder::<UserEvent>::with_user_event().build();
    let window = WindowBuilder::new()
        .with_title("Vulkanstorm Skin Studio")
        .with_inner_size(LogicalSize::new(1440.0, 960.0))
        .with_min_inner_size(LogicalSize::new(900.0, 650.0))
        .build(&event_loop)?;
    let proxy = event_loop.create_proxy();
    let navigation = format!("{}/", server.url);
    let builder = WebViewBuilder::new()
        .with_url(&navigation)
        .with_initialization_script(BRIDGE)
        .with_navigation_handler(move |url| url == navigation)
        .with_ipc_handler(move |request| {
            let event = match serde_json::from_str::<Request>(request.body()) {
                Ok(request) => UserEvent::Request(request),
                Err(error) => UserEvent::Error(format!("Invalid desktop request: {error}")),
            };
            let _ = proxy.send_event(event);
        });
    #[cfg(windows)]
    let webview = builder.build(&window)?;
    #[cfg(target_os = "linux")]
    let webview = {
        use tao::platform::unix::WindowExtUnix;
        use wry::WebViewBuilderExtUnix;
        builder.build_gtk(
            window
                .default_vbox()
                .ok_or("Missing GTK window container")?,
        )?
    };
    let proxy = event_loop.create_proxy();
    let mut saving = false;
    event_loop.run(move |event, _, control_flow| {
        *control_flow = ControlFlow::WaitUntil(Instant::now() + Duration::from_secs(1));
        match event {
            Event::WindowEvent { event: WindowEvent::CloseRequested, .. } => {
                if let Err(error) = webview.evaluate_script(
                    "window.ipc.postMessage(JSON.stringify({action:'close',dirty:\
                     typeof window.skinStudioHasChanges==='function' && window.skinStudioHasChanges()}))",
                ) {
                    show_error(&format!("Cannot check unsaved changes: {error}"));
                }
            }
            Event::UserEvent(UserEvent::Request(Request::Close { dirty })) => {
                if saving {
                    MessageDialog::new().set_title("Skin Studio")
                        .set_description("A file is still being saved. Wait for it to finish before closing.")
                        .show();
                } else if !dirty || MessageDialog::new()
                    .set_title("Unsaved Skin Studio project")
                    .set_description("Close and discard unsaved project changes?")
                    .set_buttons(MessageButtons::YesNo)
                    .set_level(MessageLevel::Warning)
                    .show() == MessageDialogResult::Yes
                {
                    *control_flow = ControlFlow::Exit;
                }
            }
            Event::UserEvent(UserEvent::Request(Request::Save { id, kind, project })) => {
                if saving {
                    if let Err(error) = webview.evaluate_script(&format!(
                        "window.skinStudioComplete({id},{{error:'Another save is in progress.'}})"
                    )) {
                        show_error(&error.to_string());
                    }
                } else {
                    saving = true;
                    let proxy = proxy.clone();
                    let url = server.url.clone();
                    thread::spawn(move || {
                        let result = match save(&url, kind, &project) {
                            Ok(path) => json!({"path": path}),
                            Err(error) => json!({"error": error.to_string()}),
                        };
                        let _ = proxy.send_event(UserEvent::Saved { id, result });
                    });
                }
            }
            Event::UserEvent(UserEvent::Saved { id, result }) => {
                saving = false;
                if let Err(error) = webview.evaluate_script(&format!(
                    "window.skinStudioComplete({id},{result})"
                )) {
                    show_error(&format!("Cannot report file save result: {error}"));
                }
            }
            Event::UserEvent(UserEvent::Error(error)) => show_error(&error),
            Event::NewEvents(_) => match server.child.try_wait() {
                Ok(Some(status)) => {
                    show_error(&format!("The editor server stopped ({status}).\n{}", server.diagnostics()));
                    *control_flow = ControlFlow::Exit;
                }
                Ok(None) => {}
                Err(error) => {
                    show_error(&format!("Cannot monitor editor server: {error}"));
                    *control_flow = ControlFlow::Exit;
                }
            },
            Event::LoopDestroyed => {
                if let Err(error) = server.stop() {
                    show_error(&format!("Cannot stop editor server: {error}"));
                }
            }
            _ => {}
        }
    });
}

fn main() {
    let check = std::env::args_os().any(|arg| arg == "--check");
    let result = std::env::current_exe().map_err(Into::into).and_then(|exe| {
        run(
            exe.parent()
                .ok_or("Missing application directory")?
                .join("bundle"),
            check,
        )
    });
    if let Err(error) = result {
        if check {
            eprintln!("{error}");
        } else {
            show_error(&error.to_string());
        }
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Read, Write};
    use std::net::TcpListener;

    fn response_server(response: &'static str) -> (String, thread::JoinHandle<()>) {
        let listener = TcpListener::bind(("127.0.0.1", 0)).unwrap();
        let url = format!("http://{}", listener.local_addr().unwrap());
        let thread = thread::spawn(move || {
            let (stream, _) = listener.accept().unwrap();
            stream
                .set_read_timeout(Some(Duration::from_secs(5)))
                .unwrap();
            let mut stream = BufReader::new(stream);
            let mut length = 0;
            loop {
                let mut line = String::new();
                stream.read_line(&mut line).unwrap();
                if line == "\r\n" {
                    break;
                }
                if let Some(value) = line.to_ascii_lowercase().strip_prefix("content-length:") {
                    length = value.trim().parse::<usize>().unwrap();
                }
            }
            stream.read_exact(&mut vec![0; length]).unwrap();
            stream.get_mut().write_all(response.as_bytes()).unwrap();
        });
        (url, thread)
    }

    #[test]
    fn successful_save_replaces_destination() {
        let (url, thread) = response_server(
            "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nsaved",
        );
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("project.json");
        fs::write(&path, "original").unwrap();
        save_to(&url, SaveKind::Project, &json!({}), &path).unwrap();
        assert_eq!(fs::read_to_string(path).unwrap(), "saved");
        thread.join().unwrap();
    }

    #[test]
    fn rejected_or_interrupted_saves_preserve_destination() {
        for response in [
            "HTTP/1.1 400 Bad Request\r\nContent-Length: 7\r\nConnection: close\r\n\r\ninvalid",
            "HTTP/1.1 200 OK\r\nContent-Length: 100\r\nConnection: close\r\n\r\npartial",
        ] {
            let (url, thread) = response_server(response);
            let directory = tempfile::tempdir().unwrap();
            let path = directory.path().join("project.json");
            fs::write(&path, "original").unwrap();
            assert!(save_to(&url, SaveKind::Project, &json!({}), &path).is_err());
            assert_eq!(fs::read_to_string(path).unwrap(), "original");
            assert_eq!(fs::read_dir(directory.path()).unwrap().count(), 1);
            thread.join().unwrap();
        }
    }

    #[test]
    fn announcement_requires_a_loopback_port() {
        assert_eq!(
            parse_announcement("Skin editor: http://127.0.0.1:12345 (Ctrl+C to stop)"),
            Some("http://127.0.0.1:12345".into())
        );
        for invalid in [
            "Skin editor: http://example.com:80 (Ctrl+C to stop)",
            "Skin editor: http://127.0.0.1:0 (Ctrl+C to stop)",
            "Skin editor: http://127.0.0.1:65536 (Ctrl+C to stop)",
        ] {
            assert_eq!(parse_announcement(invalid), None);
        }
    }

    #[test]
    fn ipc_rejects_unknown_actions_and_save_kinds() {
        assert!(
            serde_json::from_value::<Request>(
                json!({"action":"save","id":1,"kind":"export","project":{}})
            )
            .is_ok()
        );
        assert!(
            serde_json::from_value::<Request>(
                json!({"action":"save","id":1,"kind":"execute","project":{}})
            )
            .is_err()
        );
        assert!(serde_json::from_value::<Request>(json!({"action":"execute"})).is_err());
    }

    #[cfg(target_os = "linux")]
    #[test]
    fn linux_parent_exit_kills_server_child() {
        const PROBE: &str = "SKIN_STUDIO_PARENT_DEATH_PROBE";
        if let Some(pid_file) = std::env::var_os(PROBE) {
            let mut command = Command::new("sleep");
            command.arg("60");
            install_parent_death_signal(&mut command);
            let child = command.spawn().unwrap();
            fs::write(pid_file, child.id().to_string()).unwrap();
            return;
        }

        let directory = tempfile::tempdir().unwrap();
        let pid_file = directory.path().join("server.pid");
        let status = Command::new(std::env::current_exe().unwrap())
            .args(["--exact", "tests::linux_parent_exit_kills_server_child"])
            .env(PROBE, &pid_file)
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .unwrap();
        assert!(status.success());
        let pid: libc::pid_t = fs::read_to_string(pid_file).unwrap().parse().unwrap();

        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            let process_status = fs::read_to_string(format!("/proc/{pid}/stat"));
            let stopped = match process_status {
                Err(error) if error.kind() == io::ErrorKind::NotFound => true,
                Ok(stat) => {
                    stat.rsplit_once(')')
                        .and_then(|(_, rest)| rest.trim_start().chars().next())
                        == Some('Z')
                }
                Err(error) => panic!("Cannot inspect server child process: {error}"),
            };
            if stopped {
                break;
            }
            assert!(
                Instant::now() < deadline,
                "server child survived its parent"
            );
            thread::sleep(Duration::from_millis(25));
        }
    }

    #[cfg(windows)]
    #[test]
    fn windows_job_close_kills_server_child() {
        use std::os::windows::process::CommandExt;

        let job = Job::new().unwrap();
        let mut child = Command::new("cmd.exe")
            .args(["/C", "ping -n 30 127.0.0.1 >NUL"])
            .creation_flags(0x08000000)
            .spawn()
            .unwrap();
        job.assign(&child).unwrap();
        drop(job);

        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            if child.try_wait().unwrap().is_some() {
                break;
            }
            assert!(Instant::now() < deadline, "server child survived its job");
            thread::sleep(Duration::from_millis(25));
        }
    }
}
