#!/usr/bin/env python3
"""Publishes the packaged builds of a release run (context/infrastructure.md, "Two storages").

    scripts/release/publish.py --dist dist [--channel preview] [--notes "..."] [--keep 2]

Reads the <archive>.json entries written by package.py next to the archives in --dist, uploads the
archives to the object storage (builds/<channel>/) and to the rolling GitHub release <channel> of
panda-builds, then switches the manifests (builds/<channel>/manifest.json in the storage,
manifests/<channel>.json in panda-builds), deletes the builds older than the previous one and
registers the release in the portal when a service token is set. A storage that fails is left out
of the manifest; the script finishes the rest and exits with an error at the end.

Environment: each storage is used when its variables are set, at least one is required.
  S3_ENDPOINT, S3_BUCKET, AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, AWS_DEFAULT_REGION
                       the object storage (aws cli); the files are uploaded with the public-read
                       ACL, as the portal backend uploads its public objects
  S3_PUBLIC_BASE_URL   the public address of the bucket, as in the server .env of the portal;
                       <S3_ENDPOINT>/<S3_BUCKET> by default
  GH_TOKEN             a token with write access to panda-builds (gh cli); PANDA_BUILDS_REPO names
                       the repository, toyship-studio/panda-builds by default
  PANDA_REGISTRY_CI_TOKEN  a portal service token with the releases scope; PANDA_REGISTRY_URL is the
                       API base, https://pandaengine.dev/api/v1 by default
  GITHUB_STEP_SUMMARY  the summary of the workflow step, written when set
"""

from __future__ import annotations

import argparse
import base64
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import NoReturn, Optional

DEFAULT_BUILDS_REPO = "toyship-studio/panda-builds"
DEFAULT_REGISTRY_URL = "https://pandaengine.dev/api/v1"
MANIFEST_NAME = "manifest.json"


class PublishError(Exception):
    pass


def fail(message: str) -> NoReturn:
    print(f"publish: {message}", file=sys.stderr)
    sys.exit(1)


def log(message: str) -> None:
    print(message, flush=True)


def run(command: list[str], *, capture: bool = False, check: bool = True) -> subprocess.CompletedProcess:
    shown = [argument if len(argument) <= 120 else argument[:60] + "…" for argument in command]
    log("$ " + " ".join(shown))
    return subprocess.run(command, check=check, text=True, capture_output=capture)


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def human_size(size: int) -> str:
    return f"{size / (1024 * 1024):.1f} MB"


# ---- Storages ----------------------------------------------------------------------------------

class S3Storage:
    """The object storage: builds/<channel>/<file>, the manifest next to the files."""

    name = "s3"

    def __init__(self, endpoint: str, bucket: str, public_url: str, channel: str) -> None:
        self.endpoint = endpoint
        self.bucket = bucket
        self.prefix = f"builds/{channel}/"
        self.public_url = (public_url or f"{endpoint.rstrip('/')}/{bucket}").rstrip("/")

    def _aws(self, *arguments: str, capture: bool = False) -> subprocess.CompletedProcess:
        return run(["aws", "s3", *arguments, "--endpoint-url", self.endpoint], capture=capture)

    def _uri(self, file_name: str) -> str:
        return f"s3://{self.bucket}/{self.prefix}{file_name}"

    def _upload(self, path: Path, file_name: str, *extra: str) -> None:
        # Builds and manifests are public; the bucket also holds private objects of the portal.
        self._aws("cp", str(path), self._uri(file_name), "--no-progress", "--acl", "public-read", *extra)

    def url(self, file_name: str) -> str:
        return f"{self.public_url}/{self.prefix}{file_name}"

    def prepare(self, title: str, notes_path: Path) -> None:
        pass

    def upload(self, path: Path) -> None:
        self._upload(path, path.name)

    def publish_manifest(self, manifest_path: Path, title: str, notes_path: Path) -> None:
        self._upload(manifest_path, MANIFEST_NAME, "--content-type", "application/json",
                     "--cache-control", "no-cache")

    def list_files(self) -> list[str]:
        output = self._aws("ls", f"s3://{self.bucket}/{self.prefix}", capture=True).stdout
        names = []
        for line in output.splitlines():
            parts = line.split()
            if len(parts) >= 4 and parts[0] != "PRE":
                names.append(parts[-1])
        return names

    def delete(self, file_name: str) -> None:
        self._aws("rm", self._uri(file_name))


class GitHubStorage:
    """The rolling release <channel> of panda-builds; the manifest is a file of the repository."""

    name = "github"

    def __init__(self, repo: str, channel: str) -> None:
        self.repo = repo
        self.channel = channel
        self.manifest_path = f"manifests/{channel}.json"

    def _gh(self, *arguments: str, capture: bool = False, check: bool = True) -> subprocess.CompletedProcess:
        return run(["gh", *arguments], capture=capture, check=check)

    def url(self, file_name: str) -> str:
        return f"https://github.com/{self.repo}/releases/download/{self.channel}/{file_name}"

    def manifest_url(self) -> str:
        return f"https://raw.githubusercontent.com/{self.repo}/main/{self.manifest_path}"

    def prepare(self, title: str, notes_path: Path) -> None:
        exists = self._gh("release", "view", self.channel, "--repo", self.repo, capture=True, check=False)
        if exists.returncode != 0:
            self._gh("release", "create", self.channel, "--repo", self.repo, "--title", title,
                     "--notes-file", str(notes_path), "--latest")

    def upload(self, path: Path) -> None:
        self._gh("release", "upload", self.channel, str(path), "--repo", self.repo, "--clobber")

    def publish_manifest(self, manifest_path: Path, title: str, notes_path: Path) -> None:
        endpoint = f"repos/{self.repo}/contents/{self.manifest_path}"
        existing = self._gh("api", endpoint, "--jq", ".sha", capture=True, check=False)
        content = base64.b64encode(manifest_path.read_bytes()).decode("ascii")
        arguments = ["api", "-X", "PUT", endpoint, "-f", f"message={title}", "-f", f"content={content}"]
        if existing.returncode == 0 and existing.stdout.strip():
            arguments += ["-f", f"sha={existing.stdout.strip()}"]
        self._gh(*arguments, capture=True)
        self._gh("release", "edit", self.channel, "--repo", self.repo, "--title", title,
                 "--notes-file", str(notes_path))

    def list_files(self) -> list[str]:
        output = self._gh("release", "view", self.channel, "--repo", self.repo, "--json", "assets",
                          "--jq", ".assets[].name", capture=True).stdout
        return [line.strip() for line in output.splitlines() if line.strip()]

    def delete(self, file_name: str) -> None:
        self._gh("release", "delete-asset", self.channel, file_name, "--repo", self.repo, "--yes")


# ---- The portal --------------------------------------------------------------------------------

class Portal:
    """Registers the release through the service API (panda-portal/docs/api.md, Releases)."""

    def __init__(self, url: str, token: str) -> None:
        self.url = url.rstrip("/")
        self.token = token

    def request(self, method: str, path: str, body: Optional[dict] = None) -> tuple[int, Optional[dict]]:
        data = json.dumps(body).encode("utf-8") if body is not None else None
        headers = {"Authorization": f"Bearer {self.token}", "Accept": "application/json"}
        if data is not None:
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(self.url + path, data=data, method=method, headers=headers)
        log(f"{method} {self.url}{path}")
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                raw = response.read().decode("utf-8", "replace")
                return response.status, (json.loads(raw) if raw.strip() else None)
        except urllib.error.HTTPError as error:
            raw = error.read().decode("utf-8", "replace")
            try:
                return error.code, json.loads(raw)
            except ValueError:
                return error.code, {"message": raw}
        except (urllib.error.URLError, OSError) as error:
            raise PublishError(f"portal: {method} {path} failed: {error}") from error

    def register(self, manifest: dict, assets: list[dict]) -> None:
        status, body = self.request("POST", "/releases", {
            key: manifest[key] for key in ("channel", "version", "build", "commit", "notes")
        })
        if status == 409:
            # The build was registered by an earlier attempt of this run.
            status, body = self.request("GET", f"/releases/{manifest['channel']}/{manifest['version']}")
        if status not in (200, 201) or not isinstance(body, dict) or "id" not in body:
            raise PublishError(f"portal: creating the release failed ({status}): {body}")
        release_id = body["id"]
        for asset in assets:
            status, body = self.request("PUT", f"/releases/{release_id}/assets/{asset['os']}/{asset['arch']}", {
                "kind": asset["kind"], "size": asset["size"], "sha256": asset["sha256"],
                "fileName": asset["fileName"], "sources": asset["sources"],
            })
            if status != 200:
                raise PublishError(f"portal: adding {asset['fileName']} failed ({status}): {body}")
        status, body = self.request("POST", f"/releases/{release_id}/publish")
        if status != 200:
            raise PublishError(f"portal: publishing failed ({status}): {body}")


# ---- The run -----------------------------------------------------------------------------------

def load_entries(dist: Path, channel: str) -> list[dict]:
    entries = []
    for meta in sorted(dist.glob("*.json")):
        if meta.name == MANIFEST_NAME:
            continue
        entry = json.loads(meta.read_text(encoding="utf-8"))
        archive = dist / entry["fileName"]
        if not archive.is_file():
            fail(f"{entry['fileName']} named by {meta.name} is missing")
        # Authoritative: signing and stapling change the dmg after it was packaged.
        entry["size"] = archive.stat().st_size
        entry["sha256"] = sha256_of(archive)
        entry["path"] = archive
        entries.append(entry)
    if not entries:
        fail(f"no packaged builds in {dist}")
    builds = {(entry["version"], entry["build"], entry["commit"]) for entry in entries}
    if len(builds) != 1:
        fail(f"the archives come from different builds: {sorted(builds)}")
    platforms = [(entry["os"], entry["arch"]) for entry in entries]
    if len(set(platforms)) != len(platforms):
        fail(f"two archives for one platform: {platforms}")
    version = entries[0]["version"]
    expected = "" if channel == "stable" else f"-{channel}.{entries[0]['build']}"
    if (channel == "stable" and "-" in version) or expected not in version:
        fail(f"{version} is not a build of the {channel} channel")
    return entries


def release_notes(manifest: dict, entries: list[dict]) -> str:
    lines = [f"Build {manifest['build']} of commit `{manifest['commit']}`, {manifest['date']}.", ""]
    repository = os.environ.get("GITHUB_REPOSITORY")
    sha = os.environ.get("GITHUB_SHA")
    if repository and sha:
        server = os.environ.get("GITHUB_SERVER_URL", "https://github.com")
        lines[0] = f"Build {manifest['build']} of commit [`{manifest['commit']}`]({server}/{repository}/commit/{sha}), {manifest['date']}."
    if manifest["notes"]:
        lines += [manifest["notes"], ""]
    lines += ["| File | Size | SHA-256 |", "| --- | --- | --- |"]
    for entry in entries:
        lines.append(f"| {entry['fileName']} | {human_size(entry['size'])} | `{entry['sha256']}` |")
    return "\n".join(lines) + "\n"


def step_summary(title: str, notes: str, manifest: dict, storages: list, failures: list[str]) -> str:
    lines = [f"## {title}", "", notes]
    for file in manifest["files"]:
        for url in file["urls"]:
            lines.append(f"- {url}")
    lines.append("")
    for storage in storages:
        if isinstance(storage, GitHubStorage):
            lines.append(f"Manifest: {storage.manifest_url()}")
        else:
            lines.append(f"Manifest: {storage.url(MANIFEST_NAME)}")
    if failures:
        lines += ["", "### Failures", ""] + [f"- {failure}" for failure in failures]
    return "\n".join(lines) + "\n"


def retention(storage, channel: str, keep: int) -> None:
    """Deletes the files of every build but the newest `keep` ones (the rolling channel)."""
    pattern = re.compile(rf"^PandaEditor-.+-{re.escape(channel)}\.(\d+)-")
    builds: dict[int, list[str]] = {}
    for file_name in storage.list_files():
        match = pattern.match(file_name)
        if match:
            builds.setdefault(int(match.group(1)), []).append(file_name)
    kept = sorted(builds, reverse=True)[:keep]
    for build in sorted(builds):
        if build in kept:
            continue
        for file_name in builds[build]:
            log(f"{storage.name}: deleting build {build}: {file_name}")
            storage.delete(file_name)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dist", type=Path, required=True, help="the archives and their .json entries")
    parser.add_argument("--channel", default="preview", help="the release channel (default: preview)")
    parser.add_argument("--notes", default="", help="release notes (Markdown)")
    parser.add_argument("--keep", type=int, default=2,
                        help="builds to keep on a rolling channel, the new one included (default: 2; 0 keeps all)")
    args = parser.parse_args()

    entries = load_entries(args.dist, args.channel)
    version, build, commit = entries[0]["version"], entries[0]["build"], entries[0]["commit"]
    title = f"Panda Editor {version}"
    date = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")

    storages: list = []
    if os.environ.get("S3_BUCKET"):
        endpoint = os.environ.get("S3_ENDPOINT")
        if not endpoint:
            fail("S3_BUCKET is set without S3_ENDPOINT")
        storages.append(S3Storage(endpoint, os.environ["S3_BUCKET"], os.environ.get("S3_PUBLIC_BASE_URL", ""),
                                  args.channel))
    if os.environ.get("GH_TOKEN"):
        storages.append(GitHubStorage(os.environ.get("PANDA_BUILDS_REPO", DEFAULT_BUILDS_REPO), args.channel))
    if not storages:
        fail("no storage is configured: set S3_ENDPOINT and S3_BUCKET, GH_TOKEN, or both")

    failures: list[str] = []
    manifest = {
        "channel": args.channel, "version": version, "build": build, "commit": commit, "date": date,
        "notes": args.notes.strip(), "files": [],
    }
    notes_path = args.dist / "RELEASE_NOTES.md"
    notes_path.write_text(release_notes(manifest, entries), encoding="utf-8")

    # 1. The files, to every storage. A storage that fails drops out of the manifest.
    sources: dict[str, list[dict]] = {entry["fileName"]: [] for entry in entries}
    working = []
    for storage in storages:
        try:
            storage.prepare(title, notes_path)
        except subprocess.CalledProcessError as error:
            failures.append(f"{storage.name}: preparing the release failed ({error})")
            continue
        working.append(storage)
        for entry in entries:
            try:
                storage.upload(entry["path"])
                sources[entry["fileName"]].append({"storage": storage.name, "url": storage.url(entry["fileName"])})
            except subprocess.CalledProcessError as error:
                failures.append(f"{storage.name}: uploading {entry['fileName']} failed ({error})")
    for entry in entries:
        if not sources[entry["fileName"]]:
            for failure in failures:
                log(f"::error::{failure}")
            fail(f"no storage took {entry['fileName']}; the manifests were not changed")

    # 2. The manifests, after every file is in place.
    for entry in entries:
        manifest["files"].append({
            "os": entry["os"], "arch": entry["arch"], "kind": entry["kind"], "size": entry["size"],
            "sha256": entry["sha256"], "urls": [source["url"] for source in sources[entry["fileName"]]],
        })
    manifest_path = args.dist / MANIFEST_NAME
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    for storage in working:
        try:
            storage.publish_manifest(manifest_path, title, notes_path)
        except subprocess.CalledProcessError as error:
            failures.append(f"{storage.name}: publishing the manifest failed ({error})")

    # 3. The build before the previous one goes away (the rolling channel keeps no history).
    if args.keep > 0 and args.channel != "stable":
        for storage in working:
            try:
                retention(storage, args.channel, args.keep)
            except subprocess.CalledProcessError as error:
                failures.append(f"{storage.name}: deleting old builds failed ({error})")

    # 4. The portal, when its token is set.
    token = os.environ.get("PANDA_REGISTRY_CI_TOKEN")
    if token:
        assets = [{**{key: entry[key] for key in ("os", "arch", "kind", "size", "sha256", "fileName")},
                   "sources": sources[entry["fileName"]]} for entry in entries]
        try:
            Portal(os.environ.get("PANDA_REGISTRY_URL") or DEFAULT_REGISTRY_URL, token).register(manifest, assets)
        except PublishError as error:
            failures.append(str(error))
    else:
        log("PANDA_REGISTRY_CI_TOKEN is not set; the release is not registered in the portal")

    summary = step_summary(title, notes_path.read_text(encoding="utf-8"), manifest, working, failures)
    log(summary)
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        with open(summary_path, "a", encoding="utf-8") as stream:
            stream.write(summary)

    if failures:
        for failure in failures:
            log(f"::error::{failure}")
        sys.exit(1)


if __name__ == "__main__":
    main()
