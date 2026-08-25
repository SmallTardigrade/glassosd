# Publishing glassosd

Everything here is a one-time setup. After it, `git push` produces installable
RPMs without further intervention.

---

## 1. GitHub

```bash
gh repo create glassosd --public --source=. --remote=origin \
   --description "Glass notification daemon, OSD and notification centre for Wayland"
git push -u origin main
git tag -a v0.1.0 -m "glassosd 0.1.0"
git push origin v0.1.0
```

The tag matters: the spec's `Source0` points at
`https://github.com/<you>/glassosd/archive/v%{version}/glassosd-%{version}.tar.gz`,
which GitHub generates automatically for any tag. Tag `v0.1.0` and that URL
exists; do not tag it and anyone running `rpmbuild` from the spec alone gets a
404.

Set these on the repo so people can find it:

- **Topics:** `wayland`, `notifications`, `notification-daemon`, `kde`,
  `plasma`, `hyprland`, `sway`, `layer-shell`, `qt6`, `qml`
- **About:** the one-line description above

### The three CI jobs

`.github/workflows/build.yml` runs on every push:

| job | what it catches |
|---|---|
| `compile` | a new dependency that was never added to the build docs — it builds in a bare `fedora:44` container, so anything you happen to have installed locally is invisible to it |
| `rpm` | a spec that has drifted from reality: missing `BuildRequires`, files installed but not listed in `%files`, files listed but not installed |
| `reuse` | a source file added without an SPDX header |

The `rpm` job uploads the built RPM as a workflow artifact, so a one-off build
for someone to test never needs a release.

---

## 2. COPR

COPR is Fedora's build service. It is free, it builds for every current Fedora
release, and users install from it with two commands.

### One-time setup

1. Create a Fedora account at <https://accounts.fedoraproject.org>.
2. Log in to <https://copr.fedorainfracloud.org/>.
3. Copy your API token from <https://copr.fedorainfracloud.org/api/> into
   `~/.config/copr`.
4. `sudo dnf install copr-cli`

### Create the project

```bash
copr-cli create glassosd \
  --chroot fedora-43-x86_64 \
  --chroot fedora-44-x86_64 \
  --chroot fedora-rawhide-x86_64 \
  --description "Glass notification daemon, OSD and notification centre for Wayland" \
  --instructions "sudo dnf copr enable smalltardigrade/glassosd && sudo dnf install glassosd"
```

Add `--chroot fedora-44-aarch64` if you want ARM. There is nothing
architecture-specific in the code.

### Point it at the repo

```bash
copr-cli add-package-scm glassosd \
  --name glassosd \
  --clone-url https://github.com/SmallTardigrade/glassosd.git \
  --commit main \
  --spec packaging/glassosd.spec \
  --type git \
  --method make_srpm \
  --webhook-rebuild on
```

`--method make_srpm` is what makes `.copr/Makefile` the source of truth. It
builds the tarball straight from the git checkout, so **COPR never needs a
GitHub release tarball to exist** — a push is enough. (The `Source0` URL in the
spec still matters for anyone building the spec by hand.)

### Wire up the webhook

1. COPR project → **Settings → Integrations** → copy the GitHub webhook URL.
2. GitHub repo → **Settings → Webhooks → Add webhook**
   - Payload URL: the URL from step 1
   - Content type: `application/json`
   - Events: *Just the push event* (add *Branch or tag creation* if you want
     tags to trigger builds too)

Now every push to `main` rebuilds.

### First build, by hand

```bash
copr-cli build glassosd --nowait
copr-cli watch-build <BUILD_ID>
```

### What users then run

```bash
sudo dnf copr enable smalltardigrade/glassosd
sudo dnf install glassosd
```

Note the COPR username is lower-case (`smalltardigrade`) even though the
GitHub one is not (`SmallTardigrade`). COPR derives it from the Fedora
account, and `dnf copr enable` is case-sensitive — the mixed-case form
returns a 404.

---

## 3. Testing the package before you publish

Do not skip this. It runs in a pristine Fedora container, so it catches every
"works on my machine" dependency:

```bash
podman run --rm -v "$PWD:/src:ro,Z" fedora:44 bash -euxo pipefail -c '
  dnf install -y -q rpm-build rpmdevtools git make "dnf-command(builddep)"
  cp -a /src /build && cd /build && rm -rf build
  git config --global --add safe.directory /build
  make -f .copr/Makefile srpm outdir=/build/srpm
  dnf builddep -y -q /build/srpm/*.src.rpm
  rpmbuild --rebuild /build/srpm/*.src.rpm --define "_topdir /build/rpmbuild"
  dnf install -y /build/rpmbuild/RPMS/*/*.rpm
  rpm -qlp /build/rpmbuild/RPMS/*/*.rpm
  ldd /usr/bin/glassosd | grep "not found" && exit 1
  echo OK
'
```

The container has no Wayland display, so the daemon cannot actually run there.
What this proves is that the package *builds, resolves and installs* from
nothing — which is the part that breaks.

---

## 4. Arch (AUR)

Most Hyprland users are on Arch, so this is where the second-largest audience
is. Every dependency is in `extra`; a PKGBUILD sketch is in
[DEPENDENCIES.md](DEPENDENCIES.md).

Publishing to the AUR:

```bash
git clone ssh://aur@aur.archlinux.org/glassosd.git aur-glassosd
cd aur-glassosd
# add PKGBUILD
makepkg --printsrcinfo > .SRCINFO
git add PKGBUILD .SRCINFO && git commit -m "glassosd 0.1.0" && git push
```

You need an AUR account with an SSH key uploaded. Use `glassosd` for a tagged
release or `glassosd-git` for a build from `main` — the AUR treats them as
separate packages and `-git` is the conventional name for the latter.

---

## 5. Getting into Fedora proper (later)

COPR is a third-party repo; Fedora proper is not. If it is worth doing:

- The package must pass [review][review], which the spec is written to survive
  — SPDX licence expression, no bundled libraries, correct `%license`.
- The chatty `%post` message is the one thing a reviewer is likely to object
  to. Be ready to drop it.
- You need a sponsor, and the review queue is measured in months.

Do this only once the project has users. COPR is the right home until then.

[review]: https://docs.fedoraproject.org/en-US/package-maintainers/Package_Review_Process/
