"""Select firmware builds, reusing only a successful preceding PR run."""
import json
import os
from pathlib import Path
import subprocess


def git(*args):
    return subprocess.check_output(["git", *args], text=True).strip()


def classify(paths):
    c6 = ".github/c6-firmware.json" in paths
    firmware = any(
        not (p.endswith(".md") or p.startswith("docs/") or p == "LICENSE")
        for p in paths
    )
    return firmware, c6


def comparison_base(event, previous_green):
    head = event["pull_request"]["head"]["sha"]
    base = event["pull_request"]["base"]["sha"]
    cumulative = git("merge-base", base, head)
    before = event.get("before")
    if event.get("action") == "synchronize" and before and previous_green:
        if (git("merge-base", before, head) == before
                and git("merge-base", base, before) == cumulative):
            return before
    return cumulative


def main():
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
    if os.environ["GITHUB_EVENT_NAME"] == "workflow_dispatch":
        firmware = True
        c6 = str(event.get("inputs", {}).get("build_c6", "false")).lower() == "true"
        reason = "Manual build"
    else:
        previous_green = False
        before = event.get("before")
        if event.get("action") == "synchronize" and before:
            runs = json.loads(subprocess.check_output([
                "gh", "api", "-X", "GET",
                f"repos/{os.environ['GITHUB_REPOSITORY']}/actions/workflows/build.yaml/runs",
                "-f", f"head_sha={before}", "-f", "event=pull_request",
                "-f", "per_page=100",
            ], text=True))
            previous_green = any(
                r["conclusion"] == "success"
                and r["head_branch"] == event["pull_request"]["head"]["ref"]
                for r in runs["workflow_runs"]
            )
        base = comparison_base(event, previous_green)
        head = event["pull_request"]["head"]["sha"]
        # Include both old and new paths for renamed/deleted build inputs.
        paths = git("diff", "--no-renames", "--name-only", base, head).splitlines()
        firmware, c6 = classify(paths)
        reason = f"Compared {base[:12]}..{head[:12]} ({len(paths)} changed paths)"
    config = json.loads(Path(".github/c6-firmware.json").read_text())
    with open(os.environ["GITHUB_OUTPUT"], "a") as output:
        for key, value in {
            "firmware": str(firmware).lower(), "c6": str(c6).lower(),
            "c6_ref": config["ref"], "c6_idf": config["idf"],
        }.items():
            print(f"{key}={value}", file=output)
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
        print(f"{reason}\n\nS3/P4 build: **{firmware}**; C6 build: **{c6}**.", file=summary)


if __name__ == "__main__":
    main()
