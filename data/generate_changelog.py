#!/usr/bin/env python3
"""Generate a ChangeLog from Git commit history."""

import subprocess
import re
import sys
from datetime import datetime
from collections import defaultdict
from pathlib import Path


def run_git_command(args: list[str], cwd: str = ".") -> str:
    """Run a git command and return its output."""
    try:
        result = subprocess.run(
            ["git"] + args,
            cwd=cwd,
            capture_output=True,
            text=True,
            check=True
        )
        return result.stdout.strip()
    except subprocess.CalledProcessError as e:
        print(f"Error running git command: {e}", file=sys.stderr)
        sys.exit(1)


def get_commits(repo_path: str = ".") -> list[dict]:
    """Get all commits with their metadata."""
    output = run_git_command([
        "log",
        "--format=%H|||%s|||%an|||%ae|||%ad|||%d",
        "--date=iso-strict",
        "--no-merges",
        "--all"
    ], cwd=repo_path)
    
    if not output:
        return []
    
    commits = []
    exclude_prefixes = ["index on ", "Merge"]
    
    for line in output.split("\n"):
        if not line.strip():
            continue
        
        parts = line.split("|||")
        if len(parts) < 6:
            continue
        
        hash_ = parts[0].strip()
        subject = parts[1].strip()
        author = parts[2].strip()
        email = parts[3].strip()
        date = parts[4].strip()
        refs = parts[5].strip()
        
        if not hash_ or not subject:
            continue
        
        skip = any(subject.startswith(p) for p in exclude_prefixes)
        if skip:
            continue
        
        if re.match(r"^Merge\s+(branch|remote|pull)", subject, re.IGNORECASE):
            continue
        
        tags = []
        if refs:
            tag_matches = re.findall(r'(?:refs/tags/|tag:\s*)([^,)\s]+)', refs)
            tags = tag_matches
        
        commits.append({
            "hash": hash_,
            "subject": subject,
            "body": "",
            "author": author,
            "email": email,
            "date": date,
            "tags": tags,
        })
    
    return commits


def parse_conventional_commit(subject: str) -> tuple[str, str, str]:
    """Parse a conventional commit message."""
    type_mapping = {
        "add": "feat",
        "fix": "fix",
    }
    
    known_types = {"feat", "fix", "docs", "style", "refactor", "perf", "test", "build", "ci", "chore", "revert"}
    
    subject_lower = subject.lower()
    for key, mapped in type_mapping.items():
        if subject_lower.startswith(key + ":") or subject_lower.startswith(key + " "):
            return mapped, "", subject
    
    pattern = r'^(\w+)(\(.+\))?:\s*(.+)$'
    match = re.match(pattern, subject)
    if match:
        type_ = match.group(1)
        if type_ not in known_types:
            return "", "", subject
        scope = match.group(2) or ""
        if scope:
            scope = scope[1:-1]
        desc = match.group(3)
        return type_, scope, desc
    
    return "", "", subject


def group_by_version(commits: list[dict]) -> dict:
    """Group commits by version/tag."""
    versions = defaultdict(list)
    current_version = "Unreleased"
    current_date = datetime.now().isoformat()
    
    sorted_commits = sorted(commits, key=lambda c: c["date"], reverse=True)
    
    for commit in sorted_commits:
        if commit["tags"]:
            tag = commit["tags"][0]
            if tag.startswith("v"):
                current_version = tag
            else:
                current_version = f"v{tag}"
            current_date = commit["date"]
        
        commit["version"] = current_version
        commit["version_date"] = current_date
        versions[current_version].append(commit)
    
    return versions


def format_changelog(versions: dict) -> str:
    """Format the ChangeLog."""
    lines = [f"# Changelog\n"]
    
    for version, commits in versions.items():
        if version == "Unreleased":
            lines.append(f"## [{version}]")
        else:
            date = commits[0]["version_date"][:10]
            lines.append(f"## [{version}] - {date}")
        
        lines.append("")
        
        by_type = defaultdict(list)
        seen_subjects = set()
        for commit in commits:
            type_, scope, desc = parse_conventional_commit(commit["subject"])
            key = (type_, scope, desc, commit["author"])
            if key in seen_subjects:
                continue
            seen_subjects.add(key)
            if type_:
                by_type[type_].append((scope, desc, commit["author"]))
            else:
                by_type["Other"].append(("", commit["subject"], commit["author"]))
        
        type_order = ["feat", "fix", "docs", "style", "refactor", "perf", "test", "build", "ci", "chore", "Other"]
        type_titles = {
            "feat": "Features",
            "fix": "Bug Fixes",
            "docs": "Documentation",
            "style": "Styles",
            "refactor": "Code Refactoring",
            "perf": "Performance Improvements",
            "test": "Tests",
            "build": "Build System",
            "ci": "Continuous Integration",
            "chore": "Chores",
            "Other": "Other Changes",
        }
        
        for type_ in type_order:
            if type_ in by_type:
                lines.append(f"### {type_titles.get(type_, type_.capitalize())}")
                for scope, desc, author in by_type[type_]:
                    if scope:
                        lines.append(f"- **{scope}**: {desc} ({author})")
                    else:
                        lines.append(f"- {desc} ({author})")
                lines.append("")
        
        lines.append("")
    
    return "\n".join(lines)


def main(repo_path: str = "."):
    """Main function."""
    repo = Path(repo_path)
    if not repo.exists():
        print(f"Error: {repo_path} does not exist", file=sys.stderr)
        sys.exit(1)
    
    is_git = run_git_command(["rev-parse", "--git-dir"], cwd=repo_path)
    if not is_git:
        print(f"Error: {repo_path} is not a git repository", file=sys.stderr)
        sys.exit(1)
    
    commits = get_commits(repo_path)
    versions = group_by_version(commits)
    changelog = format_changelog(versions)
    
    output_file = Path(repo_path) / "CHANGELOG.md"
    with open(output_file, "w") as f:
        f.write(changelog)
    
    print(f"ChangeLog written to {output_file}")


if __name__ == "__main__":
    repo = sys.argv[1] if len(sys.argv) > 1 else "."
    main(repo)