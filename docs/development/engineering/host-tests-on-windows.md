<p align="right">
  <a href="host-tests-on-windows.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Host Tests on Windows

[`tools/validate.sh`](../../../tools/validate.sh) runs the repository's host-test
gate. On a Windows host the gate runs to completion and every suite passes, with
21 test methods skipped: all of them need directory symlinks, which Windows
refuses without Developer Mode or elevation. This document records that
host-specific behavior and what it means for CI.

## Environment and reproduction

The results below were produced on:

| Item | Value |
| --- | --- |
| Host | Windows 11 (10.0.26300), Git Bash / MINGW64 |
| Compiler | llvm-mingw clang 22.1.8 (`x86_64-w64-windows-gnu`) used as `cc` and `gcc` |
| Python | 3.13.14 |
| ESP-IDF | v5.5.3 |

The static gate runs unattended on this host:

```console
$ ./tools/validate.sh --static
Repository checks: PASS (300 text files scanned)
...
Host tests: PASS
```

The firmware gate requires an activated ESP-IDF 5.5.3 environment and is not part
of these results. On native Windows that means the ESP-IDF PowerShell or Command
Prompt created by Espressif's installer rather than Git Bash, because ESP-IDF
refuses MSys/Mingw shells; see
[environment setup](environment-setup.md#windows-and-wsl2).

Individual C suites use the same command shape as the gate. For example:

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain \
    tests/test_alert.c main/alert.c main/usage_parse.c -o /tmp/test_alert
```

## What skips, and why: directory symlinks

Three suites probe whether the host can create a directory symlink and call
`skipTest` when it reports `WinError 1314` ("the client does not hold the required
privilege"):

| Suite | Skipped | What the skipped cases assert |
| --- | --- | --- |
| `test_check_repo` | 4 | Symlinked `VENDORED_DOC_ROOTS` entries and vendored symlinks into first-party trees stay checked |
| `test_archive_firmware` | 5 | Symlinked sources, archive roots, and packaged files are rejected |
| `test_install_passport_skills` | 12 | The installer creates relative links, preserves foreign entries, and reports partial failures |

This host has no such privilege, so all 21 skip. Enabling Developer Mode or
running elevated should clear them; that expectation is not verified here,
because it is a host-level change rather than something the repository can do.

The skills installer reports the limitation rather than failing silently: it
stops at the first link, keeps what it created, and prints the canonical
`skills/passport-*/SKILL.md` paths to read directly.

The same privilege limit explains why a repository check reports
`destination already exists and is not a symlink`. On this host the existing
`.agents/skills/passport-*` entries are directory junctions: `os.readlink()` and
`os.path.isjunction()` report them, but `stat.S_ISLNK()` does not, so
`inspect_installation()` classifies them as conflicts.

## CI relationship

`.github/workflows/` runs `./tools/validate.sh --static` and
`./tools/validate.sh --firmware` on `ubuntu-latest`, where creating symlinks needs
no privilege, so the 21 cases that skip here run there. The skips are a
Windows-host effect and do not indicate a red CI run.
