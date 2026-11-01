# Windows release validation

This is the release gate for VibePulse's Windows **host service**. It does not
flash the ESP32 and must not print credentials, session contents, prompts, or
other secrets into the report.

New installations should first complete
[Windows host setup and recovery](windows-setup.md). This document is the
stricter certification pass: it deliberately repeats critical checks against
an exact clean candidate and adds lifecycle plus physical evidence.

Run the procedure from PowerShell as the Windows user who actually runs Codex
or Claude Code. The tokenserver reads that user's profile, so testing as
Administrator or SYSTEM proves the wrong installation.

## 1. Record the host without exposing account data

```powershell
[System.Environment]::OSVersion.VersionString
$env:PROCESSOR_ARCHITECTURE
$PSVersionTable.PSVersion
git --version
py -3 -c "import platform,sys; print(platform.platform()); print(sys.version)"
```

Python must be 3.11 or newer.

## 2. Validate an exact clean release

Use a new directory so old generated files cannot make the result greener or
redder than the release:

```powershell
$ValidationRoot = Join-Path $env:TEMP ("vibepulse-validation-" + [guid]::NewGuid())
git clone https://github.com/ftornberg/vibepulse.git $ValidationRoot
git -C $ValidationRoot checkout --detach v1.0.0
git -C $ValidationRoot describe --tags --always --dirty
git -C $ValidationRoot status --short
```

The description must be `v1.0.0` and status output must be empty. For a future
release, substitute the exact candidate tag or commit everywhere in this
document.

## 3. Run the complete Windows tokenserver suite

```powershell
Set-Location $ValidationRoot
$Modules = Get-Content test\tokenserver-suite.txt |
  Where-Object { $_ -and -not $_.StartsWith('#') }
py -3 -m pip install -r requirements-interaction-relay.txt
py -3 -m unittest $Modules -v
```

Record the number of tests, skips, failures/errors, and the exit code. A skip is
acceptable only when its reason is platform-specific and named in the output.

## 4. Validate the Task Scheduler installer without installing

```powershell
$AllErrors = @()
foreach ($Script in @(
  'tools\tokenserver\install-windows-task.ps1',
  'tools\tokenserver\run-windows-task.ps1',
  'test\windows-task-runner.ps1'
)) {
  $Tokens = $null
  $Errors = $null
  [System.Management.Automation.Language.Parser]::ParseFile(
    (Resolve-Path $Script),
    [ref]$Tokens,
    [ref]$Errors
  ) | Out-Null
  $AllErrors += $Errors
}
$AllErrors
.\tools\tokenserver\install-windows-task.ps1 -ValidateOnly `
  -GithubRepo "owner/repository" -ClaudePlan max5x `
  -ClaudePlanCostUsd "100" -CodexPlan pro -CodexPlanCostUsd "20"
.\test\windows-task-runner.ps1
```

The parser must return no errors for all three scripts. `-ValidateOnly` must
report the exact checkout and a Python 3.11+ interpreter, construct the action,
both triggers, and supported settings through the host's real ScheduledTasks
module, and must say that it made no Task Scheduler changes.
The runner test must pass stdout/stderr capture, log rotation, and paths
containing spaces and non-ASCII characters. When Codex is installed, the
scheduled process must receive its verified bin directory without changing the
user's global PATH. If the user already has a custom `CODEX_HOME`, the task
must receive that verified directory process-locally without editing Codex
settings or authentication.
It must also prove exact forwarding of the optional GitHub repository, named
plan labels, and per-provider subscription costs. A Windows support claim is
not allowed to silently drop display inputs that work in a foreground launch.
The installed VibePulse MCP row must report `tool_timeout_sec: 130`; a missing
timeout lets Codex cancel the bridge before the panel's bounded 120-second
answer window expires and is a failed interaction gate.

## 5. Exercise the real local sources on a temporary port

Start the service in a foreground PowerShell on an unused port:

```powershell
py -3 tools\tokenserver\tokenserver.py --port 8738
```

In a second PowerShell:

```powershell
$Root = Invoke-RestMethod http://127.0.0.1:8738/
$Tokens = Invoke-RestMethod http://127.0.0.1:8738/api/tokens
$Agent = Invoke-RestMethod http://127.0.0.1:8738/api/agent-status
$Max = Invoke-RestMethod http://127.0.0.1:8738/api/max-tracker

$Root | Select-Object service, rev, srcFingerprint, claudeProbe,
  claudeCredential, @{Name='panel'; Expression={$_.interactions.panel}}
$Tokens | Select-Object v, claudeWeekStale, codexWeekStale
$Agent | Select-Object v, seq
$Max | Select-Object v
```

Never paste the unfiltered response into an issue. Report only safe status,
presence/type, staleness, and revision fields. Stop the foreground service with
Ctrl+C when the checks are complete.

Then run the read-only setup diagnostics from the release checkout:

```powershell
py -3 tools\vibepulse_setup.py status
py -3 tools\vibepulse_setup.py doctor
```

Doctor may contact the local service and configured relay, but it must not
install, enable, disable, or change provider choices.

## 6. Inspect autostart and the firewall

Inspect first; do not silently create or change either surface:

```powershell
Get-ScheduledTask -TaskName "VibePulse tokenserver" -ErrorAction SilentlyContinue |
  Select-Object TaskName, State, Author

Get-NetFirewallRule -ErrorAction SilentlyContinue |
  Where-Object DisplayName -Like "*VibePulse*" |
  Select-Object DisplayName, Enabled, Direction, Action, Profile
```

If no inbound rule exists and direct LAN mode is required, create one from an
elevated PowerShell for TCP 8737 on **Private** networks only:

```powershell
New-NetFirewallRule -DisplayName "VibePulse tokenserver" -Direction Inbound `
  -Protocol TCP -LocalPort 8737 -Action Allow -Profile Private
```

Do not open the service on the Public profile. From another computer on the
same LAN, verify the PC's real address rather than localhost:

```powershell
Test-NetConnection -ComputerName <PC-LAN-IP> -Port 8737
```

Install `requirements-discovery.txt` in the scheduled task's exact Python
environment. Require `GET /` → `discovery.status: ready`, then prove a real
panel poll. The firmware caches one healthy `_vibepulse._tcp.local` origin and
may select another advertising Mac/PC after failure. Keep a DHCP-reserved
compiled URL as the multicast-blocked fallback; discovery is additive, never
a reason to accept an unstable fallback.

## 7. Verify the real service lifecycle

Only after the release checkout and installer validation pass:

```powershell
.\tools\tokenserver\install-windows-task.ps1 `
  -GithubRepo "owner/repository" -ClaudePlan max5x `
  -ClaudePlanCostUsd "100" -CodexPlan pro -CodexPlanCostUsd "20"
Get-ScheduledTaskInfo -TaskName "VibePulse tokenserver"
Invoke-RestMethod http://127.0.0.1:8737/ |
  Select-Object service, rev, srcFingerprint, discovery, claudeProbe, claudeCredential,
    @{Name='panel'; Expression={$_.interactions.panel}}
```

Verify all of these and record timestamps:

1. the task starts immediately;
2. it starts again after sign-out/sign-in;
3. after the tokenserver process is terminated once, the five-minute
   watchdog starts a new process with the same release revision;
4. it returns after sleep/resume;
5. one full Windows reboot does not leave the panel stale.

The v1.0.0 scheduler wrapper writes bounded stdout/stderr to
`%LOCALAPPDATA%\VibePulse\Logs\torget-tokenserver.log` and retains one bounded
`.old` tail. The tagged v0.7.1 task predated that log. Allow a bounded source
warm-up after a session or power transition, then require a continuous fresh
window; a single transient success is not enough.

## 8. Close the physical loop

The final check requires the real panel and a human tap. Use the exact short
Codex question:

`Ser du APPROVE?`

Options:

- `Ja` — `APPROVE syns` (recommended)
- `Nej` — `APPROVE saknas`

Pass only when the panel visibly shows **APPROVE**, a human taps it, and the
call returns `status: answered`, `option_index: 0`, `answer: Ja`. Before the
test, compare `git describe --tags --always --dirty` from the firmware build
checkout with the service's `otaAvailableVersion`.

Silence, timeout, panel absence, **LEAVE IT**, computer fallback, or a private
**SOMETHING IS WAITING** screen without buttons is a fail or an explicit not
tested result—never approval.

## Release verdict

The first real-PC baseline for the tagged v0.7.1 release is recorded in
[Windows v0.7.1 read-only validation](superpowers/reviews/2026-08-27-windows-v0.7.1-read-only.md).
It is intentionally PARTIAL and must not be cited as a physical end-to-end
pass.

The later
[current-main checkpoint](superpowers/reviews/2026-08-28-windows-current-main-partial.md)
records post-v0.7.1 Task Scheduler, logging, provider, and Codex app-server
evidence. It is also intentionally PARTIAL: an earlier commit's PASS does not
automatically pass a later commit, and an unavailable PC turns unfinished rows
into NOT TESTED rather than success.

The subsequent
[v1.0 core and physical checkpoint](superpowers/reviews/2026-08-28-windows-v1-core-physical.md)
records the recovered real-host gates and the exact human panel answer on
`bee5d8c`. The later
[v1.0 full lifecycle continuation](superpowers/reviews/2026-08-28-windows-v1-full-lifecycle.md)
records real sign-out/sign-in, sleep/resume, and reboot against that same
installed runtime. Together they close every required Windows row for v1.0.0;
neither report is evidence for a later untested runtime revision.

Record each row as PASS, FAIL, or NOT TESTED:

| Gate | Required to announce Windows support |
|---|---|
| Exact clean release checkout | Yes |
| Complete Windows tokenserver suite | Yes |
| Installer parser + `-ValidateOnly` | Yes |
| Real Codex quota source | Yes when Codex is claimed |
| Real Claude quota source or honest unavailable state | Yes when Claude is claimed |
| Task Scheduler start/restart/sign-in | Yes |
| Private-profile firewall + LAN reachability | Yes for direct LAN |
| Recent panel polling | Yes for panel support |
| Physical Codex question and human answer | Yes for “answer from the panel” |
| Sleep/resume and reboot | Yes |

Any required FAIL or NOT TESTED means the narrow component can be described as
automated or previewed, but not announced as physically end-to-end validated.
