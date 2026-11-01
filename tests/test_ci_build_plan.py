import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "plan", Path(__file__).resolve().parents[1] / "tools/ci_build_plan.py")
plan = importlib.util.module_from_spec(spec)
spec.loader.exec_module(plan)


class BuildPlanTests(unittest.TestCase):
    def run_plan(self, event, name, green=False, paths=None):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "event.json").write_text(json.dumps(event))
            env = {"GITHUB_EVENT_PATH": str(root / "event.json"),
                   "GITHUB_EVENT_NAME": name, "GITHUB_REPOSITORY": "owner/repo",
                   "GITHUB_OUTPUT": str(root / "output"),
                   "GITHUB_STEP_SUMMARY": str(root / "summary")}
            runs = {"workflow_runs": [
                {"conclusion": "success" if green else "failure", "head_branch": "topic"}]}
            with patch.dict(os.environ, env), \
                    patch.object(plan.subprocess, "check_output", return_value=json.dumps(runs)), \
                    patch.object(plan, "comparison_base", return_value="base") as comparison, \
                    patch.object(plan, "git", return_value="\n".join(paths or [])):
                plan.main()
                if name == "pull_request":
                    self.assertEqual(comparison.call_args.args[1], green)
            return dict(line.split("=", 1) for line in (root / "output").read_text().splitlines())

    def test_manual_selection(self):
        self.assertEqual(self.run_plan({}, "workflow_dispatch")["c6"], "false")
        result = self.run_plan({"inputs": {"build_c6": "true"}}, "workflow_dispatch")
        self.assertEqual(result["firmware"], "true")
        self.assertEqual(result["c6"], "true")
        config = json.loads((Path(__file__).resolve().parents[1] / ".github/c6-firmware.json").read_text())
        self.assertEqual(result["c6_ref"], config["ref"])
        self.assertEqual(result["c6_idf"], config["idf"])

    def test_pr_selection_and_previous_run_result(self):
        event = {"action": "synchronize", "before": "old", "pull_request": {
            "head": {"sha": "head", "ref": "topic"}, "base": {"sha": "base"}}}
        for green in (True, False):
            result = self.run_plan(event, "pull_request", green, ["README.md"])
            self.assertEqual(result["firmware"], "false")
        result = self.run_plan(event, "pull_request", False, ["README.md", "main/app_main.cpp"])
        self.assertEqual(result["firmware"], "true")
        self.assertEqual(result["c6"], "false")

    def test_paths(self):
        for paths, expected in [
            (["README.md", "docs/POWER_HISTORY.md"], (False, False)),
            ([], (False, False)),
            (["main/web/settings.html"], (True, False)),
            (["components/my_wifi/my_wifi.cpp"], (True, False)),
            (["dependencies.lock.esp32p4"], (True, False)),
            ([".github/workflows/build.yaml"], (True, False)),
            ([".github/c6-firmware.json"], (True, True)),
            (["unknown-build-input"], (True, False)),
        ]:
            with self.subTest(paths=paths):
                self.assertEqual(plan.classify(paths), expected)

    def test_only_reuse_successful_ancestor_with_unchanged_base(self):
        event = {"action": "synchronize", "before": "old", "pull_request": {
            "head": {"sha": "head"}, "base": {"sha": "base"}}}
        with patch.object(plan, "git", return_value="common") as git:
            self.assertEqual(plan.comparison_base(event, False), "common")
            self.assertEqual(git.call_count, 1)
        with patch.object(plan, "git", side_effect=["common", "old", "common"]):
            self.assertEqual(plan.comparison_base(event, True), "old")
        with patch.object(plan, "git", side_effect=["common", "different"]):
            self.assertEqual(plan.comparison_base(event, True), "common")
        with patch.object(plan, "git", side_effect=["common", "old", "newbase"]):
            self.assertEqual(plan.comparison_base(event, True), "common")
        event["action"] = "opened"
        with patch.object(plan, "git", return_value="common"):
            self.assertEqual(plan.comparison_base(event, True), "common")
