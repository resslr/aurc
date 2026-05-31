# aurc

> A fast, secure package manager wrapper for Arch Linux with automatic AUR fallback.

<div align="center">

<img src="https://github.com/resslr/aurc/assets/122219240/218741a8-0faa-4693-9fa8-feeb5285bfa9" width="600"><br><br>

&ensp;[<kbd> <br> Install <br> </kbd>](#installation)&ensp;
&ensp;[<kbd> <br> Usage <br> </kbd>](#usage)&ensp;
&ensp;[<kbd> <br> Commands <br> </kbd>](#commands)&ensp;
&ensp;[<kbd> <br> Building <br> </kbd>](#building-from-source)&ensp;

<br><br>
</div>

## Features

- Wraps `pacman` for official repo packages
- Full AUR support — download, inspect PKGBUILD, build with `makepkg`
- **Smart install** — `aurc install <pkg>` checks official repos first, falls back to AUR automatically
- PKGBUILD review prompt before every AUR installation
- Built-in `self-update` — keeps aurc current without any third-party tools
- Configure your preferred editor for mirrorlist editing

## Requirements

**Platform:** Arch Linux

**Runtime dependencies:**

| Dependency | Purpose |
|---|---|
| `pacman` | Official repo operations |
| `sudo` | Privilege escalation |
| `base-devel` | `makepkg` for building AUR packages |
| `git` | Cloning repos |
| `curl` | AUR HTTP requests |
| `json-c` | AUR API response parsing |
| `less` | PKGBUILD viewer |

**Build dependencies:** `gcc`, `make`, `libarchive`

## Installation

**Via PKGBUILD** (recommended):

```bash
curl -L https://github.com/resslr/aurc/releases/latest/download/aurc-pkgbuild.tar.gz -o aurc-pkgbuild.tar.gz
tar xzf aurc-pkgbuild.tar.gz && cd aurc-pkgbuild
makepkg -si
```

**Via pre-built binary:**

```bash
# Replace ${version} with the release tag, e.g. 1.2.3
wget https://github.com/resslr/aurc/releases/latest/download/aurc-${version}-x86_64.pkg.tar.zst
sudo pacman -U aurc-${version}-x86_64.pkg.tar.zst
```

**From source:**

```bash
sudo pacman -S gcc make base-devel curl json-c libarchive git
git clone https://github.com/resslr/aurc.git
cd aurc/src && sudo make install
```

## Updating

Once installed, aurc can update itself:

```bash
aurc self-update
```

This clones the latest source, rebuilds, and reinstalls automatically.

## Usage

```
aurc <command> [package(s)]
```

### Commands

**Installation**

| Command | Description |
|---|---|
| `install <pkg...>` | Install packages — checks official repos first, falls back to AUR automatically |
| `install-aur <pkg...>` | Install AUR packages directly |
| `install-local <path>` | Install a local `.pkg.tar.zst` file |
| `install-force <pkg...>` | Force install, skipping dependency checks |

**Removal**

| Command | Description |
|---|---|
| `remove <pkg...>` | Remove packages |
| `remove-dep <pkg...>` | Remove packages and their dependencies |
| `remove-force <pkg...>` | Force remove, even if other packages depend on it |
| `remove-force-dep <pkg...>` | Force remove with dependencies |
| `remove-orp` | Remove orphaned packages |

**Search & Query**

| Command | Description |
|---|---|
| `search <query>` | Search official repositories |
| `search-aur <query>` | Search AUR — results sorted by relevance, interactive filter prompt after results |
| `search-aur -s <name>` | Show only the exact package name match |
| `query <pkg>` | Check if a package is installed |

**System**

| Command | Description |
|---|---|
| `update` | Update all system packages (`pacman -Syyu`) |
| `self-update` | Update aurc to the latest version |
| `refresh` | Refresh repository databases |
| `modify-repo` | Edit `/etc/pacman.d/mirrorlist` in your configured editor |
| `clear-aur-cache` | Clear the AUR build cache (`~/.cache/aurc/`) |
| `config -e <editor>` | Set the default editor for `modify-repo` |

**Options**

| Flag | Description |
|---|---|
| `-h`, `--help` | Show help |
| `-v`, `--version` | Show version |

### Example

```bash
# Smart install — finds in repos, or falls back to AUR
aurc install neovim

# Inspect and install an AUR package directly
aurc install-aur spotify

# Search AUR with interactive filter
aurc search-aur python

# Remove multiple packages
aurc remove firefox thunderbird

# Keep aurc up to date
aurc self-update
```
