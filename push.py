"""Init + commit + push volt-bypass to github.com/HADOW2389/111.

Usage:
    python push.py                          # uses Windows Credential Manager (must have github.com creds saved)
    python push.py --token <PAT>            # explicit token, embedded in remote URL for one push
    python push.py --user <name> --token <PAT>
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent
REMOTE_OWNER = "HADOW2389"
REMOTE_REPO  = "111"
BRANCH       = "main"

def run(cmd, check=True, capture=False, extra_env=None):
    env = os.environ.copy()
    if extra_env:
        env.update(extra_env)
    print(f"$ {' '.join(cmd)}")
    r = subprocess.run(cmd, cwd=REPO_DIR, env=env, check=False,
                       stdout=subprocess.PIPE if capture else None,
                       stderr=subprocess.PIPE if capture else None,
                       text=True)
    out = ((r.stdout or "") + (r.stderr or "")).strip()
    if capture and out:
        print(out)
    if check and r.returncode != 0:
        sys.exit(f"failed ({r.returncode}): {' '.join(cmd)}\n{out}")
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--token", help="GitHub Personal Access Token (repo + workflow scopes)")
    ap.add_argument("--user",  default=REMOTE_OWNER, help="GitHub username for URL basic auth")
    ap.add_argument("--email", default=f"{REMOTE_OWNER}@users.noreply.github.com")
    ap.add_argument("--name",  default=REMOTE_OWNER)
    ap.add_argument("--message", default="init: volt-bypass")
    ap.add_argument("--force-remote", action="store_true", help="overwrite existing origin remote")
    args = ap.parse_args()

    # 1. git init if needed
    if not (REPO_DIR / ".git").exists():
        run(["git", "init", "-b", BRANCH])
    else:
        # Ensure current branch is `main`
        cur = run(["git", "rev-parse", "--abbrev-ref", "HEAD"], check=False, capture=True) or ""
        if cur and cur != BRANCH and cur != "HEAD":
            run(["git", "branch", "-M", BRANCH])

    # 2. local identity for this repo
    run(["git", "config", "user.name",  args.name])
    run(["git", "config", "user.email", args.email])

    # 3. stage + commit
    run(["git", "add", "-A"])
    diff = subprocess.run(["git", "diff", "--cached", "--quiet"], cwd=REPO_DIR)
    if diff.returncode == 0:
        print("[i] nothing new to commit")
    else:
        run(["git", "commit", "-m", args.message])

    # 4. remote
    if args.token:
        # embed in URL for one push; wipe after
        push_url = f"https://{args.user}:{args.token}@github.com/{REMOTE_OWNER}/{REMOTE_REPO}.git"
        display_url = f"https://github.com/{REMOTE_OWNER}/{REMOTE_REPO}.git"
    else:
        push_url = f"https://github.com/{REMOTE_OWNER}/{REMOTE_REPO}.git"
        display_url = push_url

    existing = run(["git", "remote"], check=False, capture=True).splitlines()
    if "origin" in existing:
        if args.force_remote or args.token:
            run(["git", "remote", "set-url", "origin", push_url])
    else:
        run(["git", "remote", "add", "origin", push_url])

    # 5. push. Force helper=manager so Windows Credential Manager pops if needed.
    print(f"[i] pushing to {display_url} (branch {BRANCH})")
    run(["git", "push", "-u", "origin", BRANCH],
        extra_env={"GIT_TERMINAL_PROMPT": "1"})

    # 6. if we used a token, scrub it from the remote URL
    if args.token:
        run(["git", "remote", "set-url", "origin",
             f"https://github.com/{REMOTE_OWNER}/{REMOTE_REPO}.git"])
        print("[i] token stripped from remote URL")

    print(f"\n[OK] pushed. Watch build: https://github.com/{REMOTE_OWNER}/{REMOTE_REPO}/actions")

if __name__ == "__main__":
    main()
